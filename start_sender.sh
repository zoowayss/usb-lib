#!/bin/bash

# USB重定向发送端启动脚本 (macOS)

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查是否为root权限
check_root() {
    if [[ $EUID -ne 0 ]]; then
        print_error "此程序需要root权限访问USB设备"
        print_info "请使用: sudo $0"
        exit 1
    fi
}

# 检查程序是否存在
check_program() {
    if [[ ! -f "build/sender/usb_sender" ]]; then
        print_error "找不到发送端程序"
        print_info "请先运行: ./build.sh"
        exit 1
    fi
}

# 获取本机IP地址
get_local_ip() {
    # 获取活动的网络接口IP
    local_ip=$(ifconfig | grep "inet " | grep -v 127.0.0.1 | head -1 | awk '{print $2}')
    
    if [[ -z "$local_ip" ]]; then
        print_warning "无法自动获取本机IP地址"
        local_ip="127.0.0.1"
    fi
    
    echo "$local_ip"
}

# 显示网络信息
show_network_info() {
    print_info "网络配置信息:"
    echo "----------------------------------------"
    
    # 显示所有网络接口
    print_info "可用的网络接口:"
    ifconfig | grep "inet " | grep -v 127.0.0.1 | while read line; do
        ip=$(echo $line | awk '{print $2}')
        interface=$(echo $line | awk '{print $NF}' | sed 's/.*%//')
        echo "  $ip"
    done
    
    echo "----------------------------------------"
    print_info "推荐的连接方式:"
    echo "  1. 局域网直连: Linux端使用 --host $local_ip"
    echo "  2. SSH隧道: ssh -L 3240:localhost:3240 user@linux_server"
    echo "  3. VPN连接: 使用VPN分配的IP地址"
    echo "----------------------------------------"
}

# 检查端口是否被占用
check_port() {
    local port=${1:-3240}
    
    if lsof -i :$port >/dev/null 2>&1; then
        print_warning "端口 $port 已被占用"
        print_info "正在使用端口 $port 的进程:"
        lsof -i :$port
        echo ""
        read -p "是否继续启动? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# 显示使用帮助
show_help() {
    echo "USB重定向发送端启动脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help          显示此帮助信息"
    echo "  -p, --port <port>   指定监听端口 (默认: 3240)"
    echo "  -b, --bind <addr>   指定绑定地址 (默认: 0.0.0.0)"
    echo "  -i, --info          只显示网络信息"
    echo "  -q, --quiet         静默模式"
    echo "  --reverse <host>    反向连接模式"
    echo "  --auto-reconnect    启用自动重连"
    echo ""
    echo "示例:"
    echo "  $0                           # 标准模式启动"
    echo "  $0 -p 3241                   # 使用端口3241"
    echo "  $0 --reverse 192.168.1.100   # 反向连接到Linux服务器"
    echo "  $0 --info                    # 只显示网络信息"
}

# 启动程序
start_program() {
    local args=()
    
    # 构建启动参数
    if [[ -n "$port" ]]; then
        args+=(--port "$port")
    fi
    
    if [[ -n "$bind_addr" ]]; then
        args+=(--bind "$bind_addr")
    fi
    
    if [[ -n "$reverse_host" ]]; then
        args+=(--reverse --host "$reverse_host")
        if [[ "$auto_reconnect" == "true" ]]; then
            args+=(--auto-reconnect)
        fi
    fi
    
    print_info "启动USB重定向发送端..."
    print_info "监听地址: ${bind_addr:-0.0.0.0}:${port:-3240}"
    
    if [[ -n "$reverse_host" ]]; then
        print_info "反向连接到: $reverse_host"
    fi
    
    print_info "按 Ctrl+C 停止程序"
    echo ""
    
    # 启动程序
    exec ./build/sender/usb_sender "${args[@]}"
}

# 主函数
main() {
    # 默认参数
    port=""
    bind_addr=""
    reverse_host=""
    auto_reconnect="false"
    quiet="false"
    info_only="false"
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -p|--port)
                if [[ -n "$2" && "$2" != -* ]]; then
                    port="$2"
                    shift 2
                else
                    print_error "--port 需要一个参数"
                    exit 1
                fi
                ;;
            -b|--bind)
                if [[ -n "$2" && "$2" != -* ]]; then
                    bind_addr="$2"
                    shift 2
                else
                    print_error "--bind 需要一个参数"
                    exit 1
                fi
                ;;
            -i|--info)
                info_only="true"
                shift
                ;;
            -q|--quiet)
                quiet="true"
                shift
                ;;
            --reverse)
                if [[ -n "$2" && "$2" != -* ]]; then
                    reverse_host="$2"
                    shift 2
                else
                    print_error "--reverse 需要一个主机地址参数"
                    exit 1
                fi
                ;;
            --auto-reconnect)
                auto_reconnect="true"
                shift
                ;;
            *)
                print_error "未知参数: $1"
                show_help
                exit 1
                ;;
        esac
    done
    
    # 获取本机IP
    local_ip=$(get_local_ip)
    
    # 如果只显示信息
    if [[ "$info_only" == "true" ]]; then
        show_network_info
        exit 0
    fi
    
    # 检查权限
    check_root
    
    # 检查程序
    check_program
    
    # 显示网络信息
    if [[ "$quiet" != "true" ]]; then
        show_network_info
        echo ""
    fi
    
    # 检查端口
    check_port "${port:-3240}"
    
    # 启动程序
    start_program
}

# 信号处理
trap 'print_info "正在停止程序..."; exit 0' INT TERM

# 运行主函数
main "$@"
