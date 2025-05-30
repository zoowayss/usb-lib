#pragma once

#include "network/tcp_socket.h"
#include "network/message_handler.h"
#include "protocol/usbip_protocol.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>

namespace usb_redirector {
namespace receiver {

class UsbipClient {
public:
    using DeviceListCallback = std::function<void(const std::vector<protocol::UsbipDeviceInfo>& devices)>;
    using UrbCallback = std::function<void(const protocol::UsbUrb& urb)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    UsbipClient();
    ~UsbipClient();
    
    // 禁止拷贝
    UsbipClient(const UsbipClient&) = delete;
    UsbipClient& operator=(const UsbipClient&) = delete;
    
    // 设置回调函数
    void SetDeviceListCallback(DeviceListCallback callback) { device_list_callback_ = std::move(callback); }
    void SetUrbCallback(UrbCallback callback) { urb_callback_ = std::move(callback); }
    void SetErrorCallback(ErrorCallback callback) { error_callback_ = std::move(callback); }
    
    // 连接到发送端
    bool Connect(const std::string& host, uint16_t port = 3240);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }
    
    // USBIP操作
    bool RequestDeviceList();
    bool ImportDevice(const std::string& bus_id);
    bool SendUrbResponse(const protocol::UsbUrb& urb);
    
    // 心跳
    void StartHeartbeat(int interval_seconds = 30);
    void StopHeartbeat();

private:
    void OnNetworkMessage(const network::NetworkMessage& message);
    void OnNetworkError(const std::string& error);
    void OnNetworkConnect(bool connected);
    
    void HandleDeviceListResponse(const network::NetworkMessage& message);
    void HandleDeviceImportResponse(const network::NetworkMessage& message);
    void HandleUrbSubmit(const network::NetworkMessage& message);
    void HandleHeartbeat(const network::NetworkMessage& message);
    
    void HeartbeatThread();
    
    std::unique_ptr<network::TcpSocket> tcp_client_;
    std::unique_ptr<network::MessageHandler> message_handler_;
    
    DeviceListCallback device_list_callback_;
    UrbCallback urb_callback_;
    ErrorCallback error_callback_;
    
    std::atomic<bool> connected_;
    std::atomic<bool> heartbeat_running_;
    std::thread heartbeat_thread_;
    int heartbeat_interval_;
    
    std::string server_host_;
    uint16_t server_port_;
};

} // namespace receiver
} // namespace usb_redirector
