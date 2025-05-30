#include "virtual_usb_device.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <algorithm>

namespace usb_redirector {
namespace receiver {

VirtualUsbDevice::VirtualUsbDevice()
    : created_(false)
    , attached_(false)
    , processing_(false)
    , port_number_(-1)
    , current_configuration_(0) {

    // 初始化字符串描述符
    string_descriptors_.resize(4);
    string_descriptors_[0] = ""; // 语言ID描述符
    string_descriptors_[1] = "USB Redirector"; // 制造商
    string_descriptors_[2] = "Virtual Mass Storage"; // 产品名
    string_descriptors_[3] = "123456789"; // 序列号
}

VirtualUsbDevice::~VirtualUsbDevice() {
    DestroyDevice();
}

bool VirtualUsbDevice::CreateDevice(const protocol::UsbipDeviceInfo& device_info) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (created_.load()) {
        LOG_WARNING("Virtual device already created");
        return true;
    }

    device_info_ = device_info;

    // 获取可用端口
    port_number_ = UsbipManager::Instance().GetAvailablePort();
    if (port_number_ < 0) {
        LOG_ERROR("No available USBIP port");
        return false;
    }

    // 构建设备路径
    usbip_port_path_ = "/sys/devices/platform/vhci_hcd.0/usb" + std::to_string(port_number_ + 1);
    device_path_ = "/dev/bus/usb/" + std::to_string(port_number_ + 1) + "/001";

    // 创建设备描述符
    CreateDeviceDescriptors();

    created_.store(true);
    LOG_INFO("Virtual USB device created on port " << port_number_);
    return true;
}

bool VirtualUsbDevice::AttachDevice() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!created_.load()) {
        LOG_ERROR("Device not created");
        return false;
    }

    if (attached_.load()) {
        LOG_WARNING("Device already attached");
        return true;
    }

    // 通过USBIP内核接口附加设备
    if (!AttachToPort()) {
        LOG_ERROR("Failed to attach device to USBIP port");
        return false;
    }

    // 启动URB处理线程
    processing_.store(true);
    urb_thread_ = std::thread(&VirtualUsbDevice::UrbProcessingThread, this);

    attached_.store(true);
    LOG_INFO("Virtual USB device attached");
    return true;
}

bool VirtualUsbDevice::DetachDevice() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!attached_.load()) {
        return true;
    }

    // 停止URB处理
    processing_.store(false);
    if (urb_thread_.joinable()) {
        urb_thread_.join();
    }

    // 从USBIP端口分离设备
    DetachFromPort();

    attached_.store(false);
    LOG_INFO("Virtual USB device detached");
    return true;
}

void VirtualUsbDevice::DestroyDevice() {
    DetachDevice();

    std::lock_guard<std::mutex> lock(mutex_);

    if (created_.load()) {
        // 释放端口
        if (port_number_ >= 0) {
            UsbipManager::Instance().ReleasePort(port_number_);
            port_number_ = -1;
        }

        created_.store(false);
        LOG_INFO("Virtual USB device destroyed");
    }
}

void VirtualUsbDevice::ProcessUrb(const protocol::UsbUrb& urb) {
    if (!attached_.load()) {
        LOG_WARNING("Device not attached, ignoring URB");
        return;
    }

    LOG_DEBUG("Processing URB: type=" << static_cast<int>(urb.type)
             << ", endpoint=" << static_cast<int>(urb.endpoint)
             << ", direction=" << static_cast<int>(urb.direction)
             << ", length=" << urb.data.size());

    switch (urb.type) {
        case protocol::UsbTransferType::CONTROL:
            HandleControlUrb(urb);
            break;
        case protocol::UsbTransferType::BULK:
            HandleBulkUrb(urb);
            break;
        case protocol::UsbTransferType::INTERRUPT:
            HandleInterruptUrb(urb);
            break;
        case protocol::UsbTransferType::ISOCHRONOUS:
            HandleIsochronousUrb(urb);
            break;
    }
}

std::string VirtualUsbDevice::GetDevicePath() const {
    return device_path_;
}

bool VirtualUsbDevice::AttachToPort() {
    // 这里需要与USBIP内核模块交互
    // 简化实现：通过sysfs接口模拟设备附加

    std::string attach_command = "echo 'attach " + std::to_string(port_number_) +
                                " " + device_info_.busid + "' > /sys/devices/platform/vhci_hcd.0/attach";

    return ExecuteCommand(attach_command);
}

