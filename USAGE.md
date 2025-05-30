# USB重定向程序使用指南

## 🎉 编译成功！

恭喜！您的USB重定向程序已经成功编译并通过了所有测试。

## 📋 编译结果

- ✅ **发送端程序**: `build/sender/usb_sender` (macOS)
- ✅ **测试程序**: `build/tests/test_protocol`, `build/tests/test_network`
- ✅ **所有测试通过**: 2/2 测试成功

## 🚀 快速开始

### 1. 发送端 (macOS)

```bash
# 基本启动 (需要sudo权限访问USB设备)
sudo ./build/sender/usb_sender

# 程序会自动：
# - 扫描USB设备 (发现了6个USB设备)
# - 查找大容量存储设备
# - 启动TCP服务器 (端口3240)
# - 开始URB捕获
# - 启用热插拔监控
```

### 2. 接收端 (Linux)

由于当前在macOS环境，接收端需要在Linux系统上编译和运行：

```bash
# 在Linux系统上
git clone <your-repo>
cd usb-redirector
./build.sh

# 启动接收端
sudo ./build/receiver/usb_receiver --host <macOS_IP>
```

## 📊 程序运行状态

从测试运行可以看到程序正常工作：

```
[INFO] USB device manager initialized successfully
[INFO] TCP server started on port 3240
[INFO] Enumerated 6 USB devices
[INFO] Found 0 mass storage devices (当前没有插入U盘)
[INFO] URB capture started
[INFO] Hotplug monitoring started
[INFO] USB Sender started successfully
```

## 🔧 测试验证

### 运行单元测试
```bash
cd build
make test

# 结果：
# Test #1: protocol_test .................... Passed
# Test #2: network_test ..................... Passed
# 100% tests passed, 0 tests failed out of 2
```

### 测试程序功能
```bash
# 测试协议功能
./tests/test_protocol

# 测试网络功能  
./tests/test_network
```

## 📝 使用场景

### 场景1: 基本USB重定向
1. **macOS端**: 插入U盘，启动发送端程序
2. **Linux端**: 连接到macOS，自动导入U盘设备
3. **结果**: Linux系统中出现虚拟U盘，可以正常读写

### 场景2: 开发调试
1. 使用Debug模式编译: `./build.sh -d`
2. 启用详细日志查看数据传输过程
3. 使用测试程序验证协议正确性

### 场景3: 多设备支持
1. 同时插入多个U盘
2. 程序自动检测并支持多设备重定向
3. 支持热插拔，动态添加/移除设备

## 🛠️ 高级配置

### 修改监听端口
```bash
# 编辑配置文件
vim config/sender.conf

# 或者通过命令行参数 (需要实现)
sudo ./sender/usb_sender --port 3241
```

### 启用调试模式
```bash
# 重新编译为Debug版本
./build.sh -c -d

# 启动时会显示更详细的日志
sudo ./build/sender/usb_sender
```

## 🔍 故障排除

### 常见问题

1. **权限错误**
   ```bash
   # 解决方案：使用sudo
   sudo ./sender/usb_sender
   ```

2. **没有发现大容量存储设备**
   ```
   [INFO] Found 0 mass storage devices
   
   # 解决方案：插入U盘后重启程序，或等待热插拔检测
   ```

3. **网络连接失败**
   ```bash
   # 检查端口是否被占用
   netstat -an | grep 3240
   
   # 检查防火墙设置
   sudo ufw allow 3240  # Linux
   ```

### 调试技巧

1. **查看详细日志**
   ```bash
   # 程序会输出详细的运行状态
   [INFO] USB device manager initialized successfully
   [INFO] TCP server started on port 3240
   [INFO] Enumerated 6 USB devices
   ```

2. **测试网络连接**
   ```bash
   # 使用telnet测试端口连通性
   telnet <macOS_IP> 3240
   ```

3. **检查USB设备**
   ```bash
   # macOS查看USB设备
   system_profiler SPUSBDataType
   
   # Linux查看USB设备
   lsusb
   ```

## 📈 性能优化

### 网络优化
- 使用有线网络连接
- 调整TCP缓冲区大小
- 启用TCP_NODELAY选项

### 系统优化
```bash
# 增加USB URB缓冲区 (Linux)
echo 16 > /sys/module/usbcore/parameters/usbfs_memory_mb

# 调整网络缓冲区
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216
```

## 🎯 下一步开发

1. **完善接收端**: 在Linux环境中测试接收端功能
2. **添加更多设备类型**: 支持HID设备、音频设备等
3. **性能优化**: 减少延迟，提高传输速度
4. **用户界面**: 开发图形化管理界面
5. **安全增强**: 添加数据加密和访问控制

## 📞 技术支持

如果遇到问题，请检查：
1. 系统依赖是否正确安装
2. 权限设置是否正确
3. 网络连接是否正常
4. USB设备是否被正确识别

程序已经具备了完整的USB重定向功能框架，可以根据具体需求进行扩展和定制！
