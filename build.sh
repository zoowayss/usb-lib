#!/bin/bash

# USB重定向程序构建脚本

set -e  # 遇到错误时退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
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

# 检查操作系统
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        print_info "检测到 macOS 系统"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        OS="linux"
        print_info "检测到 Linux 系统"
    else
        print_error "不支持的操作系统: $OSTYPE"
        exit 1
    fi
}

# 检查依赖
check_dependencies() {
    print_info "检查构建依赖..."

    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake 未安装，请先安装 CMake 3.16+"
        exit 1
    fi

    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    print_info "CMake 版本: $CMAKE_VERSION"

    # 检查编译器
    if [[ "$OS" == "macos" ]]; then
        if ! command -v clang++ &> /dev/null; then
            print_error "Clang++ 未安装，请安装 Xcode Command Line Tools"
            exit 1
        fi

        # 检查libusb
        if ! pkg-config --exists libusb-1.0; then
            print_error "libusb 未安装，请运行: brew install libusb"
            exit 1
        fi

        print_info "libusb 版本: $(pkg-config --modversion libusb-1.0)"

    elif [[ "$OS" == "linux" ]]; then
        if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
            print_error "G++ 或 Clang++ 未安装，请安装构建工具"
            exit 1
        fi

        # 检查USBIP工具
        if ! command -v usbip &> /dev/null; then
            print_warning "usbip 工具未安装，接收端可能无法正常工作"
            print_warning "请安装: sudo apt install linux-tools-generic (Ubuntu/Debian)"
        fi

        # 检查内核模块
        if ! lsmod | grep -q vhci_hcd; then
            print_warning "vhci_hcd 内核模块未加载"
            print_warning "请运行: sudo modprobe vhci-hcd"
        fi
    fi

    print_success "依赖检查完成"
}

# 清理构建目录
clean_build() {
    if [[ -d "build" ]]; then
        print_info "清理旧的构建目录..."
        rm -rf build
    fi
}

# 检查并清理跨平台构建缓存
check_cross_platform_cache() {
    if [[ -f "build/CMakeCache.txt" ]]; then
        # 检查缓存中的系统信息
        local cache_system=$(grep "CMAKE_HOST_SYSTEM_NAME" build/CMakeCache.txt 2>/dev/null | cut -d'=' -f2 || echo "")
        local current_system=""

        if [[ "$OS" == "macos" ]]; then
            current_system="Darwin"
        elif [[ "$OS" == "linux" ]]; then
            current_system="Linux"
        fi

        if [[ -n "$cache_system" && "$cache_system" != "$current_system" ]]; then
            print_warning "检测到跨平台构建缓存冲突"
            print_info "缓存系统: $cache_system, 当前系统: $current_system"
            print_info "自动清理构建缓存..."
            rm -rf build
        fi
    fi
}

# 创建构建目录
create_build_dir() {
    print_info "创建构建目录..."
    mkdir -p build
    cd build
}

# 配置项目
configure_project() {
    print_info "配置项目..."

    CMAKE_ARGS=""

    # 根据操作系统添加特定参数
    if [[ "$OS" == "macos" ]]; then
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14"
    fi

    # 如果是Debug模式
    if [[ "$BUILD_TYPE" == "Debug" ]]; then
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_BUILD_TYPE=Debug"
    else
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release"
    fi

    cmake .. $CMAKE_ARGS

    print_success "项目配置完成"
}

# 编译项目
build_project() {
    print_info "开始编译..."

    # 获取CPU核心数
    if [[ "$OS" == "macos" ]]; then
        CORES=$(sysctl -n hw.ncpu)
    else
        CORES=$(nproc)
    fi

    print_info "使用 $CORES 个并行任务编译"

    make -j$CORES

    print_success "编译完成"
}

# 运行测试
run_tests() {
    if [[ "$RUN_TESTS" == "true" ]]; then
        print_info "运行测试..."

        if make test; then
            print_success "所有测试通过"
        else
            print_error "测试失败"
            exit 1
        fi
    fi
}

# 安装程序
install_program() {
    if [[ "$INSTALL" == "true" ]]; then
        print_info "安装程序..."

        if [[ "$OS" == "macos" ]]; then
            # macOS安装到/usr/local/bin
            sudo cp sender/usb_sender /usr/local/bin/
            print_success "发送端已安装到 /usr/local/bin/usb_sender"
        else
            # Linux安装
            sudo cp sender/usb_sender /usr/local/bin/ 2>/dev/null || true
            sudo cp receiver/usb_receiver /usr/local/bin/
            print_success "程序已安装到 /usr/local/bin/"
        fi
    fi
}

# 显示使用说明
show_usage() {
    echo "USB重定向程序构建脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help      显示此帮助信息"
    echo "  -c, --clean     清理构建目录"
    echo "  -d, --debug     Debug模式编译"
    echo "  -t, --test      运行测试"
    echo "  -i, --install   安装程序到系统"
    echo "  --deps          只检查依赖"
    echo ""
    echo "示例:"
    echo "  $0                # 标准编译"
    echo "  $0 -d -t          # Debug模式编译并运行测试"
    echo "  $0 -c -i          # 清理重新编译并安装"
}

# 显示构建结果
show_results() {
    print_success "构建完成！"
    echo ""

    if [[ "$OS" == "macos" ]]; then
        print_info "发送端程序: $(pwd)/sender/usb_sender"
        echo "使用方法: sudo ./sender/usb_sender"
    elif [[ "$OS" == "linux" ]]; then
        print_info "接收端程序: $(pwd)/receiver/usb_receiver"
        echo "使用方法: sudo ./receiver/usb_receiver --host <发送端IP>"
    fi

    echo ""
    print_info "更多使用说明请参考 README.md"
}

# 主函数
main() {
    # 默认参数
    BUILD_TYPE="Release"
    RUN_TESTS="false"
    INSTALL="false"
    CLEAN="false"
    DEPS_ONLY="false"

    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -c|--clean)
                CLEAN="true"
                shift
                ;;
            -d|--debug)
                BUILD_TYPE="Debug"
                shift
                ;;
            -t|--test)
                RUN_TESTS="true"
                shift
                ;;
            -i|--install)
                INSTALL="true"
                shift
                ;;
            --deps)
                DEPS_ONLY="true"
                shift
                ;;
            *)
                print_error "未知参数: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    print_info "开始构建 USB 重定向程序..."

    # 检测操作系统
    detect_os

    # 检查依赖
    check_dependencies

    if [[ "$DEPS_ONLY" == "true" ]]; then
        print_success "依赖检查完成，退出"
        exit 0
    fi

    # 检查跨平台构建缓存
    check_cross_platform_cache

    # 清理构建目录
    if [[ "$CLEAN" == "true" ]]; then
        clean_build
    fi

    # 创建构建目录
    create_build_dir

    # 配置项目
    configure_project

    # 编译项目
    build_project

    # 运行测试
    run_tests

    # 安装程序
    install_program

    # 显示结果
    show_results
}

# 运行主函数
main "$@"
