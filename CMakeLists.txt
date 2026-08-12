# CGW-Framework top-level build (CGW-FW-DSN-CR-006 install/release contract)
cmake_minimum_required(VERSION 3.20)

project(CGW-Framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standard install directories + package config helpers (CR-006 §3/§5).
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Threads: cgw-framework-log/store/someip use std::thread/std::mutex.
find_package(Threads REQUIRED)

# yaml-cpp: controlled TARGET dependency (CR-006 §6). MUST resolve from a
# controlled dependency prefix (CGW_DEP_STAGING) or an equivalent controlled
# CMake package root. No silent EXISTS fallback; configure fails closed when
# the matching package/target is absent.
find_package(yaml-cpp CONFIG REQUIRED)

# ============================================================
# cgw-framework-log: independent static library (CGW-FW-DSN-CR-001)
# yaml-cpp is PRIVATE (only src/log/log_config_adapter.cpp includes it).
# ============================================================
file(GLOB FRAMEWORK_SOURCES src/log/*.cpp)
add_library(cgw-framework-log STATIC ${FRAMEWORK_SOURCES})
set_target_properties(cgw-framework-log PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(cgw-framework-log
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
# Threads PUBLIC: the async queue exposes std::thread in the link interface.
target_link_libraries(cgw-framework-log
        PUBLIC Threads::Threads
        PRIVATE yaml-cpp::yaml-cpp)

# ============================================================
# cgw-framework-config: independent static library (CGW-FW-DSN-CR-002)
# ============================================================
file(GLOB CONFIG_SOURCES src/config/*.cpp)
add_library(cgw-framework-config STATIC ${CONFIG_SOURCES})
set_target_properties(cgw-framework-config PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(cgw-framework-config
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
target_link_libraries(cgw-framework-config PRIVATE yaml-cpp::yaml-cpp)

# ============================================================
# cgw-framework-store: independent static library (CGW-FW-DSN-CR-003)
# ============================================================
file(GLOB STORE_SOURCES src/store/*.cpp)
add_library(cgw-framework-store STATIC ${STORE_SOURCES})
set_target_properties(cgw-framework-store PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(cgw-framework-store
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
target_link_libraries(cgw-framework-store PUBLIC Threads::Threads)

# ============================================================
# cgw-framework-hash: independent static library (CGW-FW-DSN-CR-004)
# FIPS 180-4 SHA-256 + reversible byte<->Hex codec (CGW-FW-DSN-CR-007);
# no config/store/log/SOME-IP dependency.
# Public headers: include/cgw/fw/hash/sha256.hpp, include/cgw/fw/hash/hex.hpp.
# ============================================================
# Sanitize OFF by default so installed archives are clean and linkable by an
# uninstrumented consumer (CR-006 §11). Devs opt in with
# -DCGW_FW_HASH_SANITIZE=ON for a sanitized test build.
option(CGW_FW_HASH_SANITIZE "Build cgw-framework-hash with ASan/UBSan" OFF)
add_library(cgw-framework-hash STATIC src/sha256.cpp src/hex.cpp)
set_target_properties(cgw-framework-hash PROPERTIES LINKER_LANGUAGE CXX)
# CR-004 §3: build-tree alias cgw::framework-hash (not installed).
add_library(cgw::framework-hash ALIAS cgw-framework-hash)
target_include_directories(cgw-framework-hash
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
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
option(CGW_FW_SOMEIP_SANITIZE "Build cgw-framework-someip with ASan/UBSan" OFF)
add_library(cgw-framework-someip STATIC
        src/someip_runtime.cpp
        src/someip_provider.cpp
        src/someip_client.cpp
        src/someip_backend.cpp
        )
set_target_properties(cgw-framework-someip PROPERTIES LINKER_LANGUAGE CXX)
# CR-005 §3: build-tree alias cgw::framework-someip (not installed).
add_library(cgw::framework-someip ALIAS cgw-framework-someip)
target_include_directories(cgw-framework-someip
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
target_link_libraries(cgw-framework-someip PUBLIC Threads::Threads)
target_compile_options(cgw-framework-someip PRIVATE -Wall -Wextra)
if(CGW_FW_SOMEIP_SANITIZE)
    target_compile_options(cgw-framework-someip PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# ============================================================
# Install rules (CGW-FW-DSN-CR-006 §3/§4)
# - SDK component: headers + archives + CMake package + pkg-config.
# - Runtime component: empty for the current static-only build; the empty set
#   is an explicit, recorded state (no placeholder files are fabricated).
# - Every formal install rule declares its COMPONENT; no Unspecified fallback.
# ============================================================
install(TARGETS
        cgw-framework-config
        cgw-framework-log
        cgw-framework-store
        cgw-framework-hash
        cgw-framework-someip
        EXPORT CGWFrameworkTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT cgw-framework-sdk
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT cgw-framework-runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT cgw-framework-runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT cgw-framework-sdk
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# Relocatable CMake package config (CR-006 §5): @PACKAGE_INIT@ derives the
# prefix from the config file location; no baked absolute/host paths.
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/CGWFrameworkConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFrameworkConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CGWFramework
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFrameworkConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFrameworkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CGWFrameworkConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CGWFramework
        COMPONENT cgw-framework-sdk)

install(EXPORT CGWFrameworkTargets
        FILE CGWFrameworkTargets.cmake
        NAMESPACE CGWFramework::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CGWFramework
        COMPONENT cgw-framework-sdk)

# pkg-config per component (CR-006 §4). config/log carry yaml-cpp as
# Requires.private (static link); store/someip carry pthread as Libs.private.
foreach(comp config log store hash someip)
    set(PC_NAME           "cgw-framework-${comp}")
    set(PC_DESCRIPTION    "CGW Framework ${comp} library")
    set(PC_LIB            "cgw-framework-${comp}")
    set(PC_REQUIRES       "")
    set(PC_LIBS_PRIVATE   "")
    if(comp STREQUAL "config" OR comp STREQUAL "log")
        set(PC_REQUIRES "yaml-cpp")
    endif()
    if(comp STREQUAL "store" OR comp STREQUAL "someip")
        set(PC_LIBS_PRIVATE "-lpthread")
    endif()
    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cgw-framework.pc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cgw-framework-${comp}.pc"
            @ONLY)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/cgw-framework-${comp}.pc"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
            COMPONENT cgw-framework-sdk)
endforeach()

# ============================================================
# Unit tests (not installed; carry no install COMPONENT)
# ============================================================
enable_testing()

# 日志组件测试 (cgw-framework-log)
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
        tests/hex_test.cpp
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
    elseif(${TEST_NAME} MATCHES "^(sha256|hex)")
        # CR-004 §3 / CR-007 §8.4: hash 测试启用 -Wall -Wextra -Werror；测试构建 ASan/UBSan。
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
        target_link_libraries(${TEST_NAME} PRIVATE cgw-framework-log yaml-cpp::yaml-cpp pthread)
    endif()
    target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# 配置与日志集成测试 (config -> LogConfig -> Logger::init)
add_executable(test_config_integration tests/test_config_integration.cpp)
target_link_libraries(test_config_integration PRIVATE cgw-framework-log cgw-framework-config yaml-cpp::yaml-cpp pthread)
target_include_directories(test_config_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
add_test(NAME test_config_integration COMMAND test_config_integration)

# 存储与配置集成测试 (config root 注入 -> Store::open)
add_executable(test_store_integration tests/test_store_integration.cpp)
target_link_libraries(test_store_integration PRIVATE cgw-framework-store cgw-framework-config yaml-cpp::yaml-cpp pthread)
target_include_directories(test_store_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
add_test(NAME test_store_integration COMMAND test_store_integration)

# 最小消费样例 (CGW-FW-DSN-CR-004 §13): links the exported build-tree alias
# cgw::framework-hash and demonstrates canonical-bytes -> SHA-256 -> compare.
add_executable(hash_consumer examples/hash_consumer.cpp)
target_link_libraries(hash_consumer PRIVATE cgw::framework-hash)
if(CGW_FW_HASH_SANITIZE)
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

# ============================================================
# Install/packaging tests (CGW-FW-DSN-CR-006)
# Fast unit checks run with the normal test build; the heavier end-to-end
# installed-consumer integration test is opt-in via CGW_FW_INSTALL_TESTS.
# ============================================================
set(CGW_FW_INSTALL_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/install")

add_test(NAME test_archive_arch
        COMMAND bash "${CGW_FW_INSTALL_TEST_DIR}/test_archive_arch.sh"
                "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(test_archive_arch PROPERTIES
        LABELS "cgw_install")

add_test(NAME test_install_components
        COMMAND bash "${CGW_FW_INSTALL_TEST_DIR}/test_install_components.sh"
                "${CMAKE_BINARY_DIR}")
set_tests_properties(test_install_components PROPERTIES
        LABELS "cgw_install")

add_test(NAME test_package_relocatable
        COMMAND bash "${CGW_FW_INSTALL_TEST_DIR}/test_package_relocatable.sh"
                "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(test_package_relocatable PROPERTIES
        LABELS "cgw_install")

option(CGW_FW_INSTALL_TESTS
        "Enable the heavy installed-consumer integration test (CR-006)" OFF)
if(CGW_FW_INSTALL_TESTS)
    add_test(NAME test_installed_consumer
            COMMAND bash "${CGW_FW_INSTALL_TEST_DIR}/test_installed_consumer.sh"
                    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(test_installed_consumer PROPERTIES
            LABELS "cgw_install")
endif()
