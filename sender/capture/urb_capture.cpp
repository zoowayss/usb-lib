#include "urb_capture.h"
#include "usb/mass_storage_device.h"
#include "utils/logger.h"
#include <algorithm>

namespace usb_redirector {
namespace sender {

UrbCapture::UrbCapture() 
    : capturing_(false)
    , should_stop_(false)
    , statistics_{} {
}

UrbCapture::~UrbCapture() {
    StopCapture();
}

bool UrbCapture::AddDevice(std::shared_ptr<MassStorageDevice> device) {
    if (!device) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    // 检查设备是否已存在
    auto it = std::find(devices_.begin(), devices_.end(), device);
    if (it != devices_.end()) {
        return true; // 已存在
    }
    
    // 设置设备的数据回调
    device->SetDataCallback([this](const protocol::UsbUrb& urb) {
        OnDeviceData(urb);
    });
    
    devices_.push_back(device);
    LOG_INFO("Added device to URB capture: " << device->GetPath());
    return true;
}

void UrbCapture::RemoveDevice(std::shared_ptr<MassStorageDevice> device) {
    if (!device) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto it = std::find(devices_.begin(), devices_.end(), device);
    if (it != devices_.end()) {
        devices_.erase(it);
        LOG_INFO("Removed device from URB capture: " << device->GetPath());
    }
}

void UrbCapture::RemoveAllDevices() {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    devices_.clear();
    LOG_INFO("Removed all devices from URB capture");
}

bool UrbCapture::StartCapture() {
    if (capturing_.load()) {
        return true;
    }
    
    should_stop_.store(false);
    capturing_.store(true);
    
    // 启动处理线程
    processing_thread_ = std::thread(&UrbCapture::ProcessingThread, this);
    
    // 启动所有设备的捕获
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (auto& device : devices_) {
            if (!device->StartCapture()) {
                LOG_WARNING("Failed to start capture for device: " << device->GetPath());
            }
        }
    }
    
    LOG_INFO("URB capture started");
    return true;
}

void UrbCapture::StopCapture() {
    if (!capturing_.load()) {
        return;
    }
    
    should_stop_.store(true);
    capturing_.store(false);
    
    // 停止所有设备的捕获
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (auto& device : devices_) {
            device->StopCapture();
        }
    }
    
    // 通知处理线程退出
    queue_cv_.notify_all();
    
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    LOG_INFO("URB capture stopped");
}

void UrbCapture::InjectUrb(const protocol::UsbUrb& urb) {
    if (!capturing_.load()) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        urb_queue_.push(urb);
    }
    queue_cv_.notify_one();
}

UrbCapture::Statistics UrbCapture::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

void UrbCapture::ResetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_ = {};
    LOG_INFO("URB capture statistics reset");
}

void UrbCapture::ProcessingThread() {
    LOG_INFO("URB processing thread started");
    
    while (!should_stop_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 等待URB数据或停止信号
        queue_cv_.wait(lock, [this] {
            return !urb_queue_.empty() || should_stop_.load();
        });
        
        if (should_stop_.load()) {
            break;
        }
        
        // 处理队列中的所有URB
        while (!urb_queue_.empty()) {
            protocol::UsbUrb urb = urb_queue_.front();
            urb_queue_.pop();
            lock.unlock();
            
            // 更新统计信息
            UpdateStatistics(urb);
            
            // 调用回调函数
            if (urb_callback_) {
                urb_callback_(urb);
            }
            
            lock.lock();
        }
    }
    
    LOG_INFO("URB processing thread stopped");
}

void UrbCapture::OnDeviceData(const protocol::UsbUrb& urb) {
    if (!capturing_.load()) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        urb_queue_.push(urb);
    }
    queue_cv_.notify_one();
}

