#include "usbip_client.h"
#include "utils/logger.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <arpa/inet.h>

namespace usb_redirector {
namespace receiver {

UsbipClient::UsbipClient()
    : tcp_client_(std::make_unique<network::TcpSocket>())
    , message_handler_(std::make_unique<network::MessageHandler>())
    , connected_(false)
    , heartbeat_running_(false)
    , heartbeat_interval_(30)
    , server_port_(3240) {

    // 设置网络回调
    tcp_client_->SetConnectCallback([this](bool connected) {
        OnNetworkConnect(connected);
    });

    tcp_client_->SetDataCallback([this](const uint8_t* data, size_t len) {
        message_handler_->ProcessReceivedData(data, len);
    });

    tcp_client_->SetErrorCallback([this](const std::string& error) {
        OnNetworkError(error);
    });

    // 设置消息处理回调
    message_handler_->SetMessageCallback([this](const network::NetworkMessage& message) {
        OnNetworkMessage(message);
    });
}

UsbipClient::~UsbipClient() {
    Disconnect();
}

bool UsbipClient::Connect(const std::string& host, uint16_t port) {
    if (connected_.load()) {
        LOG_WARNING("Already connected to USBIP server");
        return true;
    }

    server_host_ = host;
    server_port_ = port;

    LOG_INFO("Connecting to USBIP server: " << host << ":" << port);

    if (!tcp_client_->Connect(host, port)) {
        LOG_ERROR("Failed to connect to USBIP server");
        return false;
    }

    // 等待连接确认
    for (int i = 0; i < 50; ++i) { // 最多等待5秒
        if (connected_.load()) {
            LOG_INFO("Connected to USBIP server successfully");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_ERROR("Connection timeout");
    tcp_client_->Close();
    return false;
}

void UsbipClient::Disconnect() {
    if (!connected_.load()) {
        return;
    }

    LOG_INFO("Disconnecting from USBIP server");

    StopHeartbeat();
    tcp_client_->Close();
    connected_.store(false);

    LOG_INFO("Disconnected from USBIP server");
}

bool UsbipClient::RequestDeviceList() {
    if (!connected_.load()) {
        LOG_ERROR("Not connected to USBIP server");
        return false;
    }

    LOG_INFO("Requesting device list from server");

    auto message = network::MessageHandler::CreateDeviceListRequest();
    auto data = message_handler_->SerializeMessage(message);

    return tcp_client_->Send(data);
}

bool UsbipClient::ImportDevice(const std::string& bus_id) {
    if (!connected_.load()) {
        LOG_ERROR("Not connected to USBIP server");
        return false;
    }

    LOG_INFO("Importing device: " << bus_id);

    auto message = network::MessageHandler::CreateDeviceImportRequest(bus_id);
    auto data = message_handler_->SerializeMessage(message);

    return tcp_client_->Send(data);
}

bool UsbipClient::SendUrbResponse(const protocol::UsbUrb& urb) {
    if (!connected_.load()) {
        LOG_ERROR("Not connected to USBIP server");
        return false;
    }

    auto message = network::MessageHandler::CreateUrbResponse(urb);
    auto data = message_handler_->SerializeMessage(message);

    return tcp_client_->Send(data);
}

void UsbipClient::StartHeartbeat(int interval_seconds) {
    if (heartbeat_running_.load()) {
        return;
    }

    heartbeat_interval_ = interval_seconds;
    heartbeat_running_.store(true);
    heartbeat_thread_ = std::thread(&UsbipClient::HeartbeatThread, this);

    LOG_INFO("Heartbeat started with interval: " << interval_seconds << " seconds");
}

void UsbipClient::StopHeartbeat() {
    if (!heartbeat_running_.load()) {
        return;
    }

    heartbeat_running_.store(false);

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    LOG_INFO("Heartbeat stopped");
}

void UsbipClient::OnNetworkMessage(const network::NetworkMessage& message) {
    switch (static_cast<network::MessageType>(message.header.type)) {
        case network::MessageType::DEVICE_LIST_RESPONSE:
            HandleDeviceListResponse(message);
            break;

        case network::MessageType::DEVICE_IMPORT_RESPONSE:
            HandleDeviceImportResponse(message);
            break;

        case network::MessageType::URB_SUBMIT:
            HandleUrbSubmit(message);
            break;

        case network::MessageType::HEARTBEAT:
            HandleHeartbeat(message);
            break;

        default:
            LOG_WARNING("Unknown message type: " << message.header.type);
            break;
    }
}

void UsbipClient::OnNetworkError(const std::string& error) {
    LOG_ERROR("Network error: " << error);

    if (error_callback_) {
        error_callback_(error);
    }

    // 网络错误时断开连接
    connected_.store(false);
}

void UsbipClient::OnNetworkConnect(bool connected) {
    connected_.store(connected);

    if (connected) {
        LOG_INFO("Network connection established");
        StartHeartbeat();
    } else {
        LOG_INFO("Network connection lost");
        StopHeartbeat();
    }
}

void UsbipClient::HandleDeviceListResponse(const network::NetworkMessage& message) {
    LOG_INFO("Received device list response");

    // 解析设备列表
    std::vector<protocol::UsbipDeviceInfo> devices;

    if (message.payload.size() >= 12) { // 至少包含头部信息
        const uint8_t* data = message.payload.data();
        size_t offset = 0;

        // 跳过操作码和状态
        offset += 8;

        // 读取设备数量
        uint32_t num_devices = 0;
        if (offset + 4 <= message.payload.size()) {
            std::memcpy(&num_devices, data + offset, 4);
            num_devices = ntohl(num_devices);
            offset += 4;

            LOG_INFO("Device list contains " << num_devices << " devices");

            // 读取设备信息
            for (uint32_t i = 0; i < num_devices && offset + sizeof(protocol::UsbipDeviceInfo) <= message.payload.size(); ++i) {
                protocol::UsbipDeviceInfo device_info;
                std::memcpy(&device_info, data + offset, sizeof(protocol::UsbipDeviceInfo));

                // 转换字节序
                device_info.busnum = ntohl(device_info.busnum);
                device_info.devnum = ntohl(device_info.devnum);
                device_info.speed = ntohl(device_info.speed);
                device_info.idVendor = ntohs(device_info.idVendor);
                device_info.idProduct = ntohs(device_info.idProduct);
                device_info.bcdDevice = ntohs(device_info.bcdDevice);

                devices.push_back(device_info);
                offset += sizeof(protocol::UsbipDeviceInfo);

                LOG_INFO("Device " << i << ": " << device_info.path
                        << " (VID:PID = " << std::hex << device_info.idVendor
                        << ":" << device_info.idProduct << std::dec << ")");
            }
        }
    }

    if (device_list_callback_) {
        device_list_callback_(devices);
    }
}

void UsbipClient::HandleDeviceImportResponse(const network::NetworkMessage& message) {
    bool success = false;
    std::string error_msg;

    if (!message.payload.empty()) {
        success = message.payload[0] != 0;

        if (!success && message.payload.size() > 1) {
            error_msg = std::string(message.payload.begin() + 1, message.payload.end());
        }
    }

    if (success) {
        LOG_INFO("Device import successful");
    } else {
        LOG_ERROR("Device import failed: " << error_msg);
    }
}

void UsbipClient::HandleUrbSubmit(const network::NetworkMessage& message) {
    LOG_DEBUG("Received URB submit");

    // 解析USBIP数据包
    if (message.payload.size() < sizeof(protocol::UsbipCmdSubmit)) {
        LOG_ERROR("Invalid URB submit message size");
        return;
    }

    protocol::UsbipCmdSubmit cmd_submit;
    if (!protocol::UsbipProtocol::ParseCmdSubmit(message.payload.data(), message.payload.size(), cmd_submit)) {
        LOG_ERROR("Failed to parse URB submit command");
        return;
    }

    // 转换为内部URB格式
    protocol::UsbUrb urb;
    urb.id = cmd_submit.header.seqnum;
    urb.endpoint = static_cast<uint8_t>(cmd_submit.header.ep);
    urb.direction = static_cast<protocol::UsbDirection>(cmd_submit.header.direction);
    urb.flags = cmd_submit.transfer_flags;

    // 根据端点地址确定传输类型
    if (urb.endpoint == 0) {
        urb.type = protocol::UsbTransferType::CONTROL;
        std::memcpy(&urb.setup, &cmd_submit.setup, sizeof(urb.setup));
    } else {
        urb.type = protocol::UsbTransferType::BULK; // 假设是批量传输
    }

    // 提取数据
    if (cmd_submit.transfer_buffer_length > 0) {
        size_t data_offset = sizeof(protocol::UsbipCmdSubmit);
        if (message.payload.size() >= data_offset + cmd_submit.transfer_buffer_length) {
            urb.data.assign(
                message.payload.begin() + data_offset,
                message.payload.begin() + data_offset + cmd_submit.transfer_buffer_length
            );
        }
    }

    urb.status = 0;
    urb.actual_length = static_cast<uint32_t>(urb.data.size());
    urb.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (urb_callback_) {
        urb_callback_(urb);
    }
}

void UsbipClient::HandleHeartbeat(const network::NetworkMessage& message) {
    LOG_DEBUG("Received heartbeat from server");

    // 回复心跳
    auto response = network::MessageHandler::CreateHeartbeat();
    auto data = message_handler_->SerializeMessage(response);
    tcp_client_->Send(data);
}

void UsbipClient::HeartbeatThread() {
    LOG_INFO("Heartbeat thread started");

    while (heartbeat_running_.load() && connected_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(heartbeat_interval_));

        if (!heartbeat_running_.load() || !connected_.load()) {
            break;
        }

        // 发送心跳
        auto heartbeat = network::MessageHandler::CreateHeartbeat();
        auto data = message_handler_->SerializeMessage(heartbeat);

        if (!tcp_client_->Send(data)) {
            LOG_WARNING("Failed to send heartbeat");
        } else {
            LOG_DEBUG("Heartbeat sent");
        }
    }

    LOG_INFO("Heartbeat thread stopped");
}

} // namespace receiver
} // namespace usb_redirector
