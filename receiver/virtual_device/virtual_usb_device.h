#pragma once

#include "protocol/usb_types.h"
#include "protocol/usbip_protocol.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace usb_redirector {
namespace receiver {

class VirtualUsbDevice {
public:
    using UrbResponseCallback = std::function<void(const protocol::UsbUrb& urb)>;

    VirtualUsbDevice();
    ~VirtualUsbDevice();

    // 禁止拷贝
    VirtualUsbDevice(const VirtualUsbDevice&) = delete;
    VirtualUsbDevice& operator=(const VirtualUsbDevice&) = delete;

    // 设置回调函数
    void SetUrbResponseCallback(UrbResponseCallback callback) {
        urb_response_callback_ = std::move(callback);
    }

    // 设备管理
    bool CreateDevice(const protocol::UsbipDeviceInfo& device_info);
    bool AttachDevice();
    bool DetachDevice();
    void DestroyDevice();

    // 设备状态
    bool IsCreated() const { return created_.load(); }
    bool IsAttached() const { return attached_.load(); }

    // URB处理
    void ProcessUrb(const protocol::UsbUrb& urb);

    // 获取设备信息
    const protocol::UsbipDeviceInfo& GetDeviceInfo() const { return device_info_; }
    std::string GetDevicePath() const;

private:
    // USBIP内核接口
    bool LoadUsbipModule();
    bool UnloadUsbipModule();
    bool CreateUsbipPort();
    bool DestroyUsbipPort();
    bool AttachToPort();
    bool DetachFromPort();

    // URB处理
    void UrbProcessingThread();
    void HandleControlUrb(const protocol::UsbUrb& urb);
    void HandleBulkUrb(const protocol::UsbUrb& urb);
    void HandleInterruptUrb(const protocol::UsbUrb& urb);
    void HandleIsochronousUrb(const protocol::UsbUrb& urb);

    // 设备模拟
    void SimulateDeviceResponse(const protocol::UsbUrb& urb);
    std::vector<uint8_t> HandleStandardRequest(const protocol::UsbSetupPacket& setup);
    std::vector<uint8_t> HandleClassRequest(const protocol::UsbSetupPacket& setup);
    std::vector<uint8_t> HandleVendorRequest(const protocol::UsbSetupPacket& setup);

    // 描述符处理
    void CreateDeviceDescriptors();
    std::vector<uint8_t> GetDeviceDescriptor();
    std::vector<uint8_t> GetConfigurationDescriptor();
    std::vector<uint8_t> GetStringDescriptor(uint8_t index);

    // 大容量存储设备特定处理
    void HandleMassStorageUrb(const protocol::UsbUrb& urb);
    std::vector<uint8_t> ProcessScsiCommand(const std::vector<uint8_t>& cbw_data);

    // 系统接口
    bool ExecuteCommand(const std::string& command);
    bool WriteToFile(const std::string& path, const std::string& content);
    std::string ReadFromFile(const std::string& path);

    protocol::UsbipDeviceInfo device_info_;
    UrbResponseCallback urb_response_callback_;

    std::atomic<bool> created_;
    std::atomic<bool> attached_;
    std::atomic<bool> processing_;

    std::thread urb_thread_;

    std::string usbip_port_path_;
    std::string device_path_;
    int port_number_;

    mutable std::mutex mutex_;

    // 设备状态
    uint8_t current_configuration_;
    std::vector<uint8_t> device_descriptor_;
    std::vector<uint8_t> config_descriptor_;
    std::vector<std::string> string_descriptors_;
};

class UsbipManager {
public:
    static UsbipManager& Instance();

    // USBIP系统管理
    bool Initialize();
    void Cleanup();

    // 设备管理
    std::shared_ptr<VirtualUsbDevice> CreateVirtualDevice(const protocol::UsbipDeviceInfo& device_info);
    bool RemoveVirtualDevice(std::shared_ptr<VirtualUsbDevice> device);

    // 获取可用端口
    int GetAvailablePort();
    void ReleasePort(int port);

    // 系统状态
    bool IsUsbipModuleLoaded();
    std::vector<int> GetActivePorts();

private:
    UsbipManager() : initialized_(false) {}
    ~UsbipManager() = default;

    UsbipManager(const UsbipManager&) = delete;
    UsbipManager& operator=(const UsbipManager&) = delete;

    bool LoadKernelModule();
    bool UnloadKernelModule();

    std::vector<std::shared_ptr<VirtualUsbDevice>> virtual_devices_;
    std::vector<bool> port_usage_; // 端口使用情况

    mutable std::mutex mutex_;
    bool initialized_;
};

} // namespace receiver
} // namespace usb_redirector
