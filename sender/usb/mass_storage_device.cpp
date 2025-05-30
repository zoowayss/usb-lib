#include "mass_storage_device.h"
#include "utils/logger.h"
#include <cstring>
#include <chrono>

namespace usb_redirector {
namespace sender {

static constexpr uint32_t CBW_SIGNATURE = 0x43425355; // "USBC"
static constexpr uint32_t CSW_SIGNATURE = 0x53425355; // "USBS"
static constexpr uint8_t CBW_FLAG_DATA_IN = 0x80;
static constexpr uint8_t CBW_FLAG_DATA_OUT = 0x00;

MassStorageDevice::MassStorageDevice(std::shared_ptr<UsbDevice> device)
    : device_(std::move(device))
    , initialized_(false)
    , capturing_(false)
    , interface_number_(-1)
    , next_tag_(1)
    , total_blocks_(0)
    , block_size_(512) {
    
    bulk_in_endpoint_ = {};
    bulk_out_endpoint_ = {};
}

MassStorageDevice::~MassStorageDevice() {
    Cleanup();
}

bool MassStorageDevice::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    if (!device_ || !device_->Open()) {
        LOG_ERROR("Failed to open USB device");
        return false;
    }
    
    // 查找批量传输端点
    if (!FindEndpoints()) {
        LOG_ERROR("Failed to find bulk endpoints");
        return false;
    }
    
    // 声明接口
    if (!device_->ClaimInterface(interface_number_)) {
        LOG_ERROR("Failed to claim interface " << interface_number_);
        return false;
    }
    
    // 重置设备
    if (!ResetDevice()) {
        LOG_WARNING("Failed to reset device, continuing anyway");
    }
    
    // 获取设备容量
    uint64_t capacity;
    if (GetCapacity(capacity, block_size_)) {
        total_blocks_ = capacity;
        LOG_INFO("Mass storage device capacity: " << total_blocks_ << " blocks, " 
                << block_size_ << " bytes per block");
    }
    
    initialized_ = true;
    LOG_INFO("Mass storage device initialized: " << device_->GetPath());
    return true;
}

void MassStorageDevice::Cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    StopCapture();
    
    if (device_ && initialized_) {
        device_->ReleaseInterface(interface_number_);
        device_->Close();
    }
    
    initialized_ = false;
    LOG_INFO("Mass storage device cleaned up");
}

bool MassStorageDevice::StartCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || capturing_) {
        return false;
    }
    
    capturing_ = true;
    LOG_INFO("Started capturing URB data for mass storage device");
    return true;
}

void MassStorageDevice::StopCapture() {
    capturing_ = false;
    LOG_INFO("Stopped capturing URB data");
}

bool MassStorageDevice::FindEndpoints() {
    const auto& device_info = device_->GetDeviceInfo();
    const auto& config_desc = device_info.config_descriptor;
    
    if (config_desc.empty()) {
        LOG_ERROR("No configuration descriptor available");
        return false;
    }
    
    // 解析配置描述符
    size_t offset = 0;
    while (offset < config_desc.size()) {
        if (offset + 2 > config_desc.size()) break;
        
        uint8_t desc_length = config_desc[offset];
        uint8_t desc_type = config_desc[offset + 1];
        
        if (desc_length == 0 || offset + desc_length > config_desc.size()) {
            break;
        }
        
        if (desc_type == static_cast<uint8_t>(protocol::UsbDescriptorType::INTERFACE)) {
            if (desc_length >= 9) {
                const auto* iface_desc = reinterpret_cast<const protocol::UsbInterfaceDescriptor*>(&config_desc[offset]);
                
                // 检查是否是大容量存储接口
                if (iface_desc->bInterfaceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE)) {
                    interface_number_ = iface_desc->bInterfaceNumber;
                    LOG_INFO("Found mass storage interface: " << interface_number_);
                }
            }
        } else if (desc_type == static_cast<uint8_t>(protocol::UsbDescriptorType::ENDPOINT)) {
            if (desc_length >= 7 && interface_number_ >= 0) {
                const auto* ep_desc = reinterpret_cast<const protocol::UsbEndpointDescriptor*>(&config_desc[offset]);
                
                // 检查是否是批量传输端点
                if ((ep_desc->bmAttributes & 0x03) == static_cast<uint8_t>(protocol::UsbTransferType::BULK)) {
                    EndpointInfo ep_info;
                    ep_info.address = ep_desc->bEndpointAddress;
                    ep_info.max_packet_size = ep_desc->wMaxPacketSize;
                    ep_info.is_in = (ep_desc->bEndpointAddress & 0x80) != 0;
                    
                    if (ep_info.is_in) {
                        bulk_in_endpoint_ = ep_info;
                        LOG_INFO("Found bulk IN endpoint: 0x" << std::hex << static_cast<int>(ep_info.address));
                    } else {
                        bulk_out_endpoint_ = ep_info;
                        LOG_INFO("Found bulk OUT endpoint: 0x" << std::hex << static_cast<int>(ep_info.address));
                    }
                }
            }
        }
        
        offset += desc_length;
    }
    
    return interface_number_ >= 0 && bulk_in_endpoint_.address != 0 && bulk_out_endpoint_.address != 0;
}

