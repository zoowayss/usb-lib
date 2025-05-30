#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

#include "usb/usb_device_manager.h"
#include "usb/mass_storage_device.h"
#include "capture/urb_capture.h"
#include "network/tcp_socket.h"
#include "network/message_handler.h"
#include "utils/logger.h"

using namespace usb_redirector;

class UsbSender {
public:
    UsbSender() 
        : running_(false)
        , server_port_(3240) // USBIP默认端口
        , device_manager_(std::make_unique<sender::UsbDeviceManager>())
        , urb_capture_(std::make_unique<sender::UrbCapture>())
        , message_handler_(std::make_unique<network::MessageHandler>())
        , tcp_server_(std::make_unique<network::TcpSocket>()) {
    }
    
    ~UsbSender() {
        Stop();
    }
    
    bool Initialize() {
        // 初始化日志
        utils::Logger::Instance().SetLogLevel(utils::LogLevel::INFO);
        utils::Logger::Instance().SetConsoleOutput(true);
        
        LOG_INFO("Initializing USB Sender...");
        
        // 初始化USB设备管理器
        if (!device_manager_->Initialize()) {
            LOG_ERROR("Failed to initialize USB device manager");
            return false;
        }
        
        // 设置网络回调
        SetupNetworkCallbacks();
        
        // 设置URB捕获回调
        urb_capture_->SetUrbCallback([this](const protocol::UsbUrb& urb) {
            OnUrbCaptured(urb);
        });
        
        LOG_INFO("USB Sender initialized successfully");
        return true;
    }
    
    bool Start() {
        if (running_) {
            return true;
        }
        
        // 启动TCP服务器
        if (!tcp_server_->Listen("0.0.0.0", server_port_)) {
            LOG_ERROR("Failed to start TCP server on port " << server_port_);
            return false;
        }
        
        LOG_INFO("TCP server started on port " << server_port_);
        
        // 扫描大容量存储设备
        ScanMassStorageDevices();
        
        // 启动URB捕获
        if (!urb_capture_->StartCapture()) {
            LOG_ERROR("Failed to start URB capture");
            return false;
        }
        
        // 启动热插拔监控
        device_manager_->SetDeviceCallback([this](std::shared_ptr<sender::UsbDevice> device, bool connected) {
            OnDeviceHotplug(device, connected);
        });
        device_manager_->StartHotplugMonitoring();
        
        running_ = true;
        LOG_INFO("USB Sender started successfully");
        return true;
    }
    
    void Stop() {
        if (!running_) {
            return;
        }
        
        LOG_INFO("Stopping USB Sender...");
        
        running_ = false;
        
        // 停止URB捕获
        urb_capture_->StopCapture();
        
        // 停止热插拔监控
        device_manager_->StopHotplugMonitoring();
        
        // 关闭网络连接
        tcp_server_->Close();
        
        // 清理设备
        mass_storage_devices_.clear();
        
        LOG_INFO("USB Sender stopped");
    }
    
    void Run() {
        if (!running_) {
            LOG_ERROR("USB Sender not started");
            return;
        }
        
        LOG_INFO("USB Sender running... Press Ctrl+C to stop");
        
        // 主循环
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // 打印统计信息
            auto stats = urb_capture_->GetStatistics();
            if (stats.total_urbs > 0) {
                LOG_INFO("URB Stats - Total: " << stats.total_urbs 
                        << ", Control: " << stats.control_urbs
                        << ", Bulk: " << stats.bulk_urbs
                        << ", Bytes: " << stats.bytes_transferred
                        << ", Errors: " << stats.errors);
            }
        }
    }

