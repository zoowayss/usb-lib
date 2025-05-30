#pragma once

#include "network/tcp_socket.h"
#include "network/message_handler.h"
#include <string>
#include <memory>
#include <atomic>
#include <thread>

namespace usb_redirector {
namespace sender {

class ReverseClient {
public:
    using ConnectCallback = std::function<void(bool connected)>;
    using MessageCallback = std::function<void(const network::NetworkMessage& message)>;
    
    ReverseClient();
    ~ReverseClient();
    
    // 禁止拷贝
    ReverseClient(const ReverseClient&) = delete;
    ReverseClient& operator=(const ReverseClient&) = delete;
    
    // 设置回调
    void SetConnectCallback(ConnectCallback callback) { connect_callback_ = std::move(callback); }
    void SetMessageCallback(MessageCallback callback) { message_callback_ = std::move(callback); }
    
    // 连接到Linux服务器
    bool ConnectToServer(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }
    
    // 发送消息
    bool SendMessage(const network::NetworkMessage& message);
    
    // 自动重连
    void EnableAutoReconnect(bool enable, int interval_seconds = 5);

private:
    void OnNetworkConnect(bool connected);
    void OnNetworkData(const uint8_t* data, size_t len);
    void OnNetworkError(const std::string& error);
    void OnNetworkMessage(const network::NetworkMessage& message);
    
    void ReconnectThread();
    
    std::unique_ptr<network::TcpSocket> tcp_client_;
    std::unique_ptr<network::MessageHandler> message_handler_;
    
    ConnectCallback connect_callback_;
    MessageCallback message_callback_;
    
    std::atomic<bool> connected_;
    std::atomic<bool> auto_reconnect_;
    std::atomic<bool> should_stop_;
    
    std::string server_host_;
    uint16_t server_port_;
    int reconnect_interval_;
    
    std::thread reconnect_thread_;
    mutable std::mutex mutex_;
};

} // namespace sender
} // namespace usb_redirector
