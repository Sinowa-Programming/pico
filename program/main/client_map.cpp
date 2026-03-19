#include "client_map.h"

#include "pal.h"    // Get the sram frames
#include "mpu_config.h"
#include "hardware/sync.h"
#include "RP2350.h"

#include "/workspaces/pico/ported_programs/dav1d_pico_interface/dav1dplay.h"
TaskHandle_t client_task_tcb;

// Spin lock program when nothing is happening.
__attribute__((noinline, noclone)) void default_func(void) {
    while(1);
}

// Placed immediately after the payload to calculate the size of the compiled function.
__attribute__((noinline, noclone)) void default_func_end(void) {
    __asm volatile("nop"); // Prevent the compiler from optimizing this away
}

void start_client_task() {
    uintptr_t start_addr = (uintptr_t)default_func & ~1UL;   // Remove 1 LSB to get actual address
    uintptr_t end_addr   = (uintptr_t)default_func_end & ~1UL;

    // Load the default function into the first frame
    memcpy(&(vmm.sram_frames[0]), (const void*)start_addr, end_addr - start_addr);

    if(client_task_tcb != NULL) {
        vTaskDelete(client_task_tcb);
    }

    xTaskCreate(
        client_task,
        "client_task",
        CLIENT_TASK_STACK_SIZE,
        NULL,
        CLIENT_PRIORITY,
        &client_task_tcb
    );

    vTaskCoreAffinitySet(client_task_tcb, CLIENT_CORE_AFFINITY);
}

void load_frame(uintptr_t addr, main_func_t &client_main) {
    __DSB();
    __ISB();

    // Indicate thumb mode for arm execution( not having this leads to a hard fault )
    uintptr_t thumb_address = addr | 1;

    // Set the region to allow execution of the function
    set_addr_exec(0, thumb_address, thumb_address + PAGE_SIZE, true);

    // Cast the address to a function pointer
    client_main = (main_func_t)thumb_address;
}

void client_task(void* pvParameters) {
    main_func_t client_main;    // Create the function to load

    // Load frame 0 busy loop
    // load_frame(*(vmm.sram_frames[0]), client_main);

    // client_main();  // execute the code
    char *argv[] = {
        "/home/sinowa/Programming/device_dev/Pico Array/pico/",  // The current working directory of the program
        "-i",    // Input file command
        "test-420-8.ivf"    // The test file to decode
    };
    dav1dplay_main(3, argv);

    vTaskDelete(NULL);
}