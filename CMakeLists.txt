project(CGW-Framework)
cmake_minimum_required(VERSION 3.10)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 线程库 (cgw-framework-store 使用 std::thread/std::mutex)
find_package(Threads REQUIRED)

# 收集源文件
file(GLOB FRAMEWORK_SOURCES src/log/*.cpp)
file(GLOB_RECURSE FRAMEWORK_HEADERS include/*.h)

# 创建框架库
add_library(cgw-framework SHARED
        ${FRAMEWORK_SOURCES}
        ${FRAMEWORK_HEADERS}
        )
set_target_properties(cgw-framework PROPERTIES
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
    add_library(yaml-cpp::yaml-cpp STATIC IMPORTED)
    set_target_properties(yaml-cpp::yaml-cpp PROPERTIES
        IMPORTED_LOCATION "${LIB_DIR}/yaml-cpp/libyaml-cpp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/third_party/include"
    )
    # 使用LINK_FLAGS直接传递链接选项，避免CMake重排
    if(APPLE)
        set_target_properties(cgw-framework PROPERTIES
            LINK_FLAGS "-force_load ${LIB_DIR}/yaml-cpp/libyaml-cpp.a"
        )
    else()
        set_target_properties(cgw-framework PROPERTIES
            LINK_FLAGS "-Wl,--whole-archive ${LIB_DIR}/yaml-cpp/libyaml-cpp.a -Wl,--no-whole-archive"
        )
    endif()

    # 拷贝 yaml-cpp dylib 到 build 目录
    file(GLOB YAML_CPP_DYLIBS "${LIB_DIR}/yaml-cpp/*.dylib" "${LIB_DIR}/yaml-cpp/*.so")
    foreach(YAML_CPP_DYLIB ${YAML_CPP_DYLIBS})
        add_custom_command(TARGET cgw-framework POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${YAML_CPP_DYLIB}
                $<TARGET_FILE_DIR:cgw-framework>
                COMMENT "Copying ${YAML_CPP_DYLIB} to build directory"
                )
    endforeach()
else()
    message(WARNING "YAML library directory not found: ${LIB_DIR}/yaml-cpp")
endif()
# OpenSSL not required for CGW framework-log

# 设置包含目录
target_include_directories(cgw-framework
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/include>
        $<INSTALL_INTERFACE:include/cgw-framework>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )

# ============================================================
# cgw-framework-config: independent static library (CGW-FW-DSN-CR-002)
# ============================================================
file(GLOB CONFIG_SOURCES src/config/*.cpp)
add_library(cgw-framework-config STATIC ${CONFIG_SOURCES})
set_target_properties(cgw-framework-config PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(cgw-framework-config
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/cgw-framework>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
        )
if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(cgw-framework-config PRIVATE yaml-cpp::yaml-cpp)
endif()

# ============================================================
# cgw-framework-store: independent static library (CGW-FW-DSN-CR-003)
# ============================================================
file(GLOB STORE_SOURCES src/store/*.cpp)
add_library(cgw-framework-store STATIC ${STORE_SOURCES})
set_target_properties(cgw-framework-store PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(cgw-framework-store
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/cgw-framework>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
target_link_libraries(cgw-framework-store PUBLIC Threads::Threads)

# 配置文件
configure_file(CGWFrameworkConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFrameworkConfig.cmake"
        @ONLY)

# 安装配置文件
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFrameworkConfig.cmake"
        DESTINATION lib/cmake/CGWFramework
        )

# 配置 pkg-config 文件
configure_file(CGWFramework.pc.in
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFramework.pc"
        @ONLY)

# 安装 pkg-config 文件
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFramework.pc"
        DESTINATION lib/pkgconfig
        )

# 安装规则
install(TARGETS cgw-framework
        EXPORT CGWFramework-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/cgw-framework
        )

install(TARGETS cgw-framework-config
        EXPORT CGWFramework-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/cgw-framework
        )

install(TARGETS cgw-framework-store
        EXPORT CGWFramework-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/cgw-framework
        )

install(DIRECTORY include/
        DESTINATION include/cgw-framework
        FILES_MATCHING PATTERN "*.h"
        )

# 导出配置
install(EXPORT CGWFramework-targets
        FILE CGWFrameworkTargets.cmake
        NAMESPACE CGWFramework::
        DESTINATION lib/cmake/CGWFramework
        )

# 单元测试
enable_testing()

# 日志组件测试 (cgw-framework)
set(LOG_TEST_SOURCES
        tests/test_log_types.cpp
        tests/test_log_config_adapter.cpp
        tests/test_log_enricher.cpp
        tests/test_log_redactor.cpp
        tests/test_log_level_filter.cpp
        tests/test_log_async_dispatcher.cpp
        tests/test_log_json_formatter.cpp
        tests/test_log_standard_formatter.cpp
        tests/test_log_context_scope.cpp
        tests/test_log_integration.cpp
        )

# 配置组件测试 (cgw-framework-config)
set(CONFIG_TEST_SOURCES
        tests/test_config_path_resolver.cpp
        tests/test_config_yaml_parser.cpp
        tests/test_config_merger.cpp
        tests/test_config_snapshot.cpp
        tests/test_config_validator.cpp
        )

# 存储组件测试 (cgw-framework-store)
set(STORE_TEST_SOURCES
        tests/test_store_path_resolver.cpp
        tests/test_store_atomic_writer.cpp
        tests/test_store_key_lock.cpp
        tests/test_store_serializer.cpp
        tests/test_store_api.cpp
        tests/test_store_flush_policy.cpp
        tests/test_store_concurrency.cpp
        )

foreach(TEST_SOURCE ${LOG_TEST_SOURCES} ${CONFIG_TEST_SOURCES} ${STORE_TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    if(${TEST_NAME} MATCHES "^test_config_")
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-config yaml-cpp::yaml-cpp pthread)
    elseif(${TEST_NAME} MATCHES "^test_store_")
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-store pthread)
    else()
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework yaml-cpp::yaml-cpp pthread)
    endif()
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

# 配置与日志集成测试 (config -> LogConfig -> Logger::init)
add_executable(test_config_integration tests/test_config_integration.cpp)
target_link_libraries(test_config_integration PRIVATE cgw-framework cgw-framework-config yaml-cpp::yaml-cpp pthread)
target_include_directories(test_config_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
if(EXISTS ${LIB_DIR}/yaml-cpp)
    target_link_directories(test_config_integration PRIVATE $<BUILD_INTERFACE:${LIB_DIR}/yaml-cpp>)
endif()
add_test(NAME test_config_integration COMMAND test_config_integration)

# 存储与配置集成测试 (config root 注入 -> Store::open)
add_executable(test_store_integration tests/test_store_integration.cpp)
target_link_libraries(test_store_integration PRIVATE cgw-framework-store cgw-framework-config yaml-cpp::yaml-cpp pthread)
target_include_directories(test_store_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
if(EXISTS ${LIB_DIR}/yaml-cpp)
    target_link_directories(test_store_integration PRIVATE $<BUILD_INTERFACE:${LIB_DIR}/yaml-cpp>)
endif()
add_test(NAME test_store_integration COMMAND test_store_integration)