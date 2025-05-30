#pragma once

#include <libusb.h>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "protocol/usb_types.h"

namespace usb_redirector {
namespace sender {

class UsbDevice;

class UsbDeviceManager {
public:
    using DeviceCallback = std::function<void(std::shared_ptr<UsbDevice> device, bool connected)>;

    UsbDeviceManager();
    ~UsbDeviceManager();

    // 禁止拷贝
    UsbDeviceManager(const UsbDeviceManager&) = delete;
    UsbDeviceManager& operator=(const UsbDeviceManager&) = delete;

    // 初始化和清理
    bool Initialize();
    void Cleanup();

    // 设备枚举
    std::vector<std::shared_ptr<UsbDevice>> EnumerateDevices();
    std::vector<std::shared_ptr<UsbDevice>> GetMassStorageDevices();

    // 设备查找
    std::shared_ptr<UsbDevice> FindDevice(uint16_t vendor_id, uint16_t product_id);
    std::shared_ptr<UsbDevice> FindDeviceByPath(const std::string& path);

    // 热插拔监控
    void SetDeviceCallback(DeviceCallback callback) { device_callback_ = std::move(callback); }
    void StartHotplugMonitoring();
    void StopHotplugMonitoring();

    // 获取libusb上下文
    libusb_context* GetContext() const { return context_; }

private:
    void HotplugThread();
    static int HotplugCallback(libusb_context* ctx, libusb_device* device,
                              libusb_hotplug_event event, void* user_data);

    std::shared_ptr<UsbDevice> CreateDevice(libusb_device* device);
    bool IsMassStorageDevice(const protocol::UsbDeviceDescriptor& desc);

    libusb_context* context_;
    std::vector<std::shared_ptr<UsbDevice>> devices_;
    DeviceCallback device_callback_;

    std::atomic<bool> monitoring_;
    std::thread hotplug_thread_;
    libusb_hotplug_callback_handle hotplug_handle_;

    mutable std::mutex devices_mutex_;
};

class UsbDevice {
public:
    UsbDevice(libusb_device* device, UsbDeviceManager* manager);
    ~UsbDevice();

    // 禁止拷贝
    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;

    // 设备信息
    const protocol::UsbDevice& GetDeviceInfo() const { return device_info_; }
    std::string GetPath() const;
    std::string GetBusId() const;

    // 设备操作
    bool Open();
    void Close();
    bool IsOpen() const { return handle_ != nullptr; }

    // 声明接口
    bool ClaimInterface(int interface_number);
    bool ReleaseInterface(int interface_number);

    // 传输操作
    bool ControlTransfer(uint8_t request_type, uint8_t request, uint16_t value,
                        uint16_t index, uint8_t* data, uint16_t length, int* actual_length);
    bool BulkTransfer(uint8_t endpoint, uint8_t* data, int length, int* actual_length);
    bool InterruptTransfer(uint8_t endpoint, uint8_t* data, int length, int* actual_length);

    // 异步传输
    bool SubmitTransfer(libusb_transfer* transfer);

    // 获取描述符
    bool GetDeviceDescriptor(protocol::UsbDeviceDescriptor& desc);
    bool GetConfigDescriptor(std::vector<uint8_t>& config_desc);
    bool GetStringDescriptor(uint8_t desc_index, std::string& str);

private:
    void LoadDeviceInfo();

    libusb_device* device_;
    libusb_device_handle* handle_;
    UsbDeviceManager* manager_;
    protocol::UsbDevice device_info_;

    std::vector<int> claimed_interfaces_;
    mutable std::mutex mutex_;
};

} // namespace sender
} // namespace usb_redirector
