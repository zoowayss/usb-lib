#!/bin/bash

# Linux接收端编译脚本
# 用于在Linux环境下编译USB重定向接收端

set -e

echo "=== USB Redirector Linux Receiver 编译脚本 ==="

# 检查操作系统
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "错误: 此脚本只能在Linux系统上运行"
    echo "当前系统: $OSTYPE"
    exit 1
fi

# 检查必要的依赖
echo "检查编译依赖..."

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到cmake"
    echo "请安装: sudo apt-get install cmake (Ubuntu/Debian)"
    echo "或者: sudo yum install cmake (CentOS/RHEL)"
    exit 1
fi

# 检查编译器
if ! command -v g++ &> /dev/null; then
    echo "错误: 未找到g++编译器"
    echo "请安装: sudo apt-get install build-essential (Ubuntu/Debian)"
    echo "或者: sudo yum groupinstall 'Development Tools' (CentOS/RHEL)"
    exit 1
fi

# 检查pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo "错误: 未找到pkg-config"
    echo "请安装: sudo apt-get install pkg-config (Ubuntu/Debian)"
    echo "或者: sudo yum install pkgconfig (CentOS/RHEL)"
    exit 1
fi

echo "✓ 编译依赖检查通过"

# 创建构建目录
BUILD_DIR="build_linux"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "配置CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17

echo "开始编译..."
make -j$(nproc) usb_receiver

# 检查编译结果
if [ -f "receiver/usb_receiver" ]; then
    echo "✓ 编译成功!"
    echo "接收端可执行文件: $PWD/receiver/usb_receiver"

    # 显示文件信息
    echo ""
    echo "文件信息:"
    ls -la receiver/usb_receiver

    # 检查依赖
    echo ""
    echo "动态库依赖:"
    ldd receiver/usb_receiver

else
    echo "✗ 编译失败!"
    exit 1
fi

# 编译测试程序
echo ""
echo "编译测试程序..."
if make -j$(nproc) test_protocol test_network; then
    echo "✓ 测试程序编译成功"

    echo ""
    echo "运行测试..."
    ./tests/test_protocol && echo "✓ 协议测试通过"
    ./tests/test_network && echo "✓ 网络测试通过"
else
    echo "✗ 测试程序编译失败"
fi

echo ""
echo "=== 编译完成 ==="
echo ""
echo "下一步操作:"
echo "1. 加载USBIP内核模块:"
echo "   sudo modprobe vhci-hcd"
echo "   sudo modprobe usbip-core"
echo ""
echo "2. 运行接收端:"
echo "   sudo ./receiver/usb_receiver"
echo ""
echo "注意: 接收端需要root权限来操作USBIP内核接口"
