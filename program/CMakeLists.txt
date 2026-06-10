
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/main/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/main/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/modules/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/modules/*.cpp"
)

add_executable(${PROJECT_NAME} ${APP_SOURCES} "${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c")

# Custom Memory for Heap
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/program/custom_pico_mem.ld)


pico_set_binary_type(${PROJECT_NAME} no_flash)
# set_target_properties(${PROJECT_NAME} PROPERTIES
#     PICO_NO_FLASH 1
#     PICO_COPY_TO_RAM 0
# )

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/libs

    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/modules/pal
    ${CMAKE_CURRENT_LIST_DIR}/modules/communication
    ${CMAKE_CURRENT_LIST_DIR}/modules/memory
    ${CMAKE_CURRENT_LIST_DIR}/modules/file
    ${CMAKE_CURRENT_LIST_DIR}/main
    ${CMAKE_CURRENT_LIST_DIR}   # For tusb_config.h
)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

# Linking to FreeRTOS-Kernel-Heap4 to use a dynamic heap for allocations
target_link_libraries(${PROJECT_NAME} PRIVATE
    freertos_config
    FreeRTOS-Kernel
    pico_async_context_freertos
    FreeRTOS-Kernel-Heap4
    pico_stdlib

    # Usb
    tinyusb_device
    tinyusb_board

    hardware_spi
    hardware_dma

    # cmsis headers
    cmsis_core
)

#target_compile_options(Client_program PRIVATE "-include" "${CMAKE_CURRENT_LIST_DIR}/modules/pal/pal_stdlib.h")

if(TARGET Client_program)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-Wl,--whole-archive"
        Client_program
        "-Wl,--no-whole-archive"
    )
endif()

target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,--no-whole-archive"
)

pico_add_extra_outputs(${PROJECT_NAME})

# Extracts the client program binary into a client_payload.bin file
add_custom_command(TARGET pico_vpx POST_BUILD
    # Notice the --set-section-flags argument added here!
    COMMAND ${CMAKE_OBJCOPY} --set-section-flags .vram_section=alloc,load,contents -O binary -j .vram_section $<TARGET_FILE:pico_vpx> $<TARGET_FILE_DIR:pico_vpx>/client_payload.bin
    COMMENT "Extracting demand-paged binary for Linux host..."
)