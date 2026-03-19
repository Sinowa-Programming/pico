# Library that holds the VRAM settings
add_library(vram_settings INTERFACE)

# Seperate text and data
target_compile_options(vram_settings INTERFACE
    -ffunction-sections
    -fdata-sections
)

# Add FreeRTOS Posix
set( FREERTOS_PLUS_POSIX_PATH "../posix_wrapper/Lab-Project-FreeRTOS-POSIX" )
file( GLOB FREERTOS_PLUS_POSIX_SOURCES
           ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/source/*.c )

add_library(freertos_posix_config STATIC)

target_sources( freertos_posix_config PRIVATE
                ${FREERTOS_PLUS_POSIX_SOURCES} )

target_include_directories( freertos_posix_config PRIVATE
                            # Safely get FreeRTOS/Pico headers without compiling their sources
                            $<TARGET_PROPERTY:freertos_config,INTERFACE_INCLUDE_DIRECTORIES>
                            $<TARGET_PROPERTY:FreeRTOS-Kernel,INTERFACE_INCLUDE_DIRECTORIES>
                            $<TARGET_PROPERTY:pico_stdlib,INTERFACE_INCLUDE_DIRECTORIES>

                            ${FREERTOS_PLUS_POSIX_PATH}/..
                            ${FREERTOS_PLUS_POSIX_PATH}/include
                            ${FREERTOS_PLUS_POSIX_PATH}/include/FreeRTOS_POSIX
                            ${FREERTOS_PLUS_POSIX_PATH}/include/private
                            ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include
                            ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include/portable
                            ${FREERTOS_PLUS_POSIX_PATH}/FreeRTOS-Plus-POSIX/include/portable/raspberrypi
                            )

target_link_libraries(freertos_posix_config PRIVATE
    freertos_config
)

target_compile_options(freertos_posix_config PRIVATE "-include" "FreeRTOS.h")

# Add Dav1d library
# The static lib file was copied from dav1d/build/src
add_library(Dav1d_pico STATIC IMPORTED)
set_target_properties(Dav1d_pico PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libdav1d.a"
)

file(GLOB_RECURSE DAV1D_INPUT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../ported_dav1d/dav1d/tools/input/*.c"
)

# Adding Source code
add_library(Client_program STATIC
    dav1dplay.c
    dp_fifo.c
    dp_renderer_pico.c

    ${DAV1D_INPUT_SOURCES}
)
target_include_directories(Client_program PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}

    # DAV1D includes
    "${CMAKE_CURRENT_SOURCE_DIR}/../ported_dav1d/dav1d/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ported_dav1d/dav1d"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ported_dav1d/dav1d/include/dav1d"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ported_dav1d/dav1d/tools"

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
    $<TARGET_PROPERTY:freertos_posix_config,INTERFACE_INCLUDE_DIRECTORIES>
)


target_link_libraries(Client_program PRIVATE
    Dav1d_pico
    freertos_config
    freertos_posix_config
    vram_settings
)

target_compile_definitions(Client_program PRIVATE
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_COMPILE_DEFINITIONS>
)

target_compile_options(Client_program PRIVATE "-include" "FreeRTOS.h")
