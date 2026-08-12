#!/bin/bash
#
# CGW 构建环境的单一来源（single source of truth）
#
# framework 与所有 CGW 服务的 build.sh 都 source 本文件，用于确定：
#   - framework 的安装前缀（CGW_PREFIX）
#   - 消费方查找 CGWFramework 包的路径（CGWFramework_DIR）
#
# 用法（在各项目 scripts/build.sh 中）：
#   source "$(dirname "${BASH_SOURCE[0]}")/../../iov-vehicle-cgw-framework/scripts/cgw-env.sh"
#
# 切换环境不需要改任何文件，只需覆盖环境变量：
#   本地开发（默认）：  ./scripts/build.sh
#   生产/CI：           CGW_PREFIX=/opt/cgw ./scripts/build.sh
#   交叉编译 staging：  CGW_PREFIX=/path/to/sysroot/usr ./scripts/build.sh
#
# 注意：本文件只做变量赋值，不得有副作用（不要在此编译/安装/打印大段日志），
#       因为它会被所有项目在 configure 之前 source。

# framework 的安装前缀。已存在的环境变量优先，便于 CI/生产覆盖。
: "${CGW_PREFIX:=${HOME}/.local}"
export CGW_PREFIX

# 受控 TARGET 依赖 staging 前缀（CR-006 §6/§7）。yaml-cpp 等目标平台依赖
# 的 CMake package / 头文件 / 静态库 由受控 recipe 安装到本前缀的 usr 子目录，
# CGW-FW configure 与下游 consumer 都通过 CMAKE_PREFIX_PATH 指向这里解析。
# 本地开发默认指向仓库内 staging（由 scripts/stage-yaml-cpp.sh 生成，HOST 架构，
# 与 TARGET 路径隔离）；CI/交叉编译由 BUILD 注入目标架构 staging。
: "${CGW_DEP_STAGING:=${HOME}/.cgw-deps}"
export CGW_DEP_STAGING

# 消费方定位 CGWFrameworkConfig.cmake 的目录。
# 两个变量都导出：CMAKE_PREFIX_PATH 是通用入口，CGWFramework_DIR 是精确指定，
# 后者可以绕过 CMake 默认搜索路径里可能存在的旧副本（如 /usr/local）。
export CGWFramework_DIR="${CGW_PREFIX}/lib/cmake/CGWFramework"
# 受控依赖前缀也加入通用查找根，供 find_package(yaml-cpp) 解析。
export CGW_DEP_PREFIX_PATH="${CGW_DEP_STAGING}/usr"

# framework 源码树位置（供仍需引用源码/第三方库的场景使用）
CGW_ENV_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export CGW_FRAMEWORK_ROOT="$(cd "${CGW_ENV_SCRIPT_DIR}/.." && pwd)"
unset CGW_ENV_SCRIPT_DIR
