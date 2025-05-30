#include "message_handler.h"
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace usb_redirector {
namespace network {

uint32_t MessageHandler::next_sequence_ = 1;

NetworkMessage::NetworkMessage(MessageType type, const std::vector<uint8_t>& data)
    : payload(data) {
    header.magic = MessageHandler::MESSAGE_MAGIC;
    header.type = static_cast<uint32_t>(type);
    header.length = static_cast<uint32_t>(data.size());
    header.sequence = MessageHandler::GetNextSequence();
    header.checksum = 0; // 将在序列化时计算
}

NetworkMessage::NetworkMessage(MessageType type, const uint8_t* data, size_t len)
    : payload(data, data + len) {
    header.magic = MessageHandler::MESSAGE_MAGIC;
    header.type = static_cast<uint32_t>(type);
    header.length = static_cast<uint32_t>(len);
    header.sequence = MessageHandler::GetNextSequence();
    header.checksum = 0; // 将在序列化时计算
}

MessageHandler::MessageHandler() {
    receive_buffer_.reserve(MAX_MESSAGE_SIZE);
}

void MessageHandler::ProcessReceivedData(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 将数据添加到接收缓冲区
    receive_buffer_.insert(receive_buffer_.end(), data, data + len);

    // 处理完整的消息
    while (receive_buffer_.size() >= sizeof(MessageHeader)) {
        MessageHeader header;
        std::memcpy(&header, receive_buffer_.data(), sizeof(MessageHeader));

        // 转换字节序
        header.magic = ntohl(header.magic);
        header.type = ntohl(header.type);
        header.length = ntohl(header.length);
        header.sequence = ntohl(header.sequence);
        header.checksum = ntohl(header.checksum);

        // 验证魔数
        if (header.magic != MESSAGE_MAGIC) {
            // 查找下一个可能的魔数位置
            auto it = std::search(receive_buffer_.begin() + 1, receive_buffer_.end(),
                                reinterpret_cast<const uint8_t*>(&MESSAGE_MAGIC),
                                reinterpret_cast<const uint8_t*>(&MESSAGE_MAGIC) + sizeof(MESSAGE_MAGIC));
            if (it != receive_buffer_.end()) {
                receive_buffer_.erase(receive_buffer_.begin(), it);
            } else {
                receive_buffer_.clear();
            }
            continue;
        }

        // 检查消息长度
        if (header.length > MAX_MESSAGE_SIZE) {
            receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.begin() + sizeof(MessageHeader));
            continue;
        }

        // 检查是否有完整的消息
        size_t total_size = sizeof(MessageHeader) + header.length;
        if (receive_buffer_.size() < total_size) {
            break; // 等待更多数据
        }

        // 验证消息
        if (ValidateMessage(header, receive_buffer_.data() + sizeof(MessageHeader))) {
            ProcessCompleteMessage(receive_buffer_.data(), total_size);
        }

        // 移除已处理的消息
        receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.begin() + total_size);
    }
}

std::vector<uint8_t> MessageHandler::SerializeMessage(const NetworkMessage& message) {
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(MessageHeader) + message.payload.size());

    MessageHeader header = message.header;

    // 计算校验和
    header.checksum = CalculateChecksum(message.payload.data(), message.payload.size());

    // 转换字节序
    header.magic = htonl(header.magic);
    header.type = htonl(header.type);
    header.length = htonl(header.length);
    header.sequence = htonl(header.sequence);
    header.checksum = htonl(header.checksum);

    // 复制头部和载荷
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    if (!message.payload.empty()) {
        std::memcpy(buffer.data() + sizeof(MessageHeader),
                   message.payload.data(), message.payload.size());
    }

    return buffer;
}

NetworkMessage MessageHandler::CreateDeviceListRequest() {
    return NetworkMessage(MessageType::DEVICE_LIST_REQUEST, std::vector<uint8_t>());
}

NetworkMessage MessageHandler::CreateDeviceListResponse(const std::vector<protocol::UsbipDeviceInfo>& devices) {
    auto data = protocol::UsbipProtocol::SerializeDeviceList(devices);
    return NetworkMessage(MessageType::DEVICE_LIST_RESPONSE, data);
}

NetworkMessage MessageHandler::CreateDeviceImportRequest(const std::string& bus_id) {
    std::vector<uint8_t> data(bus_id.begin(), bus_id.end());
    return NetworkMessage(MessageType::DEVICE_IMPORT_REQUEST, data);
}

NetworkMessage MessageHandler::CreateDeviceImportResponse(bool success, const std::string& error) {
    std::vector<uint8_t> data;
    data.push_back(success ? 1 : 0);
    if (!success && !error.empty()) {
        data.insert(data.end(), error.begin(), error.end());
    }
    return NetworkMessage(MessageType::DEVICE_IMPORT_RESPONSE, data);
}

NetworkMessage MessageHandler::CreateUrbSubmit(const protocol::UsbUrb& urb) {
    // 这里需要将URB序列化为USBIP格式
    // 暂时返回空消息，后续实现
    std::vector<uint8_t> data;
    return NetworkMessage(MessageType::URB_SUBMIT, data);
}

NetworkMessage MessageHandler::CreateUrbResponse(const protocol::UsbUrb& urb) {
    // 这里需要将URB序列化为USBIP格式
    // 暂时返回空消息，后续实现
    std::vector<uint8_t> data;
    return NetworkMessage(MessageType::URB_RESPONSE, data);
}

NetworkMessage MessageHandler::CreateDeviceDisconnect(const std::string& bus_id) {
    std::vector<uint8_t> data(bus_id.begin(), bus_id.end());
    return NetworkMessage(MessageType::DEVICE_DISCONNECT, data);
}

NetworkMessage MessageHandler::CreateHeartbeat() {
    return NetworkMessage(MessageType::HEARTBEAT, std::vector<uint8_t>());
}

void MessageHandler::ProcessCompleteMessage(const uint8_t* data, size_t len) {
    if (len < sizeof(MessageHeader)) {
        return;
    }

    MessageHeader header;
    std::memcpy(&header, data, sizeof(MessageHeader));

    // 转换字节序
    header.magic = ntohl(header.magic);
    header.type = ntohl(header.type);
    header.length = ntohl(header.length);
    header.sequence = ntohl(header.sequence);
    header.checksum = ntohl(header.checksum);

    NetworkMessage message;
    message.header = header;

    if (header.length > 0) {
        const uint8_t* payload = data + sizeof(MessageHeader);
        message.payload.assign(payload, payload + header.length);
    }

    if (message_callback_) {
        message_callback_(message);
    }
}

uint32_t MessageHandler::CalculateChecksum(const uint8_t* data, size_t len) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        checksum += data[i];
    }
    return checksum;
}

bool MessageHandler::ValidateMessage(const MessageHeader& header, const uint8_t* payload) {
    if (header.length == 0) {
        return header.checksum == 0;
    }

    uint32_t calculated_checksum = CalculateChecksum(payload, header.length);
    return calculated_checksum == header.checksum;
}

uint32_t MessageHandler::GetNextSequence() {
    return next_sequence_++;
}

} // namespace network
} // namespace usb_redirector