bool MassStorageDevice::ResetDevice() {
    // 发送批量存储重置请求
    int actual_length;
    return device_->ControlTransfer(
        0x21, // bmRequestType: Class, Interface, Host-to-device
        static_cast<uint8_t>(MassStorageRequest::BULK_ONLY_MASS_STORAGE_RESET),
        0,     // wValue
        interface_number_, // wIndex
        nullptr, // data
        0,     // wLength
        &actual_length
    );
}

bool MassStorageDevice::GetMaxLun(uint8_t& max_lun) {
    int actual_length;
    return device_->ControlTransfer(
        0xA1, // bmRequestType: Class, Interface, Device-to-host
        static_cast<uint8_t>(MassStorageRequest::GET_MAX_LUN),
        0,     // wValue
        interface_number_, // wIndex
        &max_lun, // data
        1,     // wLength
        &actual_length
    ) && actual_length == 1;
}

bool MassStorageDevice::GetCapacity(uint64_t& total_blocks, uint32_t& block_size) {
    // 先尝试READ CAPACITY (16)
    CommandBlockWrapper cbw = {};
    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = next_tag_++;
    cbw.dCBWDataTransferLength = 32; // READ CAPACITY (16) 返回32字节
    cbw.bmCBWFlags = CBW_FLAG_DATA_IN;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = 16;
    
    // READ CAPACITY (16) 命令
    cbw.CBWCB[0] = static_cast<uint8_t>(ScsiCommand::READ_CAPACITY_16);
    cbw.CBWCB[1] = 0x10; // Service Action
    // 其他字节保持为0
    
    if (!SendCBW(cbw)) {
        LOG_ERROR("Failed to send READ CAPACITY (16) CBW");
        return false;
    }
    
    std::vector<uint8_t> capacity_data;
    if (!TransferData(true, capacity_data, 32)) {
        LOG_WARNING("READ CAPACITY (16) failed, trying READ CAPACITY (10)");
        
        // 尝试READ CAPACITY (10)
        cbw.dCBWTag = next_tag_++;
        cbw.dCBWDataTransferLength = 8;
        cbw.bCBWCBLength = 10;
        std::memset(cbw.CBWCB, 0, 16);
        cbw.CBWCB[0] = static_cast<uint8_t>(ScsiCommand::READ_CAPACITY_10);
        
        if (!SendCBW(cbw) || !TransferData(true, capacity_data, 8)) {
            LOG_ERROR("Both READ CAPACITY commands failed");
            return false;
        }
        
        // 解析READ CAPACITY (10) 响应
        if (capacity_data.size() >= 8) {
            uint32_t last_block = (capacity_data[0] << 24) | (capacity_data[1] << 16) |
                                 (capacity_data[2] << 8) | capacity_data[3];
            block_size = (capacity_data[4] << 24) | (capacity_data[5] << 16) |
                        (capacity_data[6] << 8) | capacity_data[7];
            total_blocks = static_cast<uint64_t>(last_block) + 1;
        }
    } else {
        // 解析READ CAPACITY (16) 响应
        if (capacity_data.size() >= 32) {
            total_blocks = 0;
            for (int i = 0; i < 8; ++i) {
                total_blocks = (total_blocks << 8) | capacity_data[i];
            }
            total_blocks += 1; // 最后一个块号+1
            
            block_size = (capacity_data[8] << 24) | (capacity_data[9] << 16) |
                        (capacity_data[10] << 8) | capacity_data[11];
        }
    }
    
    CommandStatusWrapper csw;
    if (!ReceiveCSW(csw, cbw.dCBWTag)) {
        LOG_ERROR("Failed to receive CSW for READ CAPACITY");
        return false;
    }
    
    return csw.bCSWStatus == 0; // 成功状态
}

