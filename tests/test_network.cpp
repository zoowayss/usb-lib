#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>
#include "network/tcp_socket.h"
#include "network/message_handler.h"
#include "utils/logger.h"

using namespace usb_redirector;

void TestTcpSocket() {
    std::cout << "Testing TCP Socket..." << std::endl;
    
    std::atomic<bool> server_received = false;
    std::atomic<bool> client_received = false;
    std::vector<uint8_t> received_data;
    
    // 创建服务器
    network::TcpSocket server;
    server.SetDataCallback([&](const uint8_t* data, size_t len) {
        received_data.assign(data, data + len);
        server_received = true;
    });
    
    // 启动服务器
    assert(server.Listen("127.0.0.1", 12345));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建客户端
    network::TcpSocket client;
    client.SetDataCallback([&](const uint8_t* data, size_t len) {
        client_received = true;
    });
    
    // 连接到服务器
    assert(client.Connect("127.0.0.1", 12345));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 发送测试数据
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    assert(client.Send(test_data));
    
    // 等待数据接收
    for (int i = 0; i < 50 && !server_received; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    assert(server_received);
    assert(received_data == test_data);
    
    client.Close();
    server.Close();
    
    std::cout << "TCP Socket: PASSED" << std::endl;
}

void TestMessageHandler() {
    std::cout << "Testing Message Handler..." << std::endl;
    
    network::MessageHandler handler;
    std::atomic<bool> message_received = false;
    network::NetworkMessage received_message;
    
    handler.SetMessageCallback([&](const network::NetworkMessage& message) {
        received_message = message;
        message_received = true;
    });
    
    // 创建测试消息
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    auto message = network::NetworkMessage(network::MessageType::HEARTBEAT, payload);
    
    // 序列化消息
    auto serialized = handler.SerializeMessage(message);
    assert(!serialized.empty());
    
    // 处理序列化的数据
    handler.ProcessReceivedData(serialized.data(), serialized.size());
    
    // 等待消息处理
    for (int i = 0; i < 50 && !message_received; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    assert(message_received);
    assert(received_message.header.type == static_cast<uint32_t>(network::MessageType::HEARTBEAT));
    assert(received_message.payload == payload);
    
    std::cout << "Message Handler: PASSED" << std::endl;
}

void TestMessageTypes() {
    std::cout << "Testing Message Types..." << std::endl;
    
    // 测试设备列表请求
    auto device_list_req = network::MessageHandler::CreateDeviceListRequest();
    assert(device_list_req.header.type == static_cast<uint32_t>(network::MessageType::DEVICE_LIST_REQUEST));
    assert(device_list_req.payload.empty());
    
    // 测试设备导入请求
    std::string bus_id = "1-2";
    auto import_req = network::MessageHandler::CreateDeviceImportRequest(bus_id);
    assert(import_req.header.type == static_cast<uint32_t>(network::MessageType::DEVICE_IMPORT_REQUEST));
    assert(import_req.payload.size() == bus_id.length());
    
    // 测试设备导入响应
    auto import_resp = network::MessageHandler::CreateDeviceImportResponse(true);
    assert(import_resp.header.type == static_cast<uint32_t>(network::MessageType::DEVICE_IMPORT_RESPONSE));
    assert(!import_resp.payload.empty());
    assert(import_resp.payload[0] == 1); // 成功
    
    // 测试心跳
    auto heartbeat = network::MessageHandler::CreateHeartbeat();
    assert(heartbeat.header.type == static_cast<uint32_t>(network::MessageType::HEARTBEAT));
    assert(heartbeat.payload.empty());
    
    std::cout << "Message Types: PASSED" << std::endl;
}

void TestNetworkIntegration() {
    std::cout << "Testing Network Integration..." << std::endl;
    
    std::atomic<bool> message_received = false;
    network::NetworkMessage received_message;
    
    // 服务器端
    network::TcpSocket server;
    network::MessageHandler server_handler;
    
    server_handler.SetMessageCallback([&](const network::NetworkMessage& message) {
        received_message = message;
        message_received = true;
    });
    
    server.SetDataCallback([&](const uint8_t* data, size_t len) {
        server_handler.ProcessReceivedData(data, len);
    });
    
    assert(server.Listen("127.0.0.1", 12346));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 客户端
    network::TcpSocket client;
    network::MessageHandler client_handler;
    
    assert(client.Connect("127.0.0.1", 12346));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 发送心跳消息
    auto heartbeat = network::MessageHandler::CreateHeartbeat();
    auto serialized = client_handler.SerializeMessage(heartbeat);
    assert(client.Send(serialized));
    
    // 等待消息接收
    for (int i = 0; i < 100 && !message_received; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    assert(message_received);
    assert(received_message.header.type == static_cast<uint32_t>(network::MessageType::HEARTBEAT));
    
    client.Close();
    server.Close();
    
    std::cout << "Network Integration: PASSED" << std::endl;
}

int main() {
    // 初始化日志
    utils::Logger::Instance().SetLogLevel(utils::LogLevel::WARNING); // 减少测试时的日志输出
    utils::Logger::Instance().SetConsoleOutput(true);
    
    std::cout << "=== USB Redirector Network Tests ===" << std::endl;
    
    try {
        TestTcpSocket();
        TestMessageHandler();
        TestMessageTypes();
        TestNetworkIntegration();
        
        std::cout << "\nAll network tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
