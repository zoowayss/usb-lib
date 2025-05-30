#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace usb_redirector {
namespace network {

class TcpSocket {
public:
    using DataCallback = std::function<void(const uint8_t* data, size_t len)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    using ConnectCallback = std::function<void(bool connected)>;

    TcpSocket();
    virtual ~TcpSocket();

    // 禁止拷贝
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // 设置回调函数
    void SetDataCallback(DataCallback callback) { data_callback_ = std::move(callback); }
    void SetErrorCallback(ErrorCallback callback) { error_callback_ = std::move(callback); }
    void SetConnectCallback(ConnectCallback callback) { connect_callback_ = std::move(callback); }

    // 连接到服务器
    bool Connect(const std::string& host, uint16_t port);
    
    // 启动服务器监听
    bool Listen(const std::string& bind_addr, uint16_t port);
    
    // 发送数据
    bool Send(const uint8_t* data, size_t len);
    bool Send(const std::vector<uint8_t>& data);
    
    // 关闭连接
    void Close();
    
    // 检查连接状态
    bool IsConnected() const { return is_connected_.load(); }
    
    // 获取本地和远程地址
    std::string GetLocalAddress() const;
    std::string GetRemoteAddress() const;

private:
    void ReceiveThread();
    void AcceptThread();
    void HandleClient(int client_fd);
    void NotifyError(const std::string& error);
    void NotifyConnect(bool connected);

    int socket_fd_;
    std::atomic<bool> is_connected_;
    std::atomic<bool> is_listening_;
    std::atomic<bool> should_stop_;
    
    std::thread receive_thread_;
    std::thread accept_thread_;
    
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    ConnectCallback connect_callback_;
    
    mutable std::mutex mutex_;
    std::vector<int> client_fds_;  // 用于服务器模式的客户端连接
};

class TcpServer {
public:
    using ClientConnectCallback = std::function<void(std::shared_ptr<TcpSocket> client)>;
    
    TcpServer();
    ~TcpServer();
    
    // 禁止拷贝
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    
    // 设置客户端连接回调
    void SetClientConnectCallback(ClientConnectCallback callback) { 
        client_connect_callback_ = std::move(callback); 
    }
    
    // 启动服务器
    bool Start(const std::string& bind_addr, uint16_t port);
    
    // 停止服务器
    void Stop();
    
    // 检查服务器状态
    bool IsRunning() const { return is_running_.load(); }
    
    // 获取连接的客户端数量
    size_t GetClientCount() const;

private:
    void AcceptThread();
    void HandleClient(int client_fd);
    
    int server_fd_;
    std::atomic<bool> is_running_;
    std::atomic<bool> should_stop_;
    
    std::thread accept_thread_;
    ClientConnectCallback client_connect_callback_;
    
    mutable std::mutex clients_mutex_;
    std::vector<std::shared_ptr<TcpSocket>> clients_;
};

} // namespace network
} // namespace usb_redirector
