#include "buffer.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace usb_redirector {
namespace utils {

Buffer::Buffer(size_t size) : data_(size) {}

Buffer::Buffer(const uint8_t* data, size_t size) : data_(data, data + size) {}

Buffer::Buffer(const std::vector<uint8_t>& data) : data_(data) {}

Buffer::Buffer(const Buffer& other) : data_(other.data_) {}

Buffer& Buffer::operator=(const Buffer& other) {
    if (this != &other) {
        data_ = other.data_;
    }
    return *this;
}

Buffer::Buffer(Buffer&& other) noexcept : data_(std::move(other.data_)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        data_ = std::move(other.data_);
    }
    return *this;
}

void Buffer::Append(const uint8_t* data, size_t size) {
    data_.insert(data_.end(), data, data + size);
}

void Buffer::Append(const std::vector<uint8_t>& data) {
    data_.insert(data_.end(), data.begin(), data.end());
}

void Buffer::Append(const Buffer& other) {
    data_.insert(data_.end(), other.data_.begin(), other.data_.end());
}

void Buffer::Prepend(const uint8_t* data, size_t size) {
    data_.insert(data_.begin(), data, data + size);
}

void Buffer::Prepend(const std::vector<uint8_t>& data) {
    data_.insert(data_.begin(), data.begin(), data.end());
}

void Buffer::Prepend(const Buffer& other) {
    data_.insert(data_.begin(), other.data_.begin(), other.data_.end());
}

std::vector<uint8_t> Buffer::Extract(size_t offset, size_t size) const {
    if (offset >= data_.size()) {
        return {};
    }
    
    size_t actual_size = std::min(size, data_.size() - offset);
    return std::vector<uint8_t>(data_.begin() + offset, data_.begin() + offset + actual_size);
}

Buffer Buffer::SubBuffer(size_t offset, size_t size) const {
    auto extracted = Extract(offset, size);
    return Buffer(extracted);
}

size_t Buffer::Find(const uint8_t* pattern, size_t pattern_size, size_t start_pos) const {
    if (pattern_size == 0 || start_pos >= data_.size()) {
        return std::string::npos;
    }
    
    auto it = std::search(data_.begin() + start_pos, data_.end(),
                         pattern, pattern + pattern_size);
    
    if (it == data_.end()) {
        return std::string::npos;
    }
    
    return std::distance(data_.begin(), it);
}

size_t Buffer::Find(const std::vector<uint8_t>& pattern, size_t start_pos) const {
    return Find(pattern.data(), pattern.size(), start_pos);
}

std::string Buffer::ToString() const {
    return std::string(data_.begin(), data_.end());
}

std::string Buffer::ToHexString() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < data_.size(); ++i) {
        if (i > 0) {
            oss << " ";
        }
        oss << std::setw(2) << static_cast<unsigned>(data_[i]);
    }
    
    return oss.str();
}

} // namespace utils
} // namespace usb_redirector
