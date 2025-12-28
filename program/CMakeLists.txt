# Example running FreeRTOS on 2 cores
add_executable(slave_processor
    main/main.cpp
    )

pico_set_binary_type(slave_processor no_flash)

# Linking to FreeRTOS-Kernel-Heap4 to use a dynamic heap for allocations
target_link_libraries(slave_processor PRIVATE
    pico_async_context_freertos
    FreeRTOS-Kernel-Heap4
    pico_stdlib
    )

target_include_directories(slave_processor PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/..
    )

# Change the configuration to just use static RAM allocation
# If configSUPPORT_DYNAMIC_ALLOCATION is left undefined it will default to 1
# If configSUPPORT_STATIC_ALLOCATION is left undefined it will default to 0
target_compile_definitions(slave_processor PRIVATE
    configSUPPORT_STATIC_ALLOCATION=0
    configSUPPORT_DYNAMIC_ALLOCATION=1
    )

# In case, I later want the bin, elf, hex...etc
# pico_add_extra_outputs(slave_processor)