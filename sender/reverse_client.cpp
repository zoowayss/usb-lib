#include "reverse_client.h"
#include "utils/logger.h"
#include <chrono>
#include <thread>

namespace usb_redirector {
namespace sender {

ReverseClient::ReverseClient()
    : tcp_client_(std::make_unique<network::TcpSocket>())
    , message_handler_(std::make_unique<network::MessageHandler>())
    , connected_(false)
    , auto_reconnect_(false)
    , should_stop_(false)
    , server_port_(3240)
    , reconnect_interval_(5) {
    
    // 设置网络回调
    tcp_client_->SetConnectCallback([this](bool connected) {
        OnNetworkConnect(connected);
    });
    
    tcp_client_->SetDataCallback([this](const uint8_t* data, size_t len) {
        OnNetworkData(data, len);
    });
    
    tcp_client_->SetErrorCallback([this](const std::string& error) {
        OnNetworkError(error);
    });
    
    // 设置消息处理回调
    message_handler_->SetMessageCallback([this](const network::NetworkMessage& message) {
        OnNetworkMessage(message);
    });
}

ReverseClient::~ReverseClient() {
    Disconnect();
}

bool ReverseClient::ConnectToServer(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_.load()) {
        LOG_WARNING("Already connected to server");
        return true;
    }
    
    server_host_ = host;
    server_port_ = port;
    
    LOG_INFO("Connecting to Linux server: " << host << ":" << port);
    
    if (!tcp_client_->Connect(host, port)) {
        LOG_ERROR("Failed to connect to Linux server");
        return false;
    }
    
    // 等待连接确认
    for (int i = 0; i < 50; ++i) { // 最多等待5秒
        if (connected_.load()) {
            LOG_INFO("Connected to Linux server successfully");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_ERROR("Connection timeout");
    tcp_client_->Close();
    return false;
}

void ReverseClient::Disconnect() {
    should_stop_.store(true);
    auto_reconnect_.store(false);
    
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
    
    if (connected_.load()) {
        LOG_INFO("Disconnecting from Linux server");
        tcp_client_->Close();
        connected_.store(false);
    }
}

bool ReverseClient::SendMessage(const network::NetworkMessage& message) {
    if (!connected_.load()) {
        LOG_ERROR("Not connected to server");
        return false;
    }
    
    auto data = message_handler_->SerializeMessage(message);
    return tcp_client_->Send(data);
}

void ReverseClient::EnableAutoReconnect(bool enable, int interval_seconds) {
    auto_reconnect_.store(enable);
    reconnect_interval_ = interval_seconds;
    
    if (enable && !reconnect_thread_.joinable()) {
        should_stop_.store(false);
        reconnect_thread_ = std::thread(&ReverseClient::ReconnectThread, this);
        LOG_INFO("Auto-reconnect enabled with interval: " << interval_seconds << " seconds");
    } else if (!enable) {
        LOG_INFO("Auto-reconnect disabled");
    }
}

void ReverseClient::OnNetworkConnect(bool connected) {
    connected_.store(connected);
    
    if (connected) {
        LOG_INFO("Network connection established with Linux server");
    } else {
        LOG_INFO("Network connection lost");
    }
    
    if (connect_callback_) {
        connect_callback_(connected);
    }
}

void ReverseClient::OnNetworkData(const uint8_t* data, size_t len) {
    message_handler_->ProcessReceivedData(data, len);
}

void ReverseClient::OnNetworkError(const std::string& error) {
    LOG_ERROR("Network error: " << error);
    connected_.store(false);
}

void ReverseClient::OnNetworkMessage(const network::NetworkMessage& message) {
    if (message_callback_) {
        message_callback_(message);
    }
}

void ReverseClient::ReconnectThread() {
    LOG_INFO("Auto-reconnect thread started");
    
    while (!should_stop_.load() && auto_reconnect_.load()) {
        if (!connected_.load()) {
            LOG_INFO("Attempting to reconnect to Linux server...");
            
            if (tcp_client_->Connect(server_host_, server_port_)) {
                // 连接成功，等待确认
                for (int i = 0; i < 30 && !connected_.load() && !should_stop_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                if (connected_.load()) {
                    LOG_INFO("Reconnected successfully");
                } else {
                    LOG_WARNING("Reconnection failed - no confirmation");
                    tcp_client_->Close();
                }
            } else {
                LOG_WARNING("Reconnection attempt failed");
            }
        }
        
        // 等待重连间隔
        for (int i = 0; i < reconnect_interval_ && !should_stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Auto-reconnect thread stopped");
}

} // namespace sender
} // namespace usb_redirector
