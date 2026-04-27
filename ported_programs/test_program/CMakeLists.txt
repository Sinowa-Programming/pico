# Library that holds the VRAM settings
add_library(vram_settings INTERFACE)

# Seperate text and data
target_compile_options(vram_settings INTERFACE
    -ffunction-sections
    -fdata-sections
)


# Adding Source code
add_library(Client_program STATIC
    test_program_main.c
    keep_client_ref.c
)
target_include_directories(Client_program PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}

    # FreeRTOS includes
    "${FREERTOS_PLUS_POSIX_PATH}/include"
    "${FREERTOS_PLUS_POSIX_PATH}/include/FreeRTOS_POSIX"
    "${FREERTOS_PLUS_POSIX_PATH}/include/private"
    "${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include"
    "${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include/portable"
    "${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include/portable/raspberrypi"

    # Safely get FreeRTOS/Pico headers without compiling their sources
    $<TARGET_PROPERTY:FreeRTOS-Kernel,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:freertos_config,INTERFACE_INCLUDE_DIRECTORIES>
)


target_link_libraries(Client_program PRIVATE
    freertos_config
    vram_settings
)

target_compile_definitions(Client_program PRIVATE
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_COMPILE_DEFINITIONS>
)

# Combine and protect your includes so CMake doesn't strip the flags
target_compile_options(Client_program PRIVATE 
    "SHELL:-include FreeRTOS.h"
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/../../program/modules/pal/pal_stdlib.h"
)