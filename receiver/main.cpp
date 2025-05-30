#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

#include "usbip/usbip_client.h"
#include "virtual_device/virtual_usb_device.h"
#include "utils/logger.h"

using namespace usb_redirector;

class UsbReceiver {
public:
    UsbReceiver() 
        : running_(false)
        , server_host_("127.0.0.1")
        , server_port_(3240)
        , usbip_client_(std::make_unique<receiver::UsbipClient>())
        , usbip_manager_(receiver::UsbipManager::Instance()) {
    }
    
    ~UsbReceiver() {
        Stop();
    }
    
    bool Initialize() {
        // 初始化日志
        utils::Logger::Instance().SetLogLevel(utils::LogLevel::INFO);
        utils::Logger::Instance().SetConsoleOutput(true);
        
        LOG_INFO("Initializing USB Receiver...");
        
        // 初始化USBIP管理器
        if (!usbip_manager_.Initialize()) {
            LOG_ERROR("Failed to initialize USBIP manager");
            return false;
        }
        
        // 设置USBIP客户端回调
        SetupUsbipCallbacks();
        
        LOG_INFO("USB Receiver initialized successfully");
        return true;
    }
    
    bool Start(const std::string& host = "", uint16_t port = 0) {
        if (running_) {
            return true;
        }
        
        if (!host.empty()) {
            server_host_ = host;
        }
        if (port > 0) {
            server_port_ = port;
        }
        
        LOG_INFO("Starting USB Receiver...");
        
        // 连接到发送端
        if (!usbip_client_->Connect(server_host_, server_port_)) {
            LOG_ERROR("Failed to connect to USB sender at " << server_host_ << ":" << server_port_);
            return false;
        }
        
        LOG_INFO("Connected to USB sender successfully");
        
        // 请求设备列表
        if (!usbip_client_->RequestDeviceList()) {
            LOG_ERROR("Failed to request device list");
            return false;
        }
        
        running_ = true;
        LOG_INFO("USB Receiver started successfully");
        return true;
    }
    
    void Stop() {
        if (!running_) {
            return;
        }
        
        LOG_INFO("Stopping USB Receiver...");
        
        running_ = false;
        
        // 断开所有虚拟设备
        for (auto& device : virtual_devices_) {
            device->DetachDevice();
        }
        virtual_devices_.clear();
        
        // 断开USBIP连接
        usbip_client_->Disconnect();
        
        // 清理USBIP管理器
        usbip_manager_.Cleanup();
        
        LOG_INFO("USB Receiver stopped");
    }
    
    void Run() {
        if (!running_) {
            LOG_ERROR("USB Receiver not started");
            return;
        }
        
        LOG_INFO("USB Receiver running... Press Ctrl+C to stop");
        
        // 主循环
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // 检查连接状态
            if (!usbip_client_->IsConnected()) {
                LOG_WARNING("Connection lost, attempting to reconnect...");
                
                if (usbip_client_->Connect(server_host_, server_port_)) {
                    LOG_INFO("Reconnected successfully");
                    usbip_client_->RequestDeviceList();
                } else {
                    LOG_ERROR("Reconnection failed");
                }
            }
        }
    }
    
    // 手动导入设备
    bool ImportDevice(const std::string& bus_id) {
        if (!usbip_client_->IsConnected()) {
            LOG_ERROR("Not connected to USB sender");
            return false;
        }
        
        return usbip_client_->ImportDevice(bus_id);
    }
    
    // 列出可用设备
    void ListDevices() {
        if (!usbip_client_->IsConnected()) {
            LOG_ERROR("Not connected to USB sender");
            return;
        }
        
        usbip_client_->RequestDeviceList();
    }

