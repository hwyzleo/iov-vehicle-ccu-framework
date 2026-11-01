# Independent installed-package consumer (CCU-FW-DSN-CR-006 §8).
#
# This project lives outside the CCU-FW source/build tree and consumes ONLY the
# installed CCUFramework package. It must not reference CCU-FW source includes,
# build-tree targets, private headers or undeclared env vars.
cmake_minimum_required(VERSION 3.20)
project(ccu_framework_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CCUFramework CONFIG REQUIRED)

add_executable(consumer main.cpp)

# Link all five exported targets to prove they resolve individually and together.
target_link_libraries(consumer PRIVATE
        CCUFramework::ccu-framework-config
        CCUFramework::ccu-framework-log
        CCUFramework::ccu-framework-store
        CCUFramework::ccu-framework-hash
        CCUFramework::ccu-framework-someip
        )