private:
    void SetupNetworkCallbacks() {
        tcp_server_->SetConnectCallback([this](bool connected) {
            if (connected) {
                LOG_INFO("Client connected");
            } else {
                LOG_INFO("Client disconnected");
            }
        });
        
        tcp_server_->SetDataCallback([this](const uint8_t* data, size_t len) {
            message_handler_->ProcessReceivedData(data, len);
        });
        
        tcp_server_->SetErrorCallback([this](const std::string& error) {
            LOG_ERROR("Network error: " << error);
        });
        
        message_handler_->SetMessageCallback([this](const network::NetworkMessage& message) {
            OnNetworkMessage(message);
        });
    }
    
    void ScanMassStorageDevices() {
        auto devices = device_manager_->GetMassStorageDevices();
        
        for (auto& usb_device : devices) {
            auto mass_storage = std::make_shared<sender::MassStorageDevice>(usb_device);
            
            if (mass_storage->Initialize()) {
                mass_storage_devices_.push_back(mass_storage);
                urb_capture_->AddDevice(mass_storage);
                
                LOG_INFO("Added mass storage device: " << mass_storage->GetPath());
            } else {
                LOG_WARNING("Failed to initialize mass storage device: " << usb_device->GetPath());
            }
        }
        
        LOG_INFO("Found " << mass_storage_devices_.size() << " mass storage devices");
    }
    
    void OnDeviceHotplug(std::shared_ptr<sender::UsbDevice> device, bool connected) {
        if (connected && device) {
            LOG_INFO("Device connected: " << device->GetPath());
            
            // 检查是否是大容量存储设备
            const auto& desc = device->GetDeviceInfo().descriptor;
            if (desc.bDeviceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE)) {
                auto mass_storage = std::make_shared<sender::MassStorageDevice>(device);
                if (mass_storage->Initialize()) {
                    mass_storage_devices_.push_back(mass_storage);
                    urb_capture_->AddDevice(mass_storage);
                    LOG_INFO("Added new mass storage device: " << mass_storage->GetPath());
                }
            }
        } else {
            LOG_INFO("Device disconnected");
            // 这里需要实现设备移除逻辑
        }
    }
    
    void OnUrbCaptured(const protocol::UsbUrb& urb) {
        // 将URB转换为网络消息并发送
        sender::UrbProcessor processor;
        auto message = processor.CreateUsbipSubmit(urb);
        auto data = message_handler_->SerializeMessage(message);
        
        if (!tcp_server_->Send(data)) {
            LOG_WARNING("Failed to send URB data over network");
        }
    }
    
    void OnNetworkMessage(const network::NetworkMessage& message) {
        switch (static_cast<network::MessageType>(message.header.type)) {
            case network::MessageType::DEVICE_LIST_REQUEST:
                HandleDeviceListRequest();
                break;
                
            case network::MessageType::DEVICE_IMPORT_REQUEST:
                HandleDeviceImportRequest(message);
                break;
                
            case network::MessageType::HEARTBEAT:
                HandleHeartbeat();
                break;
                
            default:
                LOG_WARNING("Unknown message type: " << message.header.type);
                break;
        }
    }
    
    void HandleDeviceListRequest() {
        LOG_INFO("Received device list request");
        
        std::vector<protocol::UsbipDeviceInfo> device_list;
        
        for (const auto& device : mass_storage_devices_) {
            protocol::UsbipDeviceInfo info = {};
            
            std::string path = device->GetPath();
            std::string bus_id = device->GetBusId();
            
            strncpy(info.path, path.c_str(), sizeof(info.path) - 1);
            strncpy(info.busid, bus_id.c_str(), sizeof(info.busid) - 1);
            
            const auto& dev_info = device->GetDeviceInfo();
            info.busnum = dev_info.bus_number;
            info.devnum = dev_info.device_number;
            info.speed = static_cast<uint32_t>(dev_info.speed);
            info.idVendor = dev_info.descriptor.idVendor;
            info.idProduct = dev_info.descriptor.idProduct;
            info.bcdDevice = dev_info.descriptor.bcdDevice;
            info.bDeviceClass = dev_info.descriptor.bDeviceClass;
            info.bDeviceSubClass = dev_info.descriptor.bDeviceSubClass;
            info.bDeviceProtocol = dev_info.descriptor.bDeviceProtocol;
            info.bConfigurationValue = 1; // 假设使用配置1
            info.bNumConfigurations = dev_info.descriptor.bNumConfigurations;
            info.bNumInterfaces = 1; // 大容量存储设备通常只有一个接口
            
            device_list.push_back(info);
        }
        
        auto response = network::MessageHandler::CreateDeviceListResponse(device_list);
        auto data = message_handler_->SerializeMessage(response);
        tcp_server_->Send(data);
        
        LOG_INFO("Sent device list with " << device_list.size() << " devices");
    }
    
    void HandleDeviceImportRequest(const network::NetworkMessage& message) {
        std::string bus_id(message.payload.begin(), message.payload.end());
        LOG_INFO("Received device import request for: " << bus_id);
        
        // 查找对应的设备
        bool found = false;
        for (const auto& device : mass_storage_devices_) {
            if (device->GetBusId() == bus_id) {
                found = true;
                break;
            }
        }
        
        auto response = network::MessageHandler::CreateDeviceImportResponse(found, 
            found ? "" : "Device not found");
        auto data = message_handler_->SerializeMessage(response);
        tcp_server_->Send(data);
        
        LOG_INFO("Device import " << (found ? "successful" : "failed") << " for: " << bus_id);
    }
    
    void HandleHeartbeat() {
        auto response = network::MessageHandler::CreateHeartbeat();
        auto data = message_handler_->SerializeMessage(response);
        tcp_server_->Send(data);
    }

private:
    bool running_;
    uint16_t server_port_;
    
    std::unique_ptr<sender::UsbDeviceManager> device_manager_;
    std::unique_ptr<sender::UrbCapture> urb_capture_;
    std::unique_ptr<network::MessageHandler> message_handler_;
    std::unique_ptr<network::TcpSocket> tcp_server_;
    
    std::vector<std::shared_ptr<sender::MassStorageDevice>> mass_storage_devices_;
};

// 全局变量用于信号处理
static std::unique_ptr<UsbSender> g_sender;

void SignalHandler(int signal) {
    LOG_INFO("Received signal " << signal << ", shutting down...");
    if (g_sender) {
        g_sender->Stop();
    }
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    try {
        g_sender = std::make_unique<UsbSender>();
        
        if (!g_sender->Initialize()) {
            LOG_ERROR("Failed to initialize USB Sender");
            return 1;
        }
        
        if (!g_sender->Start()) {
            LOG_ERROR("Failed to start USB Sender");
            return 1;
        }
        
        g_sender->Run();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception: " << e.what());
        return 1;
    }
    
    LOG_INFO("USB Sender exited normally");
    return 0;
}
