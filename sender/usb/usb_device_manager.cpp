#include "usb_device_manager.h"
#include "utils/logger.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace usb_redirector {
namespace sender {

UsbDeviceManager::UsbDeviceManager()
    : context_(nullptr)
    , monitoring_(false)
    , hotplug_handle_(0) {
}

UsbDeviceManager::~UsbDeviceManager() {
    Cleanup();
}

bool UsbDeviceManager::Initialize() {
    int ret = libusb_init(&context_);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to initialize libusb: " << libusb_error_name(ret));
        return false;
    }

    // 设置调试级别
    libusb_set_option(context_, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);

    LOG_INFO("USB device manager initialized successfully");
    return true;
}

void UsbDeviceManager::Cleanup() {
    StopHotplugMonitoring();

    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        devices_.clear();
    }

    if (context_) {
        libusb_exit(context_);
        context_ = nullptr;
    }

    LOG_INFO("USB device manager cleaned up");
}

std::vector<std::shared_ptr<UsbDevice>> UsbDeviceManager::EnumerateDevices() {
    std::vector<std::shared_ptr<UsbDevice>> result;

    if (!context_) {
        LOG_ERROR("USB context not initialized");
        return result;
    }

    libusb_device** device_list;
    ssize_t count = libusb_get_device_list(context_, &device_list);

    if (count < 0) {
        LOG_ERROR("Failed to get device list: " << libusb_error_name(count));
        return result;
    }

    for (ssize_t i = 0; i < count; ++i) {
        auto device = CreateDevice(device_list[i]);
        if (device) {
            result.push_back(device);
        }
    }

    libusb_free_device_list(device_list, 1);

    LOG_INFO("Enumerated " << result.size() << " USB devices");
    return result;
}

std::vector<std::shared_ptr<UsbDevice>> UsbDeviceManager::GetMassStorageDevices() {
    auto all_devices = EnumerateDevices();
    std::vector<std::shared_ptr<UsbDevice>> mass_storage_devices;

    for (const auto& device : all_devices) {
        const auto& info = device->GetDeviceInfo();
        if (info.descriptor.bDeviceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE)) {
            mass_storage_devices.push_back(device);
            LOG_INFO("Found mass storage device: " << device->GetPath());
        }
    }

    LOG_INFO("Found " << mass_storage_devices.size() << " mass storage devices");
    return mass_storage_devices;
}

std::shared_ptr<UsbDevice> UsbDeviceManager::FindDevice(uint16_t vendor_id, uint16_t product_id) {
    auto devices = EnumerateDevices();

    for (const auto& device : devices) {
        const auto& desc = device->GetDeviceInfo().descriptor;
        if (desc.idVendor == vendor_id && desc.idProduct == product_id) {
            return device;
        }
    }

    return nullptr;
}

std::shared_ptr<UsbDevice> UsbDeviceManager::FindDeviceByPath(const std::string& path) {
    auto devices = EnumerateDevices();

    for (const auto& device : devices) {
        if (device->GetPath() == path) {
            return device;
        }
    }

    return nullptr;
}

void UsbDeviceManager::StartHotplugMonitoring() {
    if (monitoring_.load() || !context_) {
        return;
    }

    // 检查是否支持热插拔
    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        LOG_WARNING("Hotplug not supported on this platform");
        return;
    }

    int ret = libusb_hotplug_register_callback(
        context_,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_NO_FLAGS,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        HotplugCallback,
        this,
        &hotplug_handle_
    );

    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to register hotplug callback: " << libusb_error_name(ret));
        return;
    }

    monitoring_.store(true);
    hotplug_thread_ = std::thread(&UsbDeviceManager::HotplugThread, this);

    LOG_INFO("Hotplug monitoring started");
}

void UsbDeviceManager::StopHotplugMonitoring() {
    if (!monitoring_.load()) {
        return;
    }

    monitoring_.store(false);

    if (hotplug_thread_.joinable()) {
        hotplug_thread_.join();
    }

    if (hotplug_handle_ && context_) {
        libusb_hotplug_deregister_callback(context_, hotplug_handle_);
        hotplug_handle_ = 0;
    }

    LOG_INFO("Hotplug monitoring stopped");
}

