# USB重定向网络配置指南

## 🌐 网络连接方案

由于macOS本地通常没有公网IP，以下是几种实用的网络连接解决方案：

## 方案1: 局域网直连 (最简单)

### 适用场景
- macOS和Linux在同一局域网内
- 家庭网络、办公室网络、同一WiFi

### 配置步骤

#### 1. 查看macOS IP地址
```bash
# 查看所有网络接口
ifconfig

# 或者只查看活动的IP地址
ifconfig | grep "inet " | grep -v 127.0.0.1

# 示例输出:
# inet 192.168.1.100 netmask 0xffffff00 broadcast 192.168.1.255
```

#### 2. 启动macOS发送端
```bash
# 默认监听所有接口
sudo ./build/sender/usb_sender

# 或指定绑定地址
sudo ./build/sender/usb_sender --bind 192.168.1.100
```

#### 3. Linux连接到macOS
```bash
# 连接到macOS的局域网IP
sudo ./build/receiver/usb_receiver --host 192.168.1.100 --port 3240
```

## 方案2: 反向连接 (推荐)

### 适用场景
- Linux有公网IP或固定IP
- macOS在NAT后面无法被直接访问
- 云服务器场景

### 配置步骤

#### 1. Linux启动服务器模式
```bash
# Linux作为服务器，监听连接
sudo ./build/receiver/usb_receiver --server-mode --port 3240

# 如果有公网IP，绑定到公网接口
sudo ./build/receiver/usb_receiver --server-mode --bind 0.0.0.0 --port 3240
```

#### 2. macOS主动连接Linux
```bash
# macOS连接到Linux服务器
sudo ./build/sender/usb_sender --reverse --host <Linux_IP> --port 3240

# 启用自动重连
sudo ./build/sender/usb_sender --reverse --host <Linux_IP> --auto-reconnect
```

## 方案3: SSH隧道

### 适用场景
- 需要加密传输
- 跨越复杂网络环境
- 已有SSH访问权限

### 配置步骤

#### 1. 建立SSH隧道 (macOS端)
```bash
# 将本地3240端口转发到Linux的3240端口
ssh -L 3240:localhost:3240 user@linux_server

# 后台运行
ssh -fN -L 3240:localhost:3240 user@linux_server
```

#### 2. 启动程序
```bash
# Linux端正常启动
sudo ./build/receiver/usb_receiver

# macOS连接到本地隧道
sudo ./build/sender/usb_sender --host 127.0.0.1 --port 3240
```

## 方案4: VPN连接

### 适用场景
- 跨地域网络
- 企业环境
- 需要安全连接

### 配置步骤

#### 1. 建立VPN连接
```bash
# 使用WireGuard、OpenVPN等建立VPN
# 确保macOS和Linux在同一VPN网段

# 查看VPN分配的IP
ip addr show wg0  # WireGuard
ip addr show tun0 # OpenVPN
```

#### 2. 使用VPN IP连接
```bash
# 使用VPN分配的IP地址连接
sudo ./build/receiver/usb_receiver --host 10.0.0.1 --port 3240
```

## 方案5: 内网穿透

### 适用场景
- 无法配置路由器端口转发
- 动态IP环境
- 临时访问需求

### 使用frp内网穿透

#### 1. 服务器端配置 (有公网IP的服务器)
```ini
# frps.ini
[common]
bind_port = 7000

[usb_redirect]
type = tcp
local_port = 3240
remote_port = 6000
```

#### 2. macOS客户端配置
```ini
# frpc.ini
[common]
server_addr = your_server_ip
server_port = 7000

[usb_redirect]
type = tcp
local_ip = 127.0.0.1
local_port = 3240
remote_port = 6000
```

#### 3. 启动服务
```bash
# 服务器端
./frps -c frps.ini

# macOS端
./frpc -c frpc.ini
sudo ./build/sender/usb_sender

# Linux端连接
sudo ./build/receiver/usb_receiver --host your_server_ip --port 6000
```

## 🔧 网络配置优化

### 防火墙设置

#### macOS防火墙
```bash
# 允许程序通过防火墙
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add ./build/sender/usb_sender
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblock ./build/sender/usb_sender
```

#### Linux防火墙
```bash
# Ubuntu/Debian
sudo ufw allow 3240

# CentOS/RHEL
sudo firewall-cmd --permanent --add-port=3240/tcp
sudo firewall-cmd --reload
```

### 网络性能优化

#### TCP缓冲区调优
```bash
# Linux系统优化
echo 'net.core.rmem_max = 16777216' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 16777216' >> /etc/sysctl.conf
echo 'net.ipv4.tcp_rmem = 4096 87380 16777216' >> /etc/sysctl.conf
echo 'net.ipv4.tcp_wmem = 4096 65536 16777216' >> /etc/sysctl.conf
sysctl -p
```

#### 网络延迟测试
```bash
# 测试网络延迟
ping <target_ip>

# 测试端口连通性
telnet <target_ip> 3240

# 测试带宽
iperf3 -s  # 服务器端
iperf3 -c <server_ip>  # 客户端
```

## 🚀 推荐配置

### 开发测试环境
```bash
# 方案1: 局域网直连
# 简单、快速、低延迟
macOS: sudo ./build/sender/usb_sender
Linux: sudo ./build/receiver/usb_receiver --host 192.168.1.100
```

### 生产环境
```bash
# 方案2: 反向连接 + 自动重连
# 稳定、可靠、自动恢复
Linux: sudo ./build/receiver/usb_receiver --server-mode
macOS: sudo ./build/sender/usb_sender --reverse --host <Linux_IP> --auto-reconnect
```

### 安全要求高的环境
```bash
# 方案3: SSH隧道
# 加密传输、安全可靠
ssh -L 3240:localhost:3240 user@linux_server
sudo ./build/sender/usb_sender --host 127.0.0.1
```

## 🔍 故障排除

### 连接问题诊断
```bash
# 1. 检查网络连通性
ping <target_ip>

# 2. 检查端口是否开放
nmap -p 3240 <target_ip>

# 3. 检查程序是否监听
netstat -an | grep 3240

# 4. 查看程序日志
sudo ./build/sender/usb_sender --log-level DEBUG
```

### 常见错误解决

1. **连接被拒绝**
   - 检查目标程序是否启动
   - 检查防火墙设置
   - 确认端口号正确

2. **连接超时**
   - 检查网络连通性
   - 确认IP地址正确
   - 检查路由配置

3. **频繁断线**
   - 启用自动重连
   - 检查网络稳定性
   - 调整心跳间隔

选择最适合您环境的网络配置方案，确保USB重定向程序能够稳定运行！
