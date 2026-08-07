#!/bin/bash
#
# iov-vehicle-cgw-framework 构建脚本
#
# 用法: ./scripts/build.sh [选项]
#
# 选项:
#   --clean          清理构建目录后重新构建
#   --no-test        跳过测试步骤
#   --install        构建后安装到 INSTALL_PREFIX
#   --prefix <DIR>   安装前缀（默认取 scripts/cgw-env.sh 中的 CGW_PREFIX）
#   --release        Release 模式构建（默认 Debug）
#   --help           显示帮助信息
#
# 安装前缀的单一来源是 scripts/cgw-env.sh：
#   本地开发：  ./scripts/build.sh --install                 # -> ${HOME}/.local
#   生产/CI：   CGW_PREFIX=/opt/cgw ./scripts/build.sh --install
#
# 注意：CGWFrameworkConfig.cmake 需要在 configure 阶段就知道前缀，
#       因此前缀通过 -DCMAKE_INSTALL_PREFIX 传入，不能只在 install 阶段指定。
#

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# 构建环境单一来源：提供 CGW_PREFIX / CGWFramework_DIR
source "${PROJECT_ROOT}/scripts/cgw-env.sh"

# 默认选项
CLEAN_BUILD=false
RUN_TESTS=true
INSTALL_AFTER_BUILD=false
BUILD_TYPE="Debug"
# 安装前缀来自 cgw-env.sh（CGW_PREFIX），--prefix 可覆盖
INSTALL_PREFIX="${CGW_PREFIX}"

# 显示帮助信息
show_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --clean          清理构建目录后重新构建"
    echo "  --no-test        跳过测试步骤"
    echo "  --install        构建后安装到 --prefix 指定的目录"
    echo "  --prefix <DIR>   安装前缀（默认 ${CGW_PREFIX}，来自 scripts/cgw-env.sh）"
    echo "  --release        Release 模式构建（默认 Debug）"
    echo "  --help           显示帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                              # 完整构建（Debug + 测试）"
    echo "  $0 --no-test                    # 仅构建，跳过测试"
    echo "  $0 --clean                      # 清理后重新构建"
    echo "  $0 --release                    # Release 模式构建"
    echo "  $0 --clean --install            # 清理构建并安装到 ${CGW_PREFIX}"
    echo "  CGW_PREFIX=/opt/cgw $0 --install  # 生产前缀（单一旋钮，无需改文件）"
}

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

# 检测操作系统
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [ -f /etc/debian_version ]; then
            echo "debian"
        elif [ -f /etc/redhat-release ]; then
            echo "redhat"
        else
            echo "linux"
        fi
    else
        echo "unknown"
    fi
}

# 检查依赖
check_dependencies() {
    print_info "检查构建依赖..."

    local missing_deps=()

    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi

    # 检查 C++ 编译器
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        missing_deps+=("c++ compiler (g++ or clang++)")
    fi

    # 检查 Make
    if ! command -v make &> /dev/null; then
        missing_deps+=("make")
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        print_error "缺少以下依赖:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        return 1
    fi

    print_success "基本构建工具已就绪"
    return 0
}

# 安装依赖（macOS）
install_deps_macos() {
    print_info "检测到 macOS 系统"

    # 检查 Homebrew
    if ! command -v brew &> /dev/null; then
        print_warning "未找到 Homebrew，正在安装..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi

    print_info "安装依赖库..."
    brew install cmake

    print_success "依赖安装完成"
}

# 安装依赖（Debian/Ubuntu）
install_deps_debian() {
    print_info "检测到 Debian/Ubuntu 系统"

    print_info "安装依赖库..."
    sudo apt-get update
    sudo apt-get install -y cmake

    print_success "依赖安装完成"
}

# 安装依赖（CentOS/RHEL）
install_deps_redhat() {
    print_info "检测到 CentOS/RHEL 系统"

    print_info "安装依赖库..."
    sudo yum install -y cmake

    print_success "依赖安装完成"
}

# 安装依赖
install_dependencies() {
    local os_type=$(detect_os)

    case $os_type in
        macos)
            install_deps_macos
            ;;
        debian)
            install_deps_debian
            ;;
        redhat)
            install_deps_redhat
            ;;
        *)
            print_warning "未知操作系统，请手动安装依赖"
            print_info "所需依赖: cmake"
            ;;
    esac
}

# 清理构建目录
clean_build() {
    if [ -d "$BUILD_DIR" ]; then
        print_info "清理构建目录: ${BUILD_DIR}"
        rm -rf "$BUILD_DIR"
        print_success "构建目录已清理"
    fi
}

# 配置项目
configure_project() {
    print_info "配置 CMake 项目..."
    print_info "  构建类型: ${BUILD_TYPE}"
    print_info "  安装前缀: ${INSTALL_PREFIX}"

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # CMAKE_INSTALL_PREFIX 必须在 configure 阶段给定：
    # CGWFrameworkConfig.cmake 需要它来定位头文件
    cmake .. \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
        -DBUILD_TESTS=$([ "$RUN_TESTS" = true ] && echo "ON" || echo "OFF")

    if [ $? -ne 0 ]; then
        print_error "CMake 配置失败"
        return 1
    fi

    print_success "CMake 配置完成"
    return 0
}