bool VirtualUsbDevice::DetachFromPort() {
    std::string detach_command = "echo 'detach " + std::to_string(port_number_) +
                                "' > /sys/devices/platform/vhci_hcd.0/detach";

    return ExecuteCommand(detach_command);
}

void VirtualUsbDevice::UrbProcessingThread() {
    LOG_INFO("URB processing thread started for virtual device");

    while (processing_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 这里应该从内核接收URB请求
        // 简化实现：等待外部URB注入
    }

    LOG_INFO("URB processing thread stopped");
}

void VirtualUsbDevice::HandleControlUrb(const protocol::UsbUrb& urb) {
    LOG_DEBUG("Handling control URB");

    protocol::UsbUrb response = urb;
    response.direction = (urb.direction == protocol::UsbDirection::IN) ?
                        protocol::UsbDirection::OUT : protocol::UsbDirection::IN;

    // 解析Setup包
    const auto& setup = urb.setup;
    uint8_t request_type = setup.bmRequestType;
    uint8_t request = setup.bRequest;

    if ((request_type & 0x60) == 0x00) { // 标准请求
        response.data = HandleStandardRequest(setup);
    } else if ((request_type & 0x60) == 0x20) { // 类请求
        response.data = HandleClassRequest(setup);
    } else if ((request_type & 0x60) == 0x40) { // 厂商请求
        response.data = HandleVendorRequest(setup);
    }

    response.status = 0; // 成功
    response.actual_length = static_cast<uint32_t>(response.data.size());

    if (urb_response_callback_) {
        urb_response_callback_(response);
    }
}

void VirtualUsbDevice::HandleBulkUrb(const protocol::UsbUrb& urb) {
    LOG_DEBUG("Handling bulk URB");

    // 对于大容量存储设备，处理SCSI命令
    if (device_info_.bDeviceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE)) {
        HandleMassStorageUrb(urb);
    } else {
        // 其他设备类型的处理
        SimulateDeviceResponse(urb);
    }
}

void VirtualUsbDevice::HandleInterruptUrb(const protocol::UsbUrb& urb) {
    LOG_DEBUG("Handling interrupt URB");
    SimulateDeviceResponse(urb);
}

void VirtualUsbDevice::HandleIsochronousUrb(const protocol::UsbUrb& urb) {
    LOG_DEBUG("Handling isochronous URB");
    SimulateDeviceResponse(urb);
}

void VirtualUsbDevice::SimulateDeviceResponse(const protocol::UsbUrb& urb) {
    protocol::UsbUrb response = urb;
    response.direction = (urb.direction == protocol::UsbDirection::IN) ?
                        protocol::UsbDirection::OUT : protocol::UsbDirection::IN;

    // 简单的回显响应
    if (urb.direction == protocol::UsbDirection::OUT) {
        // 主机发送数据到设备，设备确认
        response.data.clear();
    } else {
        // 设备发送数据到主机，返回一些测试数据
        response.data = {0x00, 0x01, 0x02, 0x03};
    }

    response.status = 0;
    response.actual_length = static_cast<uint32_t>(response.data.size());

    if (urb_response_callback_) {
        urb_response_callback_(response);
    }
}

std::vector<uint8_t> VirtualUsbDevice::HandleStandardRequest(const protocol::UsbSetupPacket& setup) {
    std::vector<uint8_t> response;

    switch (setup.bRequest) {
        case static_cast<uint8_t>(protocol::UsbStandardRequest::GET_DESCRIPTOR):
            {
                uint8_t desc_type = (setup.wValue >> 8) & 0xFF;
                uint8_t desc_index = setup.wValue & 0xFF;

                switch (desc_type) {
                    case static_cast<uint8_t>(protocol::UsbDescriptorType::DEVICE):
                        response = GetDeviceDescriptor();
                        break;
                    case static_cast<uint8_t>(protocol::UsbDescriptorType::CONFIGURATION):
                        response = GetConfigurationDescriptor();
                        break;
                    case static_cast<uint8_t>(protocol::UsbDescriptorType::STRING):
                        response = GetStringDescriptor(desc_index);
                        break;
                }
            }
            break;

        case static_cast<uint8_t>(protocol::UsbStandardRequest::SET_CONFIGURATION):
            current_configuration_ = setup.wValue & 0xFF;
            LOG_INFO("Set configuration: " << static_cast<int>(current_configuration_));
            break;

        case static_cast<uint8_t>(protocol::UsbStandardRequest::GET_CONFIGURATION):
            response.push_back(current_configuration_);
            break;
    }

    return response;
}