void UsbDeviceManager::HotplugThread() {
    while (monitoring_.load()) {
        struct timeval tv = {1, 0}; // 1秒超时
        libusb_handle_events_timeout(context_, &tv);
    }
}

int UsbDeviceManager::HotplugCallback(libusb_context* ctx, libusb_device* device,
                                     libusb_hotplug_event event, void* user_data) {
    auto* manager = static_cast<UsbDeviceManager*>(user_data);

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
        LOG_INFO("USB device connected");
        auto usb_device = manager->CreateDevice(device);
        if (usb_device && manager->device_callback_) {
            manager->device_callback_(usb_device, true);
        }
    } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
        LOG_INFO("USB device disconnected");
        // 这里需要找到对应的设备并通知断开连接
        // 暂时简化处理
        if (manager->device_callback_) {
            manager->device_callback_(nullptr, false);
        }
    }

    return 0;
}

std::shared_ptr<UsbDevice> UsbDeviceManager::CreateDevice(libusb_device* device) {
    if (!device) {
        return nullptr;
    }

    try {
        auto usb_device = std::make_shared<UsbDevice>(device, this);
        return usb_device;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create USB device: " << e.what());
        return nullptr;
    }
}

bool UsbDeviceManager::IsMassStorageDevice(const protocol::UsbDeviceDescriptor& desc) {
    return desc.bDeviceClass == static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE);
}

// UsbDevice implementation
UsbDevice::UsbDevice(libusb_device* device, UsbDeviceManager* manager)
    : device_(device)
    , handle_(nullptr)
    , manager_(manager) {

    if (device_) {
        libusb_ref_device(device_);
        LoadDeviceInfo();
    }
}

UsbDevice::~UsbDevice() {
    Close();

    if (device_) {
        libusb_unref_device(device_);
    }
}

std::string UsbDevice::GetPath() const {
    if (!device_) {
        return "";
    }

    uint8_t bus = libusb_get_bus_number(device_);
    uint8_t addr = libusb_get_device_address(device_);

    std::ostringstream oss;
    oss << "/dev/bus/usb/" << std::setfill('0') << std::setw(3) << static_cast<int>(bus)
        << "/" << std::setfill('0') << std::setw(3) << static_cast<int>(addr);

    return oss.str();
}

std::string UsbDevice::GetBusId() const {
    if (!device_) {
        return "";
    }

    uint8_t bus = libusb_get_bus_number(device_);
    uint8_t addr = libusb_get_device_address(device_);

    std::ostringstream oss;
    oss << static_cast<int>(bus) << "-" << static_cast<int>(addr);

    return oss.str();
}

bool UsbDevice::Open() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (handle_) {
        return true; // 已经打开
    }

    int ret = libusb_open(device_, &handle_);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to open USB device: " << libusb_error_name(ret));
        return false;
    }

    LOG_INFO("USB device opened: " << GetPath());
    return true;
}

void UsbDevice::Close() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 释放所有声明的接口
    for (int interface_num : claimed_interfaces_) {
        libusb_release_interface(handle_, interface_num);
    }
    claimed_interfaces_.clear();

    if (handle_) {
        libusb_close(handle_);
        handle_ = nullptr;
        LOG_INFO("USB device closed: " << GetPath());
    }
}

bool UsbDevice::ClaimInterface(int interface_number) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!handle_) {
        LOG_ERROR("Device not opened");
        return false;
    }

    int ret = libusb_claim_interface(handle_, interface_number);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to claim interface " << interface_number << ": " << libusb_error_name(ret));
        return false;
    }

    claimed_interfaces_.push_back(interface_number);
    LOG_INFO("Claimed interface " << interface_number);
    return true;
}

bool UsbDevice::ReleaseInterface(int interface_number) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!handle_) {
        return true;
    }

    int ret = libusb_release_interface(handle_, interface_number);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to release interface " << interface_number << ": " << libusb_error_name(ret));
        return false;
    }

    auto it = std::find(claimed_interfaces_.begin(), claimed_interfaces_.end(), interface_number);
    if (it != claimed_interfaces_.end()) {
        claimed_interfaces_.erase(it);
    }

    LOG_INFO("Released interface " << interface_number);
    return true;
}

