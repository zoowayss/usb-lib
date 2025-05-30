#include "tcp_socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace usb_redirector {
namespace network {

TcpSocket::TcpSocket()
    : socket_fd_(-1)
    , is_connected_(false)
    , is_listening_(false)
    , should_stop_(false) {
}

TcpSocket::~TcpSocket() {
    Close();
}

bool TcpSocket::Connect(const std::string& host, uint16_t port) {
    if (is_connected_.load()) {
        return false;
    }

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        NotifyError("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        NotifyError("Invalid address: " + host);
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        NotifyError("Failed to connect: " + std::string(strerror(errno)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    is_connected_.store(true);
    should_stop_.store(false);

    // 启动接收线程
    receive_thread_ = std::thread(&TcpSocket::ReceiveThread, this);

    NotifyConnect(true);
    return true;
}

bool TcpSocket::Listen(const std::string& bind_addr, uint16_t port) {
    if (is_listening_.load()) {
        return false;
    }

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        NotifyError("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }

    // 设置地址重用
    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        NotifyError("Failed to set socket options: " + std::string(strerror(errno)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (bind_addr.empty() || bind_addr == "0.0.0.0") {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, bind_addr.c_str(), &server_addr.sin_addr) <= 0) {
            NotifyError("Invalid bind address: " + bind_addr);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }

    if (bind(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        NotifyError("Failed to bind: " + std::string(strerror(errno)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (listen(socket_fd_, 5) < 0) {
        NotifyError("Failed to listen: " + std::string(strerror(errno)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    is_listening_.store(true);
    should_stop_.store(false);

    // 启动接受连接线程
    accept_thread_ = std::thread(&TcpSocket::AcceptThread, this);

    return true;
}

bool TcpSocket::Send(const uint8_t* data, size_t len) {
    if (!is_connected_.load() || socket_fd_ < 0) {
        return false;
    }

    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(socket_fd_, data + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            NotifyError("Send failed: " + std::string(strerror(errno)));
            return false;
        }
        total_sent += sent;
    }

    return true;
}

bool TcpSocket::Send(const std::vector<uint8_t>& data) {
    return Send(data.data(), data.size());
}

void TcpSocket::Close() {
    should_stop_.store(true);
    is_connected_.store(false);
    is_listening_.store(false);

    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }

    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int fd : client_fds_) {
            close(fd);
        }
        client_fds_.clear();
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    NotifyConnect(false);
}

void TcpSocket::ReceiveThread() {
    const size_t buffer_size = 8192;
    std::vector<uint8_t> buffer(buffer_size);

    while (!should_stop_.load() && is_connected_.load()) {
        ssize_t received = recv(socket_fd_, buffer.data(), buffer_size, 0);
        if (received > 0) {
            if (data_callback_) {
                data_callback_(buffer.data(), received);
            }
        } else if (received == 0) {
            // 连接关闭
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                NotifyError("Receive failed: " + std::string(strerror(errno)));
                break;
            }
        }
    }

    is_connected_.store(false);
    NotifyConnect(false);
}

void TcpSocket::AcceptThread() {
    while (!should_stop_.load() && is_listening_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(socket_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && !should_stop_.load()) {
                NotifyError("Accept failed: " + std::string(strerror(errno)));
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            client_fds_.push_back(client_fd);
        }

        // 为每个客户端启动处理线程
        std::thread client_thread(&TcpSocket::HandleClient, this, client_fd);
        client_thread.detach();
    }
}

void TcpSocket::HandleClient(int client_fd) {
    const size_t buffer_size = 8192;
    std::vector<uint8_t> buffer(buffer_size);

    while (!should_stop_.load()) {
        ssize_t received = recv(client_fd, buffer.data(), buffer_size, 0);
        if (received > 0) {
            if (data_callback_) {
                data_callback_(buffer.data(), received);
            }
        } else if (received == 0) {
            // 客户端关闭连接
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
    }

    close(client_fd);

    // 从客户端列表中移除
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
                         client_fds_.end());
    }
}

void TcpSocket::NotifyError(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

void TcpSocket::NotifyConnect(bool connected) {
    if (connect_callback_) {
        connect_callback_(connected);
    }
}

std::string TcpSocket::GetLocalAddress() const {
    if (socket_fd_ < 0) {
        return "";
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(socket_fd_, (struct sockaddr*)&addr, &len) < 0) {
        return "";
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}

std::string TcpSocket::GetRemoteAddress() const {
    if (socket_fd_ < 0) {
        return "";
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getpeername(socket_fd_, (struct sockaddr*)&addr, &len) < 0) {
        return "";
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace network
} // namespace usb_redirector