std::vector<uint8_t> VirtualUsbDevice::HandleClassRequest(const protocol::UsbSetupPacket& setup) {
    std::vector<uint8_t> response;

    // 大容量存储类请求
    if (device_info_.bDeviceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE)) {
        switch (setup.bRequest) {
            case 0xFF: // Bulk-Only Mass Storage Reset
                LOG_INFO("Mass storage reset request");
                break;
            case 0xFE: // Get Max LUN
                response.push_back(0); // 只有一个LUN
                LOG_INFO("Get Max LUN request");
                break;
        }
    }

    return response;
}

std::vector<uint8_t> VirtualUsbDevice::HandleVendorRequest(const protocol::UsbSetupPacket& setup) {
    std::vector<uint8_t> response;

    LOG_INFO("Vendor request: " << static_cast<int>(setup.bRequest));

    // 厂商特定请求处理
    // 这里可以根据具体设备需求实现

    return response;
}

void VirtualUsbDevice::CreateDeviceDescriptors() {
    // 创建设备描述符
    device_descriptor_.resize(18);
    auto* desc = reinterpret_cast<protocol::UsbDeviceDescriptor*>(device_descriptor_.data());

    desc->bLength = 18;
    desc->bDescriptorType = static_cast<uint8_t>(protocol::UsbDescriptorType::DEVICE);
    desc->bcdUSB = 0x0200; // USB 2.0
    desc->bDeviceClass = device_info_.bDeviceClass;
    desc->bDeviceSubClass = device_info_.bDeviceSubClass;
    desc->bDeviceProtocol = device_info_.bDeviceProtocol;
    desc->bMaxPacketSize0 = 64;
    desc->idVendor = device_info_.idVendor;
    desc->idProduct = device_info_.idProduct;
    desc->bcdDevice = device_info_.bcdDevice;
    desc->iManufacturer = 1;
    desc->iProduct = 2;
    desc->iSerialNumber = 3;
    desc->bNumConfigurations = 1;

    // 创建配置描述符（简化版本）
    config_descriptor_.resize(32); // 配置+接口+端点描述符
    // 这里应该根据实际设备创建完整的配置描述符
}

std::vector<uint8_t> VirtualUsbDevice::GetDeviceDescriptor() {
    return device_descriptor_;
}

std::vector<uint8_t> VirtualUsbDevice::GetConfigurationDescriptor() {
    return config_descriptor_;
}

std::vector<uint8_t> VirtualUsbDevice::GetStringDescriptor(uint8_t index) {
    std::vector<uint8_t> response;

    if (index < string_descriptors_.size()) {
        const std::string& str = string_descriptors_[index];

        if (index == 0) {
            // 语言ID描述符
            response = {4, 3, 0x09, 0x04}; // 英语(美国)
        } else {
            // Unicode字符串描述符
            response.push_back(static_cast<uint8_t>(2 + str.length() * 2));
            response.push_back(3); // 字符串描述符类型

            for (char c : str) {
                response.push_back(c);
                response.push_back(0); // Unicode低字节
            }
        }
    }

    return response;
}

void VirtualUsbDevice::HandleMassStorageUrb(const protocol::UsbUrb& urb) {
    // 简化的大容量存储设备处理
    protocol::UsbUrb response = urb;
    response.direction = (urb.direction == protocol::UsbDirection::IN) ?
                        protocol::UsbDirection::OUT : protocol::UsbDirection::IN;

    if (urb.direction == protocol::UsbDirection::OUT) {
        // 处理CBW (Command Block Wrapper)
        if (urb.data.size() >= 31) { // CBW大小
            response.data = ProcessScsiCommand(urb.data);
        }
    } else {
        // 返回CSW (Command Status Wrapper)
        response.data.resize(13);
        // 简化的CSW：签名 + 标签 + 残留 + 状态
        uint32_t signature = 0x53425355; // "USBS"
        std::memcpy(response.data.data(), &signature, 4);
        // 其他字段设为0（成功状态）
    }

    response.status = 0;
    response.actual_length = static_cast<uint32_t>(response.data.size());

    if (urb_response_callback_) {
        urb_response_callback_(response);
    }
}

