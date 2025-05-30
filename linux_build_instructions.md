# Linux接收端编译指南

## 快速开始

### 1. 安装依赖
```bash
# 自动安装依赖（推荐）
./install_deps_linux.sh

# 或手动安装（Ubuntu/Debian）
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config usbip linux-tools-generic
```

### 2. 编译接收端
```bash
# 运行编译脚本
./build_linux_receiver.sh
```

### 3. 运行接收端
```bash
# 加载内核模块
sudo modprobe vhci-hcd
sudo modprobe usbip-core

# 运行接收端
cd build_linux
sudo ./receiver/usb_receiver
```

## 系统要求

- Linux内核版本 >= 3.17 (支持USBIP)
- GCC >= 7.0 (支持C++17)
- CMake >= 3.16

## 手动编译步骤

如果自动脚本失败，可以手动编译：

```bash
# 创建构建目录
mkdir -p build_linux
cd build_linux

# 配置CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
make -j$(nproc) usb_receiver

# 运行测试
./tests/test_protocol
./tests/test_network
```

## 常见问题

### 编译错误
```bash
# 如果缺少头文件
sudo apt-get install linux-headers-$(uname -r)

# 如果CMake版本过低
sudo apt-get install cmake/backports  # Debian
```

### 运行时错误
```bash
# 权限不足
sudo ./receiver/usb_receiver

# USBIP模块未加载
sudo modprobe vhci-hcd usbip-core

# 检查模块状态
lsmod | grep -E "(vhci|usbip)"
```

### 网络问题
```bash
# 开放端口3240
sudo ufw allow 3240                    # Ubuntu
sudo firewall-cmd --add-port=3240/tcp  # CentOS
```

## 文件说明

- `build_linux_receiver.sh` - 主编译脚本
- `install_deps_linux.sh` - 依赖安装脚本
- `build_linux/receiver/usb_receiver` - 编译生成的接收端程序
