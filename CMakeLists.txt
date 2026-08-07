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

# ============================================================
# cgw-framework-hash: independent static library (CGW-FW-DSN-CR-004)
# FIPS 180-4 SHA-256; no config/store/log/SOME-IP dependency.
# Public header: include/cgw/fw/hash/sha256.hpp.
# ============================================================
# ASan/UBSan for the test build (CR-004 §3). Default ON so the test build is
# sanitized; set -DCGW_FW_HASH_SANITIZE=OFF for a clean release/install lib.
option(CGW_FW_HASH_SANITIZE "Build cgw-framework-hash with ASan/UBSan" ON)
add_library(cgw-framework-hash STATIC src/sha256.cpp)
set_target_properties(cgw-framework-hash PROPERTIES LINKER_LANGUAGE CXX)
# CR-004 §3: export alias cgw::framework-hash.
add_library(cgw::framework-hash ALIAS cgw-framework-hash)
target_include_directories(cgw-framework-hash
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/cgw-framework>
        )
target_compile_options(cgw-framework-hash PRIVATE -Wall -Wextra -Werror)
if(CGW_FW_HASH_SANITIZE)
    target_compile_options(cgw-framework-hash PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# ============================================================
# cgw-framework-someip: independent static library (CGW-FW-DSN-CR-005)
# SOME/IP Runtime/Provider/Client with a PRIVATE controlled backend.
# Public headers: include/cgw/fw/someip/*.hpp. Backend is PRIVATE (src/).
# ============================================================
option(CGW_FW_SOMEIP_SANITIZE "Build cgw-framework-someip with ASan/UBSan" ON)
add_library(cgw-framework-someip STATIC
        src/someip_runtime.cpp
        src/someip_provider.cpp
        src/someip_client.cpp
        src/someip_backend.cpp
        )
set_target_properties(cgw-framework-someip PROPERTIES LINKER_LANGUAGE CXX)
# CR-005 §3: export alias cgw::framework-someip.
add_library(cgw::framework-someip ALIAS cgw-framework-someip)
target_include_directories(cgw-framework-someip
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/cgw-framework>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
target_link_libraries(cgw-framework-someip PUBLIC Threads::Threads)
target_compile_options(cgw-framework-someip PRIVATE -Wall -Wextra)
if(CGW_FW_SOMEIP_SANITIZE)
    target_compile_options(cgw-framework-someip PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

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

install(TARGETS cgw-framework-hash
        EXPORT CGWFramework-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/cgw-framework
        )

install(TARGETS cgw-framework-someip
        EXPORT CGWFramework-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/cgw-framework
        )

install(DIRECTORY include/
        DESTINATION include/cgw-framework
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
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

# 摘要组件测试 (cgw-framework-hash)
set(HASH_TEST_SOURCES
        tests/sha256_test.cpp
        tests/sha256_differential_test.cpp
        )

# SOME/IP 组件测试 (cgw-framework-someip)
set(SOMEIP_TEST_SOURCES
        tests/someip_runtime_test.cpp
        tests/someip_provider_test.cpp
        tests/someip_client_test.cpp
        tests/someip_integration_test.cpp
        )

foreach(TEST_SOURCE ${LOG_TEST_SOURCES} ${CONFIG_TEST_SOURCES} ${STORE_TEST_SOURCES} ${HASH_TEST_SOURCES} ${SOMEIP_TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    if(${TEST_NAME} MATCHES "^test_config_")
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-config yaml-cpp::yaml-cpp pthread)
    elseif(${TEST_NAME} MATCHES "^test_store_")
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-store pthread)
    elseif(${TEST_NAME} MATCHES "^sha256")
        # CR-004 §3: hash 测试启用 -Wall -Wextra -Werror；测试构建 ASan/UBSan。
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-hash)
        target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror)
        if(CGW_FW_HASH_SANITIZE)
            target_compile_options(${TEST_NAME} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
            set_target_properties(${TEST_NAME} PROPERTIES
                    LINK_FLAGS "-fsanitize=address,undefined")
        endif()
    elseif(${TEST_NAME} MATCHES "^someip")
        # CR-005 §14: someip 测试链接 cgw-framework-someip；ASan/UBSan。
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-someip pthread)
        target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra)
        if(CGW_FW_SOMEIP_SANITIZE)
            target_compile_options(${TEST_NAME} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
            set_target_properties(${TEST_NAME} PROPERTIES
                    LINK_FLAGS "-fsanitize=address,undefined")
        endif()
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

# 最小消费样例 (CGW-FW-DSN-CR-004 §13): links the exported build-tree alias
# cgw::framework-hash and demonstrates canonical-bytes -> SHA-256 -> compare.
add_executable(hash_consumer examples/hash_consumer.cpp)
target_link_libraries(hash_consumer PRIVATE cgw::framework-hash)
if(CGW_FW_HASH_SANITIZE)
    # The sanitized static lib requires the consumer to link the sanitizer
    # runtime too. (Set -DCGW_FW_HASH_SANITIZE=OFF for a clean release lib.)
    target_compile_options(hash_consumer PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    set_target_properties(hash_consumer PROPERTIES
            LINK_FLAGS "-fsanitize=address,undefined")
endif()

# 最小 Provider/Client 消费样例 (CGW-FW-DSN-CR-005 §15.4): links the exported
# build-tree alias cgw::framework-someip and demonstrates Provider/Client direct
# communication via the controlled (fake) backend.
add_executable(someip_consumer examples/someip_consumer.cpp)
target_link_libraries(someip_consumer PRIVATE cgw::framework-someip)
if(CGW_FW_SOMEIP_SANITIZE)
    target_compile_options(someip_consumer PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    set_target_properties(someip_consumer PROPERTIES
            LINK_FLAGS "-fsanitize=address,undefined")
endif()