std::vector<uint8_t> VirtualUsbDevice::ProcessScsiCommand(const std::vector<uint8_t>& cbw_data) {
    std::vector<uint8_t> response;

    // 简化的SCSI命令处理
    if (cbw_data.size() >= 31) {
        uint8_t scsi_cmd = cbw_data[15]; // SCSI命令在CBW的偏移15

        switch (scsi_cmd) {
            case 0x12: { // INQUIRY
                response.resize(36);
                response[0] = 0x00; // 直接访问设备
                response[1] = 0x80; // 可移动介质
                response[2] = 0x04; // SCSI-2
                response[3] = 0x02; // 响应数据格式
                response[4] = 31;   // 附加长度
                // 厂商和产品标识
                std::string vendor = "Virtual ";
                std::string product = "Mass Storage   ";
                std::memcpy(&response[8], vendor.c_str(), 8);
                std::memcpy(&response[16], product.c_str(), 16);
                break;
            }

            case 0x25: { // READ CAPACITY (10)
                response.resize(8);
                // 返回虚拟容量：1024个块，每块512字节
                uint32_t last_block = htonl(1023);
                uint32_t block_size = htonl(512);
                std::memcpy(response.data(), &last_block, 4);
                std::memcpy(response.data() + 4, &block_size, 4);
                break;
            }

            default:
                LOG_WARNING("Unsupported SCSI command: 0x" << std::hex << static_cast<int>(scsi_cmd));
                break;
        }
    }

    return response;
}

bool VirtualUsbDevice::ExecuteCommand(const std::string& command) {
    int result = std::system(command.c_str());
    return result == 0;
}

bool VirtualUsbDevice::WriteToFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    return file.good();
}

std::string VirtualUsbDevice::ReadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// UsbipManager implementation
UsbipManager& UsbipManager::Instance() {
    static UsbipManager instance;
    return instance;
}

bool UsbipManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    // 初始化端口使用情况（假设最多8个端口）
    port_usage_.resize(8, false);

    // 加载USBIP内核模块
    if (!LoadKernelModule()) {
        LOG_ERROR("Failed to load USBIP kernel module");
        return false;
    }

    initialized_ = true;
    LOG_INFO("USBIP manager initialized");
    return true;
}

void UsbipManager::Cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    // 清理所有虚拟设备
    virtual_devices_.clear();

    // 卸载内核模块
    UnloadKernelModule();

    initialized_ = false;
    LOG_INFO("USBIP manager cleaned up");
}

int UsbipManager::GetAvailablePort() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (size_t i = 0; i < port_usage_.size(); ++i) {
        if (!port_usage_[i]) {
            port_usage_[i] = true;
            return static_cast<int>(i);
        }
    }

    return -1; // 没有可用端口
}

void UsbipManager::ReleasePort(int port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (port >= 0 && port < static_cast<int>(port_usage_.size())) {
        port_usage_[port] = false;
    }
}

bool UsbipManager::LoadKernelModule() {
    // 加载vhci-hcd模块
    int result = std::system("modprobe vhci-hcd");
    return result == 0;
}

bool UsbipManager::UnloadKernelModule() {
    // 卸载vhci-hcd模块
    int result = std::system("modprobe -r vhci-hcd");
    return result == 0;
}

std::shared_ptr<VirtualUsbDevice> UsbipManager::CreateVirtualDevice(const protocol::UsbipDeviceInfo& device_info) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto device = std::make_shared<VirtualUsbDevice>();
    if (device->CreateDevice(device_info)) {
        virtual_devices_.push_back(device);
        return device;
    }

    return nullptr;
}

bool UsbipManager::RemoveVirtualDevice(std::shared_ptr<VirtualUsbDevice> device) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(virtual_devices_.begin(), virtual_devices_.end(), device);
    if (it != virtual_devices_.end()) {
        (*it)->DestroyDevice();
        virtual_devices_.erase(it);
        return true;
    }

    return false;
}

bool UsbipManager::IsUsbipModuleLoaded() {
    // 检查vhci_hcd模块是否已加载
    int result = std::system("lsmod | grep -q vhci_hcd");
    return result == 0;
}

std::vector<int> UsbipManager::GetActivePorts() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int> active_ports;
    for (size_t i = 0; i < port_usage_.size(); ++i) {
        if (port_usage_[i]) {
            active_ports.push_back(static_cast<int>(i));
        }
    }

    return active_ports;
}

} // namespace receiver
} // namespace usb_redirector
