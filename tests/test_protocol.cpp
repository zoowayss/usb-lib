#include <iostream>
#include <cassert>
#include <cstring>
#include "protocol/usbip_protocol.h"
#include "protocol/usb_types.h"
#include "utils/logger.h"

using namespace usb_redirector;

void TestUsbipProtocol() {
    std::cout << "Testing USBIP Protocol..." << std::endl;

    // 测试设备信息序列化
    std::vector<protocol::UsbipDeviceInfo> devices;
    protocol::UsbipDeviceInfo device = {};

    strcpy(device.path, "/dev/bus/usb/001/002");
    strcpy(device.busid, "1-2");
    device.busnum = 1;
    device.devnum = 2;
    device.speed = static_cast<uint32_t>(protocol::UsbSpeed::HIGH);
    device.idVendor = 0x1234;
    device.idProduct = 0x5678;
    device.bDeviceClass = static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE);

    devices.push_back(device);

    auto serialized = protocol::UsbipProtocol::SerializeDeviceList(devices);
    assert(!serialized.empty());

    std::cout << "Device list serialization: PASSED" << std::endl;

    // 测试URB命令序列化
    protocol::UsbipCmdSubmit cmd = {};
    cmd.header.command = static_cast<uint32_t>(protocol::UsbipOpCode::USBIP_CMD_SUBMIT);
    cmd.header.seqnum = 1;
    cmd.header.devid = 0;
    cmd.header.direction = static_cast<uint32_t>(protocol::UsbDirection::OUT);
    cmd.header.ep = 0;
    cmd.transfer_buffer_length = 8;

    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto cmd_serialized = protocol::UsbipProtocol::SerializeCmdSubmit(cmd, test_data.data(), test_data.size());
    assert(!cmd_serialized.empty());

    std::cout << "URB command serialization: PASSED" << std::endl;

    // 测试字节序转换
    protocol::UsbipHeader header = {};
    header.command = 0x12345678;
    header.seqnum = 0x87654321;

    protocol::UsbipProtocol::HostToNetwork(header);
    assert(header.command != 0x12345678); // 应该被转换

    protocol::UsbipProtocol::NetworkToHost(header);
    assert(header.command == 0x12345678); // 应该恢复原值

    std::cout << "Byte order conversion: PASSED" << std::endl;
}

void TestUsbTypes() {
    std::cout << "Testing USB Types..." << std::endl;

    // 测试USB设备描述符
    protocol::UsbDeviceDescriptor desc = {};
    desc.bLength = 18;
    desc.bDescriptorType = static_cast<uint8_t>(protocol::UsbDescriptorType::DEVICE);
    desc.idVendor = 0x1234;
    desc.idProduct = 0x5678;
    desc.bDeviceClass = static_cast<uint8_t>(protocol::UsbDeviceClass::MASS_STORAGE);

    assert(desc.bLength == 18);
    assert(desc.idVendor == 0x1234);

    std::cout << "USB device descriptor: PASSED" << std::endl;

    // 测试URB结构
    protocol::UsbUrb urb = {};
    urb.id = 123;
    urb.type = protocol::UsbTransferType::BULK;
    urb.direction = protocol::UsbDirection::IN;
    urb.endpoint = 0x81;
    urb.data = {0xAA, 0xBB, 0xCC, 0xDD};
    urb.actual_length = 4;

    assert(urb.id == 123);
    assert(urb.data.size() == 4);
    assert(urb.data[0] == 0xAA);

    std::cout << "URB structure: PASSED" << std::endl;
}

int main() {
    // 初始化日志
    utils::Logger::Instance().SetLogLevel(utils::LogLevel::INFO);
    utils::Logger::Instance().SetConsoleOutput(true);

    std::cout << "=== USB Redirector Protocol Tests ===" << std::endl;

    try {
        TestUsbipProtocol();
        TestUsbTypes();

        std::cout << "\nAll tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
