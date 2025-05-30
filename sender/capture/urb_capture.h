#pragma once

#include "protocol/usb_types.h"
#include "network/message_handler.h"
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace usb_redirector {
namespace sender {

class MassStorageDevice;

class UrbCapture {
public:
    using UrbCallback = std::function<void(const protocol::UsbUrb& urb)>;
    
    UrbCapture();
    ~UrbCapture();
    
    // 禁止拷贝
    UrbCapture(const UrbCapture&) = delete;
    UrbCapture& operator=(const UrbCapture&) = delete;
    
    // 设置回调函数
    void SetUrbCallback(UrbCallback callback) { urb_callback_ = std::move(callback); }
    
    // 添加要监控的设备
    bool AddDevice(std::shared_ptr<MassStorageDevice> device);
    void RemoveDevice(std::shared_ptr<MassStorageDevice> device);
    void RemoveAllDevices();
    
    // 开始和停止捕获
    bool StartCapture();
    void StopCapture();
    bool IsCapturing() const { return capturing_.load(); }
    
    // 手动注入URB (用于测试)
    void InjectUrb(const protocol::UsbUrb& urb);
    
    // 获取统计信息
    struct Statistics {
        uint64_t total_urbs;
        uint64_t control_urbs;
        uint64_t bulk_urbs;
        uint64_t interrupt_urbs;
        uint64_t iso_urbs;
        uint64_t bytes_transferred;
        uint64_t errors;
    };
    
    Statistics GetStatistics() const;
    void ResetStatistics();

private:
    void ProcessingThread();
    void OnDeviceData(const protocol::UsbUrb& urb);
    void UpdateStatistics(const protocol::UsbUrb& urb);
    
    std::vector<std::shared_ptr<MassStorageDevice>> devices_;
    UrbCallback urb_callback_;
    
    std::atomic<bool> capturing_;
    std::atomic<bool> should_stop_;
    
    std::thread processing_thread_;
    
    std::queue<protocol::UsbUrb> urb_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    mutable std::mutex devices_mutex_;
    mutable std::mutex stats_mutex_;
    Statistics statistics_;
};

class UrbProcessor {
public:
    UrbProcessor();
    ~UrbProcessor() = default;
    
    // 处理URB数据，转换为USBIP格式
    std::vector<uint8_t> ProcessUrb(const protocol::UsbUrb& urb);
    
    // 创建USBIP命令
    network::NetworkMessage CreateUsbipSubmit(const protocol::UsbUrb& urb);
    network::NetworkMessage CreateUsbipResponse(const protocol::UsbUrb& urb);

private:
    protocol::UsbipCmdSubmit CreateCmdSubmit(const protocol::UsbUrb& urb);
    protocol::UsbipRetSubmit CreateRetSubmit(const protocol::UsbUrb& urb);
    
    uint32_t next_seqnum_;
};

} // namespace sender
} // namespace usb_redirector