private:
    void SetupUsbipCallbacks() {
        usbip_client_->SetDeviceListCallback([this](const std::vector<protocol::UsbipDeviceInfo>& devices) {
            OnDeviceListReceived(devices);
        });
        
        usbip_client_->SetUrbCallback([this](const protocol::UsbUrb& urb) {
            OnUrbReceived(urb);
        });
        
        usbip_client_->SetErrorCallback([this](const std::string& error) {
            OnUsbipError(error);
        });
    }
    
    void OnDeviceListReceived(const std::vector<protocol::UsbipDeviceInfo>& devices) {
        LOG_INFO("Received device list with " << devices.size() << " devices:");
        
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& device = devices[i];
            
            LOG_INFO("Device " << i << ":");
            LOG_INFO("  Path: " << device.path);
            LOG_INFO("  Bus ID: " << device.busid);
            LOG_INFO("  VID:PID: " << std::hex << device.idVendor << ":" << device.idProduct << std::dec);
            LOG_INFO("  Class: " << static_cast<int>(device.bDeviceClass));
            LOG_INFO("  Speed: " << device.speed);
            
            // 自动导入大容量存储设备
            if (device.bDeviceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE)) {
                LOG_INFO("Auto-importing mass storage device: " << device.busid);
                ImportDeviceInternal(device);
            }
        }
    }
    
    void OnUrbReceived(const protocol::UsbUrb& urb) {
        LOG_DEBUG("Received URB: ID=" << urb.id 
                 << ", Type=" << static_cast<int>(urb.type)
                 << ", Endpoint=" << static_cast<int>(urb.endpoint)
                 << ", Length=" << urb.data.size());
        
        // 查找对应的虚拟设备并处理URB
        for (auto& device : virtual_devices_) {
            device->ProcessUrb(urb);
            break; // 简化：只发送给第一个设备
        }
    }
    
    void OnUsbipError(const std::string& error) {
        LOG_ERROR("USBIP error: " << error);
        
        // 错误处理：可能需要重连或清理设备
        if (error.find("connection") != std::string::npos) {
            LOG_INFO("Connection error detected, will attempt reconnection");
        }
    }
    
    bool ImportDeviceInternal(const protocol::UsbipDeviceInfo& device_info) {
        // 创建虚拟USB设备
        auto virtual_device = std::make_shared<receiver::VirtualUsbDevice>();
        
        // 设置URB响应回调
        virtual_device->SetUrbResponseCallback([this](const protocol::UsbUrb& urb) {
            OnUrbResponse(urb);
        });
        
        // 创建并附加设备
        if (!virtual_device->CreateDevice(device_info)) {
            LOG_ERROR("Failed to create virtual device");
            return false;
        }
        
        if (!virtual_device->AttachDevice()) {
            LOG_ERROR("Failed to attach virtual device");
            virtual_device->DestroyDevice();
            return false;
        }
        
        virtual_devices_.push_back(virtual_device);
        
        // 通知发送端导入设备
        if (!usbip_client_->ImportDevice(device_info.busid)) {
            LOG_ERROR("Failed to import device on sender side");
            virtual_device->DetachDevice();
            virtual_device->DestroyDevice();
            virtual_devices_.pop_back();
            return false;
        }
        
        LOG_INFO("Successfully imported device: " << device_info.busid);
        LOG_INFO("Virtual device path: " << virtual_device->GetDevicePath());
        return true;
    }
    
    void OnUrbResponse(const protocol::UsbUrb& urb) {
        LOG_DEBUG("Sending URB response: ID=" << urb.id 
                 << ", Status=" << urb.status
                 << ", Length=" << urb.actual_length);
        
        // 将URB响应发送回发送端
        if (!usbip_client_->SendUrbResponse(urb)) {
            LOG_WARNING("Failed to send URB response");
        }
    }

private:
    bool running_;
    std::string server_host_;
    uint16_t server_port_;
    
    std::unique_ptr<receiver::UsbipClient> usbip_client_;
    receiver::UsbipManager& usbip_manager_;
    
    std::vector<std::shared_ptr<receiver::VirtualUsbDevice>> virtual_devices_;
};

// 全局变量用于信号处理
static std::unique_ptr<UsbReceiver> g_receiver;

void SignalHandler(int signal) {
    LOG_INFO("Received signal " << signal << ", shutting down...");
    if (g_receiver) {
        g_receiver->Stop();
    }
}

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -h, --host <host>     USB sender host (default: 127.0.0.1)\n"
              << "  -p, --port <port>     USB sender port (default: 3240)\n"
              << "  -l, --list            List available devices and exit\n"
              << "  -i, --import <bus_id> Import specific device by bus ID\n"
              << "  --help                Show this help message\n";
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    std::string host;
    uint16_t port = 0;
    bool list_only = false;
    std::string import_device;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "-h" || arg == "--host") {
            if (i + 1 < argc) {
                host = argv[++i];
            } else {
                std::cerr << "Error: --host requires an argument\n";
                return 1;
            }
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } else {
                std::cerr << "Error: --port requires an argument\n";
                return 1;
            }
        } else if (arg == "-l" || arg == "--list") {
            list_only = true;
        } else if (arg == "-i" || arg == "--import") {
            if (i + 1 < argc) {
                import_device = argv[++i];
            } else {
                std::cerr << "Error: --import requires an argument\n";
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    try {
        g_receiver = std::make_unique<UsbReceiver>();
        
        if (!g_receiver->Initialize()) {
            LOG_ERROR("Failed to initialize USB Receiver");
            return 1;
        }
        
        if (!g_receiver->Start(host, port)) {
            LOG_ERROR("Failed to start USB Receiver");
            return 1;
        }
        
        if (list_only) {
            // 只列出设备
            g_receiver->ListDevices();
            std::this_thread::sleep_for(std::chrono::seconds(2)); // 等待响应
        } else if (!import_device.empty()) {
            // 导入特定设备
            if (g_receiver->ImportDevice(import_device)) {
                LOG_INFO("Device imported successfully");
            } else {
                LOG_ERROR("Failed to import device");
                return 1;
            }
        } else {
            // 正常运行模式
            g_receiver->Run();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception: " << e.what());
        return 1;
    }
    
    LOG_INFO("USB Receiver exited normally");
    return 0;
}
