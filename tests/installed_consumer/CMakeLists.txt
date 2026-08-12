# Independent installed-package consumer (CGW-FW-DSN-CR-006 §8).
#
# This project lives outside the CGW-FW source/build tree and consumes ONLY the
# installed CGWFramework package. It must not reference CGW-FW source includes,
# build-tree targets, private headers or undeclared env vars.
cmake_minimum_required(VERSION 3.20)
project(cgw_framework_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CGWFramework CONFIG REQUIRED)

add_executable(consumer main.cpp)

# Link all five exported targets to prove they resolve individually and together.
target_link_libraries(consumer PRIVATE
        CGWFramework::cgw-framework-config
        CGWFramework::cgw-framework-log
        CGWFramework::cgw-framework-store
        CGWFramework::cgw-framework-hash
        CGWFramework::cgw-framework-someip
        )
