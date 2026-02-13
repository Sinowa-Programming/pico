cmake_minimum_required(VERSION 3.22)

# Platform to build for e.g. rp2040/rp2350/rp2350-arm-s/rp2350-riscv/host
set(PICO_PLATFORM "rp2350")  # rp2350-arm-ns = non secure | rp2350-arm-s = secure

# Pull in SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_vpx C CXX ASM)

pico_sdk_init()

# Free RTOS
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(freertos_config INTERFACE
    configSUPPORT_STATIC_ALLOCATION=0
    configSUPPORT_DYNAMIC_ALLOCATION=1
)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel)
include(FreeRTOS_Kernel_import.cmake)

add_subdirectory(program)