void UrbCapture::UpdateStatistics(const protocol::UsbUrb& urb) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    statistics_.total_urbs++;
    statistics_.bytes_transferred += urb.actual_length;
    
    if (urb.status != 0) {
        statistics_.errors++;
    }
    
    switch (urb.type) {
        case protocol::UsbTransferType::CONTROL:
            statistics_.control_urbs++;
            break;
        case protocol::UsbTransferType::BULK:
            statistics_.bulk_urbs++;
            break;
        case protocol::UsbTransferType::INTERRUPT:
            statistics_.interrupt_urbs++;
            break;
        case protocol::UsbTransferType::ISOCHRONOUS:
            statistics_.iso_urbs++;
            break;
    }
}

// UrbProcessor implementation
UrbProcessor::UrbProcessor() : next_seqnum_(1) {}

std::vector<uint8_t> UrbProcessor::ProcessUrb(const protocol::UsbUrb& urb) {
    std::vector<uint8_t> result;
    
    if (urb.direction == protocol::UsbDirection::OUT) {
        // 主机到设备的传输，创建USBIP_CMD_SUBMIT
        auto cmd_submit = CreateCmdSubmit(urb);
        result = protocol::UsbipProtocol::SerializeCmdSubmit(cmd_submit, urb.data.data(), urb.data.size());
    } else {
        // 设备到主机的传输，创建USBIP_RET_SUBMIT
        auto ret_submit = CreateRetSubmit(urb);
        result = protocol::UsbipProtocol::SerializeRetSubmit(ret_submit, urb.data.data(), urb.data.size());
    }
    
    return result;
}

network::NetworkMessage UrbProcessor::CreateUsbipSubmit(const protocol::UsbUrb& urb) {
    auto usbip_data = ProcessUrb(urb);
    return network::NetworkMessage(network::MessageType::URB_SUBMIT, usbip_data);
}

network::NetworkMessage UrbProcessor::CreateUsbipResponse(const protocol::UsbUrb& urb) {
    auto usbip_data = ProcessUrb(urb);
    return network::NetworkMessage(network::MessageType::URB_RESPONSE, usbip_data);
}

protocol::UsbipCmdSubmit UrbProcessor::CreateCmdSubmit(const protocol::UsbUrb& urb) {
    protocol::UsbipCmdSubmit cmd = {};
    
    cmd.header.command = static_cast<uint32_t>(protocol::UsbipOpCode::USBIP_CMD_SUBMIT);
    cmd.header.seqnum = next_seqnum_++;
    cmd.header.devid = 0; // 设备ID，需要根据实际设备设置
    cmd.header.direction = static_cast<uint32_t>(urb.direction);
    cmd.header.ep = urb.endpoint;
    
    cmd.transfer_flags = urb.flags;
    cmd.transfer_buffer_length = static_cast<int32_t>(urb.data.size());
    cmd.start_frame = 0;
    cmd.number_of_packets = 0;
    cmd.interval = 0;
    
    // 对于控制传输，设置setup包
    if (urb.type == protocol::UsbTransferType::CONTROL) {
        std::memcpy(&cmd.setup, &urb.setup, sizeof(urb.setup));
    } else {
        cmd.setup = 0;
    }
    
    return cmd;
}

protocol::UsbipRetSubmit UrbProcessor::CreateRetSubmit(const protocol::UsbUrb& urb) {
    protocol::UsbipRetSubmit ret = {};
    
    ret.header.command = static_cast<uint32_t>(protocol::UsbipOpCode::USBIP_RET_SUBMIT);
    ret.header.seqnum = next_seqnum_++;
    ret.header.devid = 0; // 设备ID，需要根据实际设备设置
    ret.header.direction = static_cast<uint32_t>(urb.direction);
    ret.header.ep = urb.endpoint;
    
    ret.status = urb.status;
    ret.actual_length = static_cast<int32_t>(urb.actual_length);
    ret.start_frame = 0;
    ret.number_of_packets = 0;
    ret.error_count = 0;
    
    return ret;
}

} // namespace sender
} // namespace usb_redirector
