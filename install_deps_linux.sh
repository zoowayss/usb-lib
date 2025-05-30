#!/bin/bash

# Linux依赖安装脚本
# 自动检测Linux发行版并安装编译依赖

set -e

echo "=== USB Redirector Linux 依赖安装脚本 ==="

# 检查是否为root用户
if [[ $EUID -eq 0 ]]; then
    echo "注意: 正在以root用户运行"
else
    echo "注意: 需要sudo权限来安装软件包"
fi

# 检测Linux发行版
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$NAME
    VER=$VERSION_ID
elif type lsb_release >/dev/null 2>&1; then
    OS=$(lsb_release -si)
    VER=$(lsb_release -sr)
else
    OS=$(uname -s)
    VER=$(uname -r)
fi

echo "检测到系统: $OS $VER"

# 根据发行版安装依赖
case $OS in
    "Ubuntu"*|"Debian"*)
        echo "安装Ubuntu/Debian依赖..."
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            pkg-config \
            usbip \
            linux-tools-generic
        ;;
    "CentOS"*|"Red Hat"*|"Rocky"*|"AlmaLinux"*)
        echo "安装CentOS/RHEL依赖..."
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y \
            cmake \
            pkgconfig \
            usbip-utils
        ;;
    "Fedora"*)
        echo "安装Fedora依赖..."
        sudo dnf groupinstall -y "Development Tools"
        sudo dnf install -y \
            cmake \
            pkgconfig \
            usbip-utils
        ;;
    *)
        echo "警告: 未识别的Linux发行版: $OS"
        echo "请手动安装以下依赖:"
        echo "- build-essential (gcc, g++, make)"
        echo "- cmake"
        echo "- pkg-config"
        echo "- usbip-utils"
        exit 1
        ;;
esac

echo "✓ 依赖安装完成"

# 检查USBIP内核模块
echo ""
echo "检查USBIP内核模块..."
if modinfo vhci-hcd &>/dev/null; then
    echo "✓ vhci-hcd模块可用"
else
    echo "⚠ vhci-hcd模块不可用，可能需要安装linux-headers"
fi

if modinfo usbip-core &>/dev/null; then
    echo "✓ usbip-core模块可用"
else
    echo "⚠ usbip-core模块不可用"
fi

echo ""
echo "=== 依赖安装完成 ==="
echo ""
echo "现在可以运行编译脚本:"
echo "./build_linux_receiver.sh"
