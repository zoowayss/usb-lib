#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <queue>
#include "protocol/usbip_protocol.h"
#include "protocol/usb_types.h"

namespace usb_redirector {
namespace network {

// 消息类型
enum class MessageType : uint32_t {
    DEVICE_LIST_REQUEST = 1,
    DEVICE_LIST_RESPONSE = 2,
    DEVICE_IMPORT_REQUEST = 3,
    DEVICE_IMPORT_RESPONSE = 4,
    URB_SUBMIT = 5,
    URB_RESPONSE = 6,
    DEVICE_DISCONNECT = 7,
    HEARTBEAT = 8
};

// 网络消息头
struct MessageHeader {
    uint32_t magic;         // 魔数，用于验证
    uint32_t type;          // 消息类型
    uint32_t length;        // 消息长度（不包括头部）
    uint32_t sequence;      // 序列号
    uint32_t checksum;      // 校验和
} __attribute__((packed));

// 网络消息
struct NetworkMessage {
    MessageHeader header;
    std::vector<uint8_t> payload;

    NetworkMessage() = default;
    NetworkMessage(MessageType type, const std::vector<uint8_t>& data);
    NetworkMessage(MessageType type, const uint8_t* data, size_t len);
};

class MessageHandler {
public:
    using MessageCallback = std::function<void(const NetworkMessage& message)>;

    static constexpr uint32_t MESSAGE_MAGIC = 0x55534249; // "USBI"
    static constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB

    MessageHandler();
    ~MessageHandler() = default;

    // 设置消息回调
    void SetMessageCallback(MessageCallback callback) {
        message_callback_ = std::move(callback);
    }

    // 处理接收到的数据
    void ProcessReceivedData(const uint8_t* data, size_t len);

    // 序列化消息为网络数据
    std::vector<uint8_t> SerializeMessage(const NetworkMessage& message);

    // 创建各种类型的消息
    static NetworkMessage CreateDeviceListRequest();
    static NetworkMessage CreateDeviceListResponse(const std::vector<protocol::UsbipDeviceInfo>& devices);
    static NetworkMessage CreateDeviceImportRequest(const std::string& bus_id);
    static NetworkMessage CreateDeviceImportResponse(bool success, const std::string& error = "");
    static NetworkMessage CreateUrbSubmit(const protocol::UsbUrb& urb);
    static NetworkMessage CreateUrbResponse(const protocol::UsbUrb& urb);
    static NetworkMessage CreateDeviceDisconnect(const std::string& bus_id);
    static NetworkMessage CreateHeartbeat();

    // 获取下一个序列号
    static uint32_t GetNextSequence();

private:
    void ProcessCompleteMessage(const uint8_t* data, size_t len);
    uint32_t CalculateChecksum(const uint8_t* data, size_t len);
    bool ValidateMessage(const MessageHeader& header, const uint8_t* payload);

    std::vector<uint8_t> receive_buffer_;
    MessageCallback message_callback_;
    std::mutex mutex_;

    static uint32_t next_sequence_;
};

} // namespace network
} // namespace usb_redirector
