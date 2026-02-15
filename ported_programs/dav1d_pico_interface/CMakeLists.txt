cmake_minimum_required(VERSION 3.22)

# Platform to build for e.g. rp2040/rp2350/rp2350-arm-s/rp2350-riscv/host
set(PICO_PLATFORM "rp2350")  # rp2350-arm-ns = non secure | rp2350-arm-s = secure

# Pull in SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_dav1d C CXX ASM)

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

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/../../FreeRTOS-Kernel)
include(FreeRTOS_Kernel_import.cmake)

# Add FreeRTOS Posix
set( FREERTOS_PLUS_POSIX_PATH "../posix_wrapper/Lab-Project-FreeRTOS-POSIX" )
file( GLOB FREERTOS_PLUS_POSIX_SOURCES
           ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/source/*.c )

target_sources( ${PROJECT_NAME} PRIVATE
                ${FREERTOS_PLUS_POSIX_SOURCES} )

target_include_directories( ${PROJECT_NAME} PRIVATE
                            ${FREERTOS_PLUS_POSIX_PATH}/include
                            ${FREERTOS_PLUS_POSIX_PATH}/include/private
                            ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include
                            ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include/portable
                            ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include/portable/pc/windows )

# Add Dav1d library
# The static lib file was copied from dav1d/build/src
add_library(Dav1d_pico STATIC IMPORTED)
set_target_properties(Dav1d_pico PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libdav1d.a"
)

add_executable( ${PROJECT_NAME}
    dav1dplay.c
)

# Add all needed libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    freertos_config
    FreeRTOS-Kernel
    pico_async_context_freertos
    FreeRTOS-Kernel-Heap4
    pico_stdlib
    Dav1d_pico
)

# Add custom PAL header
target_include_directories(${PROJECT_NAME} INTERFACE
    "/workspaces/pico/program/modules/pal"
)

# Adding Source code
target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_sources( ${PROJECT_NAME} PRIVATE
    dp_fifo.c
    dp_renderer_placebo.c
    dp_renderer_sdl.c
)

pico_add_extra_outputs(${PROJECT_NAME})