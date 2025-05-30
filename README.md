# USB重定向程序

基于C++17开发的USB重定向程序，使用libusb框架抓取USB协议数据，将URB数据封装为USBIP协议通过TCP网络传输，在接收端注入到虚拟USB设备中。

## 功能特性

- **发送端(macOS)**: 使用libusb捕获USB设备数据，专门支持大容量存储设备(U盘)
- **接收端(Linux)**: 使用USBIP内核模块创建虚拟USB设备
- **USBIP协议兼容**: 完整实现USBIP v1.1.1协议栈
- **网络传输**: 基于TCP的可靠数据传输，支持断线重连和心跳检测
- **设备热插拔**: 支持USB设备的热插拔检测和处理
- **多设备支持**: 可同时重定向多个USB设备

## 系统架构

```
发送端 (macOS)                    接收端 (Linux)
┌─────────────────┐               ┌─────────────────┐
│   USB设备       │               │  虚拟USB设备    │
│      ↕          │               │      ↕          │
│  libusb捕获     │               │  USBIP内核模块  │
│      ↓          │               │      ↑          │
│  URB数据处理    │               │  URB数据注入    │
│      ↓          │               │      ↑          │
│  USBIP协议封装  │ ============> │  USBIP协议解析  │
│      ↓          │   TCP网络     │      ↑          │
│  网络发送       │               │  网络接收       │
└─────────────────┘               └─────────────────┘
```

## 编译要求

### 发送端 (macOS)
- macOS 10.14+
- Xcode Command Line Tools
- CMake 3.16+
- libusb 1.0
- IOKit框架

### 接收端 (Linux)
- Linux内核 3.17+ (支持USBIP)
- GCC 7+ 或 Clang 6+
- CMake 3.16+
- USBIP内核模块

## 安装依赖

### macOS (发送端)
```bash
# 安装Homebrew (如果未安装)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install cmake libusb
```

### Linux (接收端)
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install cmake build-essential linux-tools-generic

# CentOS/RHEL
sudo yum install cmake gcc-c++ kernel-devel
sudo yum install usbip-utils

# 加载USBIP内核模块
sudo modprobe vhci-hcd
```

## 编译安装

```bash
# 克隆代码
git clone <repository-url>
cd usb-redirector

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make -j$(nproc)

# 运行测试
make test
```

## 使用方法

### 1. 启动发送端 (macOS)

```bash
# 基本启动
sudo ./sender/usb_sender

# 指定端口
sudo ./sender/usb_sender --port 3240
```

**注意**: 需要sudo权限访问USB设备

### 2. 启动接收端 (Linux)

```bash
# 连接到本地发送端
sudo ./receiver/usb_receiver

# 连接到远程发送端
sudo ./receiver/usb_receiver --host 192.168.1.100 --port 3240

# 列出可用设备
sudo ./receiver/usb_receiver --list

# 导入特定设备
sudo ./receiver/usb_receiver --import 1-2
```

### 3. 验证设备重定向

在接收端检查虚拟设备：
```bash
# 查看USB设备
lsusb

# 查看块设备
lsblk

# 挂载U盘 (如果是存储设备)
sudo mkdir /mnt/usb
sudo mount /dev/sdb1 /mnt/usb
```

## 配置选项

### 发送端配置
- `--port <port>`: 监听端口 (默认: 3240)
- `--bind <addr>`: 绑定地址 (默认: 0.0.0.0)
- `--log-level <level>`: 日志级别 (DEBUG/INFO/WARNING/ERROR)
- `--log-file <file>`: 日志文件路径

### 接收端配置
- `--host <host>`: 发送端地址 (默认: 127.0.0.1)
- `--port <port>`: 发送端端口 (默认: 3240)
- `--list`: 列出可用设备
- `--import <bus_id>`: 导入指定设备
- `--auto-import`: 自动导入所有大容量存储设备

## 支持的设备类型

当前版本主要支持：
- **大容量存储设备**: U盘、移动硬盘、SD卡读卡器等
- **USB 2.0/3.0设备**: 高速和超高速设备
- **标准SCSI命令**: INQUIRY, READ CAPACITY, READ/WRITE等

## 故障排除

### 常见问题

1. **权限错误**
   ```bash
   # macOS
   sudo ./usb_sender
   
   # Linux
   sudo ./usb_receiver
   ```

2. **libusb错误**
   ```bash
   # 检查libusb安装
   pkg-config --modversion libusb-1.0
   
   # 重新安装libusb
   brew reinstall libusb  # macOS
   ```

3. **USBIP模块未加载**
   ```bash
   # 加载内核模块
   sudo modprobe vhci-hcd
   
   # 检查模块状态
   lsmod | grep vhci
   ```

4. **网络连接问题**
   ```bash
   # 检查端口占用
   netstat -an | grep 3240
   
   # 检查防火墙设置
   sudo ufw allow 3240  # Ubuntu
   ```

### 调试模式

启用详细日志：
```bash
# 发送端
sudo ./usb_sender --log-level DEBUG

# 接收端
sudo ./usb_receiver --log-level DEBUG
```

## 性能优化

### 网络优化
- 使用有线网络连接
- 调整TCP缓冲区大小
- 启用TCP_NODELAY选项

### 系统优化
```bash
# 增加USB URB缓冲区
echo 16 > /sys/module/usbcore/parameters/usbfs_memory_mb

# 调整网络缓冲区
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216
```

## 开发指南

### 项目结构
```
usb-redirector/
├── common/           # 公共模块
│   ├── protocol/     # USBIP协议实现
│   ├── network/      # 网络通信
│   └── utils/        # 工具类
├── sender/           # 发送端(macOS)
│   ├── usb/          # USB设备管理
│   └── capture/      # URB数据捕获
├── receiver/         # 接收端(Linux)
│   ├── usbip/        # USBIP客户端
│   └── virtual_device/ # 虚拟设备管理
└── tests/            # 测试代码
```

### 添加新设备类型

1. 在`protocol/usb_types.h`中添加设备类定义
2. 在发送端实现设备特定的URB捕获逻辑
3. 在接收端实现对应的虚拟设备处理
4. 添加相应的测试用例

### 扩展协议支持

1. 修改`protocol/usbip_protocol.h`添加新的消息类型
2. 实现序列化和反序列化方法
3. 在网络层添加消息处理逻辑
4. 更新文档和测试

## 许可证

本项目采用MIT许可证，详见LICENSE文件。

## 贡献指南

1. Fork本项目
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建Pull Request

## 联系方式

如有问题或建议，请通过以下方式联系：
- 提交Issue
- 发送邮件至: [your-email@example.com]

## 更新日志

### v1.0.0 (当前版本)
- 实现基本的USB重定向功能
- 支持macOS发送端和Linux接收端
- 支持大容量存储设备
- 实现USBIP协议栈
- 添加网络通信和错误处理
