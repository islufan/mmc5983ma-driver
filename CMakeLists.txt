cmake_minimum_required(VERSION 3.16)
project(mmc5983ma C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 源码为 UTF-8，MSVC 默认按本地代码页解析会报 C4819
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(mmc5983ma STATIC
    src/mmc5983ma.c
)
target_include_directories(mmc5983ma PUBLIC src)

# 只在使用 STM32 HAL 时才需要（需要 main.h / HAL 头文件路径）
option(MMC_BUILD_HAL_PORT "Build STM32 HAL adapter" OFF)
if(MMC_BUILD_HAL_PORT)
    add_library(mmc5983ma_hal STATIC port/mmc5983ma_hal.c)
    target_link_libraries(mmc5983ma_hal PUBLIC mmc5983ma)
    target_include_directories(mmc5983ma_hal PUBLIC port)
endif()

option(MMC_BUILD_TESTS "Build unit tests" OFF)
if(MMC_BUILD_TESTS)
    enable_testing()
    add_executable(test_mmc5983ma tests/test_mmc5983ma.c)
    target_link_libraries(test_mmc5983ma PRIVATE mmc5983ma)
    add_test(NAME mmc5983ma_tests COMMAND test_mmc5983ma)
endif()
