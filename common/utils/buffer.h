#pragma once

#include <vector>
#include <memory>
#include <cstdint>

namespace usb_redirector {
namespace utils {

class Buffer {
public:
    Buffer() = default;
    explicit Buffer(size_t size);
    Buffer(const uint8_t* data, size_t size);
    Buffer(const std::vector<uint8_t>& data);
    
    ~Buffer() = default;
    
    // 拷贝构造和赋值
    Buffer(const Buffer& other);
    Buffer& operator=(const Buffer& other);
    
    // 移动构造和赋值
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    
    // 数据访问
    uint8_t* Data() { return data_.data(); }
    const uint8_t* Data() const { return data_.data(); }
    size_t Size() const { return data_.size(); }
    bool Empty() const { return data_.empty(); }
    
    // 容量管理
    void Reserve(size_t capacity) { data_.reserve(capacity); }
    void Resize(size_t size) { data_.resize(size); }
    void Clear() { data_.clear(); }
    
    // 数据操作
    void Append(const uint8_t* data, size_t size);
    void Append(const std::vector<uint8_t>& data);
    void Append(const Buffer& other);
    
    void Prepend(const uint8_t* data, size_t size);
    void Prepend(const std::vector<uint8_t>& data);
    void Prepend(const Buffer& other);
    
    // 数据提取
    std::vector<uint8_t> Extract(size_t offset, size_t size) const;
    Buffer SubBuffer(size_t offset, size_t size) const;
    
    // 查找
    size_t Find(const uint8_t* pattern, size_t pattern_size, size_t start_pos = 0) const;
    size_t Find(const std::vector<uint8_t>& pattern, size_t start_pos = 0) const;
    
    // 操作符重载
    uint8_t& operator[](size_t index) { return data_[index]; }
    const uint8_t& operator[](size_t index) const { return data_[index]; }
    
    // 转换
    std::vector<uint8_t> ToVector() const { return data_; }
    std::string ToString() const;
    
    // 十六进制表示
    std::string ToHexString() const;

private:
    std::vector<uint8_t> data_;
};

} // namespace utils
} // namespace usb_redirector
