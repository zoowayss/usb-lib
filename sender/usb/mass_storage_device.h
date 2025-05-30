#pragma once

#include "usb_device_manager.h"
#include "protocol/usb_types.h"
#include <memory>
#include <vector>
#include <functional>

namespace usb_redirector {
namespace sender {

// SCSI命令
enum class ScsiCommand : uint8_t {
    TEST_UNIT_READY = 0x00,
    REQUEST_SENSE = 0x03,
    INQUIRY = 0x12,
    READ_CAPACITY_10 = 0x25,
    READ_10 = 0x28,
    WRITE_10 = 0x2A,
    READ_CAPACITY_16 = 0x9E,
    READ_16 = 0x88,
    WRITE_16 = 0x8A
};

// USB大容量存储类特定请求
enum class MassStorageRequest : uint8_t {
    BULK_ONLY_MASS_STORAGE_RESET = 0xFF,
    GET_MAX_LUN = 0xFE
};

// 命令块包装器 (CBW)
struct CommandBlockWrapper {
    uint32_t dCBWSignature;      // 0x43425355 "USBC"
    uint32_t dCBWTag;            // 命令标签
    uint32_t dCBWDataTransferLength; // 数据传输长度
    uint8_t bmCBWFlags;          // 标志位
    uint8_t bCBWLUN;             // 逻辑单元号
    uint8_t bCBWCBLength;        // 命令块长度
    uint8_t CBWCB[16];           // 命令块
} __attribute__((packed));

// 命令状态包装器 (CSW)
struct CommandStatusWrapper {
    uint32_t dCSWSignature;      // 0x53425355 "USBS"
    uint32_t dCSWTag;            // 命令标签
    uint32_t dCSWDataResidue;    // 数据残留
    uint8_t bCSWStatus;          // 命令状态
} __attribute__((packed));

class MassStorageDevice {
public:
    using DataCallback = std::function<void(const protocol::UsbUrb& urb)>;
    
    explicit MassStorageDevice(std::shared_ptr<UsbDevice> device);
    ~MassStorageDevice();
    
    // 禁止拷贝
    MassStorageDevice(const MassStorageDevice&) = delete;
    MassStorageDevice& operator=(const MassStorageDevice&) = delete;
    
    // 设备操作
    bool Initialize();
    void Cleanup();
    
    // 设置数据回调
    void SetDataCallback(DataCallback callback) { data_callback_ = std::move(callback); }
    
    // 获取设备信息
    const protocol::UsbDevice& GetDeviceInfo() const { return device_->GetDeviceInfo(); }
    std::string GetPath() const { return device_->GetPath(); }
    std::string GetBusId() const { return device_->GetBusId(); }
    
    // 开始捕获URB数据
    bool StartCapture();
    void StopCapture();
    bool IsCapturing() const { return capturing_; }
    
    // 处理SCSI命令
    bool ProcessScsiCommand(const CommandBlockWrapper& cbw, std::vector<uint8_t>& response_data);
    
    // 获取设备容量信息
    bool GetCapacity(uint64_t& total_blocks, uint32_t& block_size);
    
    // 读写操作
    bool ReadBlocks(uint64_t start_block, uint32_t block_count, std::vector<uint8_t>& data);
    bool WriteBlocks(uint64_t start_block, uint32_t block_count, const std::vector<uint8_t>& data);

private:
    struct EndpointInfo {
        uint8_t address;
        uint16_t max_packet_size;
        bool is_in;
    };
    
    bool FindEndpoints();
    bool ResetDevice();
    bool GetMaxLun(uint8_t& max_lun);
    
    // SCSI命令处理
    bool HandleInquiry(std::vector<uint8_t>& response);
    bool HandleTestUnitReady();
    bool HandleRequestSense(std::vector<uint8_t>& response);
    bool HandleReadCapacity10(std::vector<uint8_t>& response);
    bool HandleReadCapacity16(std::vector<uint8_t>& response);
    bool HandleRead10(const uint8_t* cdb, std::vector<uint8_t>& response);
    bool HandleWrite10(const uint8_t* cdb, const std::vector<uint8_t>& data);
    
    // 传输操作
    bool SendCBW(const CommandBlockWrapper& cbw);
    bool ReceiveCSW(CommandStatusWrapper& csw, uint32_t expected_tag);
    bool TransferData(bool is_read, std::vector<uint8_t>& data, uint32_t length);
    
    // URB生成
    protocol::UsbUrb CreateControlUrb(uint8_t request_type, uint8_t request, 
                                     uint16_t value, uint16_t index, 
                                     const std::vector<uint8_t>& data = {});
    protocol::UsbUrb CreateBulkUrb(uint8_t endpoint, const std::vector<uint8_t>& data, 
                                  protocol::UsbDirection direction);
    
    std::shared_ptr<UsbDevice> device_;
    DataCallback data_callback_;
    
    bool initialized_;
    bool capturing_;
    int interface_number_;
    
    EndpointInfo bulk_in_endpoint_;
    EndpointInfo bulk_out_endpoint_;
    
    uint32_t next_tag_;
    uint64_t total_blocks_;
    uint32_t block_size_;
    
    mutable std::mutex mutex_;
};

} // namespace sender
} // namespace usb_redirector
