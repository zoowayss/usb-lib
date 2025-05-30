#include "usbip_protocol.h"
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace usb_redirector {
namespace protocol {

uint32_t UsbipProtocol::next_seqnum_ = 1;

std::vector<uint8_t> UsbipProtocol::SerializeDeviceList(const std::vector<UsbipDeviceInfo>& devices) {
    std::vector<uint8_t> buffer;
    
    // USBIP操作头
    uint32_t op_code = htonl(static_cast<uint32_t>(UsbipOpCode::OP_REPLY) | 
                            static_cast<uint32_t>(UsbipOpCode::OP_DEVLIST));
    uint32_t status = htonl(0);  // 成功
    uint32_t num_devices = htonl(static_cast<uint32_t>(devices.size()));
    
    buffer.resize(sizeof(op_code) + sizeof(status) + sizeof(num_devices));
    size_t offset = 0;
    
    std::memcpy(buffer.data() + offset, &op_code, sizeof(op_code));
    offset += sizeof(op_code);
    std::memcpy(buffer.data() + offset, &status, sizeof(status));
    offset += sizeof(status);
    std::memcpy(buffer.data() + offset, &num_devices, sizeof(num_devices));
    offset += sizeof(num_devices);
    
    // 设备信息
    for (const auto& device : devices) {
        size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(UsbipDeviceInfo));
        
        UsbipDeviceInfo net_device = device;
        // 转换字节序
        net_device.busnum = htonl(net_device.busnum);
        net_device.devnum = htonl(net_device.devnum);
        net_device.speed = htonl(net_device.speed);
        net_device.idVendor = htons(net_device.idVendor);
        net_device.idProduct = htons(net_device.idProduct);
        net_device.bcdDevice = htons(net_device.bcdDevice);
        
        std::memcpy(buffer.data() + old_size, &net_device, sizeof(UsbipDeviceInfo));
    }
    
    return buffer;
}

std::vector<uint8_t> UsbipProtocol::SerializeCmdSubmit(const UsbipCmdSubmit& cmd, 
                                                       const uint8_t* data, size_t data_len) {
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(UsbipCmdSubmit) + data_len);
    
    UsbipCmdSubmit net_cmd = cmd;
    HostToNetwork(net_cmd);
    
    std::memcpy(buffer.data(), &net_cmd, sizeof(UsbipCmdSubmit));
    if (data && data_len > 0) {
        std::memcpy(buffer.data() + sizeof(UsbipCmdSubmit), data, data_len);
    }
    
    return buffer;
}

std::vector<uint8_t> UsbipProtocol::SerializeRetSubmit(const UsbipRetSubmit& ret, 
                                                       const uint8_t* data, size_t data_len) {
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(UsbipRetSubmit) + data_len);
    
    UsbipRetSubmit net_ret = ret;
    HostToNetwork(net_ret);
    
    std::memcpy(buffer.data(), &net_ret, sizeof(UsbipRetSubmit));
    if (data && data_len > 0) {
        std::memcpy(buffer.data() + sizeof(UsbipRetSubmit), data, data_len);
    }
    
    return buffer;
}

bool UsbipProtocol::ParseHeader(const uint8_t* data, size_t len, UsbipHeader& header) {
    if (len < sizeof(UsbipHeader)) {
        return false;
    }
    
    std::memcpy(&header, data, sizeof(UsbipHeader));
    NetworkToHost(header);
    return true;
}

bool UsbipProtocol::ParseCmdSubmit(const uint8_t* data, size_t len, UsbipCmdSubmit& cmd) {
    if (len < sizeof(UsbipCmdSubmit)) {
        return false;
    }
    
    std::memcpy(&cmd, data, sizeof(UsbipCmdSubmit));
    NetworkToHost(cmd);
    return true;
}

bool UsbipProtocol::ParseRetSubmit(const uint8_t* data, size_t len, UsbipRetSubmit& ret) {
    if (len < sizeof(UsbipRetSubmit)) {
        return false;
    }
    
    std::memcpy(&ret, data, sizeof(UsbipRetSubmit));
    NetworkToHost(ret);
    return true;
}

void UsbipProtocol::HostToNetwork(UsbipHeader& header) {
    header.command = htonl(header.command);
    header.seqnum = htonl(header.seqnum);
    header.devid = htonl(header.devid);
    header.direction = htonl(header.direction);
    header.ep = htonl(header.ep);
}

void UsbipProtocol::NetworkToHost(UsbipHeader& header) {
    header.command = ntohl(header.command);
    header.seqnum = ntohl(header.seqnum);
    header.devid = ntohl(header.devid);
    header.direction = ntohl(header.direction);
    header.ep = ntohl(header.ep);
}

void UsbipProtocol::HostToNetwork(UsbipCmdSubmit& cmd) {
    HostToNetwork(cmd.header);
    cmd.transfer_flags = htonl(cmd.transfer_flags);
    cmd.transfer_buffer_length = htonl(cmd.transfer_buffer_length);
    cmd.start_frame = htonl(cmd.start_frame);
    cmd.number_of_packets = htonl(cmd.number_of_packets);
    cmd.interval = htonl(cmd.interval);
    // setup字段保持原样，因为它是8字节的setup包数据
}

void UsbipProtocol::NetworkToHost(UsbipCmdSubmit& cmd) {
    NetworkToHost(cmd.header);
    cmd.transfer_flags = ntohl(cmd.transfer_flags);
    cmd.transfer_buffer_length = ntohl(cmd.transfer_buffer_length);
    cmd.start_frame = ntohl(cmd.start_frame);
    cmd.number_of_packets = ntohl(cmd.number_of_packets);
    cmd.interval = ntohl(cmd.interval);
}

void UsbipProtocol::HostToNetwork(UsbipRetSubmit& ret) {
    HostToNetwork(ret.header);
    ret.status = htonl(ret.status);
    ret.actual_length = htonl(ret.actual_length);
    ret.start_frame = htonl(ret.start_frame);
    ret.number_of_packets = htonl(ret.number_of_packets);
    ret.error_count = htonl(ret.error_count);
}

void UsbipProtocol::NetworkToHost(UsbipRetSubmit& ret) {
    NetworkToHost(ret.header);
    ret.status = ntohl(ret.status);
    ret.actual_length = ntohl(ret.actual_length);
    ret.start_frame = ntohl(ret.start_frame);
    ret.number_of_packets = ntohl(ret.number_of_packets);
    ret.error_count = ntohl(ret.error_count);
}

} // namespace protocol
} // namespace usb_redirector
