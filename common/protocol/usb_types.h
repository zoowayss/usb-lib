#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace usb_redirector {
namespace protocol {

// USB传输类型
enum class UsbTransferType : uint8_t {
    CONTROL = 0,
    ISOCHRONOUS = 1,
    BULK = 2,
    INTERRUPT = 3
};

// USB传输方向
enum class UsbDirection : uint8_t {
    OUT = 0,  // 主机到设备
    IN = 1    // 设备到主机
};

// USB设备速度
enum class UsbSpeed : uint32_t {
    UNKNOWN = 0,
    LOW = 1,     // 1.5 Mbps
    FULL = 2,    // 12 Mbps
    HIGH = 3,    // 480 Mbps
    SUPER = 4    // 5 Gbps
};

// USB设备类
enum class UsbDeviceClass : uint8_t {
    PER_INTERFACE = 0x00,
    AUDIO = 0x01,
    COMM = 0x02,
    HID = 0x03,
    PHYSICAL = 0x05,
    IMAGE = 0x06,
    PRINTER = 0x07,
    MASS_STORAGE = 0x08,  // 大容量存储设备
    HUB = 0x09,
    DATA = 0x0A,
    SMART_CARD = 0x0B,
    CONTENT_SECURITY = 0x0D,
    VIDEO = 0x0E,
    PERSONAL_HEALTHCARE = 0x0F,
    DIAGNOSTIC_DEVICE = 0xDC,
    WIRELESS = 0xE0,
    MISCELLANEOUS = 0xEF,
    APPLICATION_SPECIFIC = 0xFE,
    VENDOR_SPECIFIC = 0xFF
};

// USB标准请求
enum class UsbStandardRequest : uint8_t {
    GET_STATUS = 0x00,
    CLEAR_FEATURE = 0x01,
    SET_FEATURE = 0x03,
    SET_ADDRESS = 0x05,
    GET_DESCRIPTOR = 0x06,
    SET_DESCRIPTOR = 0x07,
    GET_CONFIGURATION = 0x08,
    SET_CONFIGURATION = 0x09,
    GET_INTERFACE = 0x0A,
    SET_INTERFACE = 0x0B,
    SYNCH_FRAME = 0x0C
};

// USB描述符类型
enum class UsbDescriptorType : uint8_t {
    DEVICE = 0x01,
    CONFIGURATION = 0x02,
    STRING = 0x03,
    INTERFACE = 0x04,
    ENDPOINT = 0x05,
    DEVICE_QUALIFIER = 0x06,
    OTHER_SPEED_CONFIGURATION = 0x07,
    INTERFACE_POWER = 0x08
};

// USB设备描述符
struct UsbDeviceDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed));

// USB配置描述符
struct UsbConfigurationDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed));

// USB接口描述符
struct UsbInterfaceDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed));

// USB端点描述符
struct UsbEndpointDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed));

// USB Setup包
struct UsbSetupPacket {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

// URB (USB Request Block)
struct UsbUrb {
    uint32_t id;                    // URB唯一标识
    UsbTransferType type;           // 传输类型
    UsbDirection direction;         // 传输方向
    uint8_t endpoint;               // 端点地址
    uint32_t flags;                 // 传输标志
    std::vector<uint8_t> data;      // 数据缓冲区
    UsbSetupPacket setup;           // Setup包(仅控制传输)
    int32_t status;                 // 传输状态
    uint32_t actual_length;         // 实际传输长度
    uint64_t timestamp;             // 时间戳
};

// USB设备信息
struct UsbDevice {
    std::string path;               // 设备路径
    std::string bus_id;             // 总线ID
    uint32_t bus_number;            // 总线号
    uint32_t device_number;         // 设备号
    UsbSpeed speed;                 // 设备速度
    UsbDeviceDescriptor descriptor; // 设备描述符
    std::vector<uint8_t> config_descriptor; // 配置描述符
    bool is_connected;              // 连接状态
};

} // namespace protocol
} // namespace usb_redirector