# 编译项目
build_project() {
    print_info "编译项目..."

    cd "$BUILD_DIR"

    # 获取 CPU 核心数
    if [[ "$OSTYPE" == "darwin"* ]]; then
        local cores=$(sysctl -n hw.ncpu)
    else
        local cores=$(nproc)
    fi

    make -j$cores

    if [ $? -ne 0 ]; then
        print_error "编译失败"
        return 1
    fi

    print_success "编译完成"
    return 0
}

# 运行测试
run_tests() {
    if [ "$RUN_TESTS" = false ]; then
        print_warning "跳过测试步骤"
        return 0
    fi

    print_info "运行单元测试..."

    cd "$BUILD_DIR"

    ctest --output-on-failure

    if [ $? -ne 0 ]; then
        print_error "测试失败"
        return 1
    fi

    print_success "所有测试通过"
    return 0
}

# 安装项目
install_project() {
    if [ "$INSTALL_AFTER_BUILD" = false ]; then
        return 0
    fi

    print_info "安装项目到: ${INSTALL_PREFIX}"

    cd "$BUILD_DIR"

    # 仅在前缀不可写时才用 sudo（如 /usr/local）；
    # 默认的 ${HOME}/.local 无需提权，避免产出 root 属主的文件
    if mkdir -p "${INSTALL_PREFIX}" 2>/dev/null && [ -w "${INSTALL_PREFIX}" ]; then
        cmake --install .
    else
        print_warning "前缀 ${INSTALL_PREFIX} 不可写，使用 sudo 安装"
        sudo cmake --install .
    fi

    if [ $? -ne 0 ]; then
        print_error "安装失败"
        return 1
    fi

    print_success "安装完成"
    print_info "消费方请使用: -DCGWFramework_DIR=${INSTALL_PREFIX}/lib/cmake/CGWFramework"
    return 0
}

# 显示构建摘要
show_summary() {
    echo ""
    echo "=========================================="
    echo "构建摘要"
    echo "=========================================="
    echo "项目根目录: ${PROJECT_ROOT}"
    echo "构建目录:   ${BUILD_DIR}"
    echo "构建类型:   ${BUILD_TYPE}"
    echo ""
    echo "构建产物:"
    echo "  - 静态库:    ${BUILD_DIR}/libcgw-framework-log.a"
    if [ "$RUN_TESTS" = true ]; then
        echo "  - 测试程序:  ${BUILD_DIR}/tests/"
    fi
    echo ""
    if [ "$INSTALL_AFTER_BUILD" = true ]; then
        echo "安装位置:"
        echo "  - 静态库:    ${INSTALL_PREFIX}/lib/libcgw-framework-log.a"
        echo "  - 静态库:    ${INSTALL_PREFIX}/lib/libcgw-framework-config.a"
        echo "  - 头文件:    ${INSTALL_PREFIX}/include/cgw-framework/"
        echo "  - CMake配置: ${INSTALL_PREFIX}/lib/cmake/CGWFramework/"
    fi
    echo "=========================================="
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --clean)
                CLEAN_BUILD=true
                shift
                ;;
            --no-test)
                RUN_TESTS=false
                shift
                ;;
            --install)
                INSTALL_AFTER_BUILD=true
                shift
                ;;
            --prefix)
                if [ -z "$2" ]; then
                    print_error "--prefix 需要一个目录参数"
                    exit 1
                fi
                INSTALL_PREFIX="$2"
                shift 2
                ;;
            --prefix=*)
                INSTALL_PREFIX="${1#--prefix=}"
                shift
                ;;
            --release)
                BUILD_TYPE="Release"
                shift
                ;;
            --help)
                show_help
                exit 0
                ;;
            *)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# 主函数
main() {
    echo "=========================================="
    echo "iov-vehicle-cgw-framework 构建脚本"
    echo "=========================================="
    echo ""

    # 解析参数
    parse_args "$@"

    # 检查依赖
    if ! check_dependencies; then
        print_warning "尝试安装缺失的依赖..."
        install_dependencies

        # 再次检查
        if ! check_dependencies; then
            print_error "依赖检查失败，请手动安装"
            exit 1
        fi
    fi

    # 清理构建目录（如果需要）
    if [ "$CLEAN_BUILD" = true ]; then
        clean_build
    fi

    # 配置项目
    if ! configure_project; then
        exit 1
    fi

    # 编译项目
    if ! build_project; then
        exit 1
    fi

    # 运行测试
    if ! run_tests; then
        exit 1
    fi

    # 安装项目
    if ! install_project; then
        exit 1
    fi

    # 显示摘要
    show_summary

    print_success "构建流程完成!"
}

# 运行主函数
main "$@"
