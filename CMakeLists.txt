cmake_minimum_required(VERSION 3.22)


# Platform to build for e.g. rp2040/rp2350/rp2350-arm-s/rp2350-riscv/host
set(PICO_PLATFORM "rp2350")  # rp2350-arm-ns = non secure | rp2350-arm-s = secure

# Pull in SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_vpx C CXX ASM)

pico_sdk_init()

option(ENABLE_DAV1D_PICO_INTERFACE "Build the dav1d pico interface" ON)

# Free RTOS
add_library(freertos_config INTERFACE)
target_include_directories( freertos_config
                            INTERFACE
                            ./ )


set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel)
include(FreeRTOS_Kernel_import.cmake)

message("FreeRTOS Kernel setup")

if(ENABLE_DAV1D_PICO_INTERFACE)
    add_subdirectory(ported_programs/dav1d_pico_interface)
else()
    message(STATUS "dav1d pico interface disabled (ENABLE_DAV1D_PICO_INTERFACE=OFF)")
    if(NOT TARGET Client_program)
        add_library(Client_program INTERFACE)
    endif()
endif()
add_subdirectory(program)
