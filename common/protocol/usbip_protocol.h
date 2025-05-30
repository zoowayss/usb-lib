#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace usb_redirector {
namespace protocol {

// USBIP协议版本
constexpr uint16_t USBIP_VERSION = 0x0111;

// USBIP操作码
enum class UsbipOpCode : uint32_t {
    OP_REQUEST = 0x80000000,
    OP_REPLY = 0x00000000,
    
    OP_DEVLIST = 0x00000005,
    OP_IMPORT = 0x00000003,
    
    USBIP_CMD_SUBMIT = 0x00000001,
    USBIP_CMD_UNLINK = 0x00000002,
    USBIP_RET_SUBMIT = 0x00000003,
    USBIP_RET_UNLINK = 0x00000004
};

// USBIP协议头部
struct UsbipHeader {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
} __attribute__((packed));

// URB提交命令
struct UsbipCmdSubmit {
    UsbipHeader header;
    uint32_t transfer_flags;
    int32_t transfer_buffer_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t interval;
    uint64_t setup;
} __attribute__((packed));

// URB返回
struct UsbipRetSubmit {
    UsbipHeader header;
    int32_t status;
    int32_t actual_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t error_count;
} __attribute__((packed));

// 设备信息
struct UsbipDeviceInfo {
    char path[256];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
} __attribute__((packed));

class UsbipProtocol {
public:
    UsbipProtocol() = default;
    ~UsbipProtocol() = default;

    // 序列化和反序列化
    static std::vector<uint8_t> SerializeDeviceList(const std::vector<UsbipDeviceInfo>& devices);
    static std::vector<uint8_t> SerializeCmdSubmit(const UsbipCmdSubmit& cmd, const uint8_t* data = nullptr, size_t data_len = 0);
    static std::vector<uint8_t> SerializeRetSubmit(const UsbipRetSubmit& ret, const uint8_t* data = nullptr, size_t data_len = 0);
    
    static bool ParseHeader(const uint8_t* data, size_t len, UsbipHeader& header);
    static bool ParseCmdSubmit(const uint8_t* data, size_t len, UsbipCmdSubmit& cmd);
    static bool ParseRetSubmit(const uint8_t* data, size_t len, UsbipRetSubmit& ret);

    // 字节序转换
    static void HostToNetwork(UsbipHeader& header);
    static void NetworkToHost(UsbipHeader& header);
    static void HostToNetwork(UsbipCmdSubmit& cmd);
    static void NetworkToHost(UsbipCmdSubmit& cmd);
    static void HostToNetwork(UsbipRetSubmit& ret);
    static void NetworkToHost(UsbipRetSubmit& ret);

private:
    static uint32_t next_seqnum_;
};

} // namespace protocol
} // namespace usb_redirector
