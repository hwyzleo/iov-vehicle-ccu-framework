project(CGW-Framework)
cmake_minimum_required(VERSION 3.10)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 收集源文件
file(GLOB_RECURSE FRAMEWORK_SOURCES src/*.cpp)
file(GLOB_RECURSE FRAMEWORK_HEADERS include/*.h)

# 创建框架库
add_library(hwyz SHARED
        ${FRAMEWORK_SOURCES}
        ${FRAMEWORK_HEADERS}
        )
set_target_properties(hwyz PROPERTIES
        LINKER_LANGUAGE CXX
        )

# 根据不同的环境设置不同的库文件目录
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/arm64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/aarch64-linux-gnu)
    endif ()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-linux-gnu)
    endif ()
endif ()

# 添加三方库
if(EXISTS ${LIB_DIR}/yaml-cpp)
    # 使用IMPORTED静态库提供include路径
    add_library(yaml-cpp STATIC IMPORTED)
    set_target_properties(yaml-cpp PROPERTIES
        IMPORTED_LOCATION "${LIB_DIR}/yaml-cpp/libyaml-cpp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/third_party/include"
    )
    # 使用LINK_FLAGS直接传递链接选项，避免CMake重排
    if(APPLE)
        set_target_properties(hwyz PROPERTIES
            LINK_FLAGS "-force_load ${LIB_DIR}/yaml-cpp/libyaml-cpp.a"
        )
    else()
        set_target_properties(hwyz PROPERTIES
            LINK_FLAGS "-Wl,--whole-archive ${LIB_DIR}/yaml-cpp/libyaml-cpp.a -Wl,--no-whole-archive"
        )
    endif()

    # 拷贝 yaml-cpp dylib 到 build 目录
    file(GLOB YAML_CPP_DYLIBS "${LIB_DIR}/yaml-cpp/*.dylib" "${LIB_DIR}/yaml-cpp/*.so")
    foreach(YAML_CPP_DYLIB ${YAML_CPP_DYLIBS})
        add_custom_command(TARGET hwyz POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${YAML_CPP_DYLIB}
                $<TARGET_FILE_DIR:hwyz>
                COMMENT "Copying ${YAML_CPP_DYLIB} to build directory"
                )
    endforeach()
else()
    message(WARNING "YAML library directory not found: ${LIB_DIR}/yaml-cpp")
endif()
# OpenSSL not required for CGW framework-log

# 设置包含目录
target_include_directories(hwyz
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )

# 配置文件
configure_file(HWYZConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZConfig.cmake"
        @ONLY)

# 安装配置文件
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZConfig.cmake"
        DESTINATION lib/cmake/HWYZ
        )

# 配置 pkg-config 文件
configure_file(HWYZ.pc.in
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZ.pc"
        @ONLY)

# 安装 pkg-config 文件
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZ.pc"
        DESTINATION lib/pkgconfig
        )

# 安装规则
install(TARGETS hwyz
        EXPORT HWYZ-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
        )

install(DIRECTORY include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
        )

# 导出配置
install(EXPORT HWYZ-targets
        FILE HWYZTargets.cmake
        NAMESPACE HWYZ::
        DESTINATION lib/cmake/HWYZ
        )

# 单元测试
enable_testing()

set(TEST_SOURCES
        tests/test_log_types.cpp
        tests/test_log_config_adapter.cpp
        tests/test_log_enricher.cpp
        )

foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE hwyz yaml-cpp pthread)
    target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            )
    if(EXISTS ${LIB_DIR}/yaml-cpp)
        target_link_directories(${TEST_NAME} PRIVATE $<BUILD_INTERFACE:${LIB_DIR}/yaml-cpp>)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()