bool UsbDevice::ControlTransfer(uint8_t request_type, uint8_t request, uint16_t value,
                               uint16_t index, uint8_t* data, uint16_t length, int* actual_length) {
    if (!handle_) {
        LOG_ERROR("Device not opened");
        return false;
    }

    int ret = libusb_control_transfer(handle_, request_type, request, value, index, data, length, 5000);
    if (ret < 0) {
        LOG_ERROR("Control transfer failed: " << libusb_error_name(ret));
        return false;
    }

    if (actual_length) {
        *actual_length = ret;
    }

    return true;
}

bool UsbDevice::BulkTransfer(uint8_t endpoint, uint8_t* data, int length, int* actual_length) {
    if (!handle_) {
        LOG_ERROR("Device not opened");
        return false;
    }

    int ret = libusb_bulk_transfer(handle_, endpoint, data, length, actual_length, 5000);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Bulk transfer failed: " << libusb_error_name(ret));
        return false;
    }

    return true;
}

bool UsbDevice::InterruptTransfer(uint8_t endpoint, uint8_t* data, int length, int* actual_length) {
    if (!handle_) {
        LOG_ERROR("Device not opened");
        return false;
    }

    int ret = libusb_interrupt_transfer(handle_, endpoint, data, length, actual_length, 5000);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Interrupt transfer failed: " << libusb_error_name(ret));
        return false;
    }

    return true;
}

bool UsbDevice::SubmitTransfer(libusb_transfer* transfer) {
    if (!handle_) {
        LOG_ERROR("Device not opened");
        return false;
    }

    int ret = libusb_submit_transfer(transfer);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Submit transfer failed: " << libusb_error_name(ret));
        return false;
    }

    return true;
}

bool UsbDevice::GetDeviceDescriptor(protocol::UsbDeviceDescriptor& desc) {
    if (!device_) {
        return false;
    }

    struct libusb_device_descriptor libusb_desc;
    int ret = libusb_get_device_descriptor(device_, &libusb_desc);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to get device descriptor: " << libusb_error_name(ret));
        return false;
    }

    std::memcpy(&desc, &libusb_desc, sizeof(desc));
    return true;
}

bool UsbDevice::GetConfigDescriptor(std::vector<uint8_t>& config_desc) {
    if (!device_) {
        return false;
    }

    struct libusb_config_descriptor* config;
    int ret = libusb_get_active_config_descriptor(device_, &config);
    if (ret != LIBUSB_SUCCESS) {
        LOG_ERROR("Failed to get config descriptor: " << libusb_error_name(ret));
        return false;
    }

    config_desc.assign(
        reinterpret_cast<const uint8_t*>(config),
        reinterpret_cast<const uint8_t*>(config) + config->wTotalLength
    );

    libusb_free_config_descriptor(config);
    return true;
}

bool UsbDevice::GetStringDescriptor(uint8_t desc_index, std::string& str) {
    if (!handle_) {
        LOG_ERROR("Device not opened");
        return false;
    }

    unsigned char buffer[256];
    int ret = libusb_get_string_descriptor_ascii(handle_, desc_index, buffer, sizeof(buffer));
    if (ret < 0) {
        LOG_ERROR("Failed to get string descriptor: " << libusb_error_name(ret));
        return false;
    }

    str = std::string(reinterpret_cast<char*>(buffer), ret);
    return true;
}

void UsbDevice::LoadDeviceInfo() {
    if (!device_) {
        return;
    }

    device_info_.path = GetPath();
    device_info_.bus_id = GetBusId();
    device_info_.bus_number = libusb_get_bus_number(device_);
    device_info_.device_number = libusb_get_device_address(device_);
    device_info_.speed = static_cast<protocol::UsbSpeed>(libusb_get_device_speed(device_));
    device_info_.is_connected = true;

    // 获取设备描述符
    struct libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(device_, &desc);
    if (ret == LIBUSB_SUCCESS) {
        std::memcpy(&device_info_.descriptor, &desc, sizeof(desc));
    }

    // 获取配置描述符
    struct libusb_config_descriptor* config;
    ret = libusb_get_active_config_descriptor(device_, &config);
    if (ret == LIBUSB_SUCCESS) {
        device_info_.config_descriptor.assign(
            reinterpret_cast<const uint8_t*>(config),
            reinterpret_cast<const uint8_t*>(config) + config->wTotalLength
        );
        libusb_free_config_descriptor(config);
    }
}

} // namespace sender
} // namespace usb_redirector