bool MassStorageDevice::SendCBW(const CommandBlockWrapper& cbw) {
    int actual_length;
    return device_->BulkTransfer(
        bulk_out_endpoint_.address,
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&cbw)),
        sizeof(CommandBlockWrapper),
        &actual_length
    ) && actual_length == sizeof(CommandBlockWrapper);
}

bool MassStorageDevice::ReceiveCSW(CommandStatusWrapper& csw, uint32_t expected_tag) {
    int actual_length;
    bool result = device_->BulkTransfer(
        bulk_in_endpoint_.address,
        reinterpret_cast<uint8_t*>(&csw),
        sizeof(CommandStatusWrapper),
        &actual_length
    );
    
    if (!result || actual_length != sizeof(CommandStatusWrapper)) {
        return false;
    }
    
    return csw.dCSWSignature == CSW_SIGNATURE && csw.dCSWTag == expected_tag;
}

bool MassStorageDevice::TransferData(bool is_read, std::vector<uint8_t>& data, uint32_t length) {
    if (length == 0) {
        return true;
    }
    
    data.resize(length);
    int actual_length;
    
    uint8_t endpoint = is_read ? bulk_in_endpoint_.address : bulk_out_endpoint_.address;
    
    return device_->BulkTransfer(endpoint, data.data(), length, &actual_length) &&
           static_cast<uint32_t>(actual_length) == length;
}

protocol::UsbUrb MassStorageDevice::CreateControlUrb(uint8_t request_type, uint8_t request,
                                                     uint16_t value, uint16_t index,
                                                     const std::vector<uint8_t>& data) {
    protocol::UsbUrb urb;
    urb.id = next_tag_++;
    urb.type = protocol::UsbTransferType::CONTROL;
    urb.direction = (request_type & 0x80) ? protocol::UsbDirection::IN : protocol::UsbDirection::OUT;
    urb.endpoint = 0;
    urb.flags = 0;
    urb.data = data;
    
    // 设置Setup包
    urb.setup.bmRequestType = request_type;
    urb.setup.bRequest = request;
    urb.setup.wValue = value;
    urb.setup.wIndex = index;
    urb.setup.wLength = static_cast<uint16_t>(data.size());
    
    urb.status = 0;
    urb.actual_length = 0;
    urb.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    return urb;
}

protocol::UsbUrb MassStorageDevice::CreateBulkUrb(uint8_t endpoint, const std::vector<uint8_t>& data,
                                                  protocol::UsbDirection direction) {
    protocol::UsbUrb urb;
    urb.id = next_tag_++;
    urb.type = protocol::UsbTransferType::BULK;
    urb.direction = direction;
    urb.endpoint = endpoint;
    urb.flags = 0;
    urb.data = data;
    urb.status = 0;
    urb.actual_length = static_cast<uint32_t>(data.size());
    urb.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    return urb;
}

} // namespace sender
} // namespace usb_redirector
