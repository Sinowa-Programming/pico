#include "client_map.h"

#include "pal.h"    // Get the sram frames
#include "mpu_config.h"
#include "hardware/sync.h"
#include "RP2350.h"

// Limit visibility to only this file
namespace {
    CLIENT::main_func_t client_main = nullptr;
}

TaskHandle_t CLIENT::client_task_tcb = NULL;

// Spin lock program when nothing is happening.
__attribute__((noinline, noclone)) void default_func(void) {
    while(1);
}

// Placed immediately after the payload to calculate the size of the compiled function.
__attribute__((noinline, noclone)) void default_func_end(void) {
    __asm volatile("nop"); // Prevent the compiler from optimizing this away
}

void CLIENT::start_client_task() {
    uintptr_t start_addr = (uintptr_t)default_func & ~1UL;   // Remove 1 LSB to get actual address
    uintptr_t end_addr   = (uintptr_t)default_func_end & ~1UL;

    // Load the default function into the first frame
    memcpy(vmm.sram_frames[0], (const void*)start_addr, end_addr - start_addr);

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

void CLIENT::load_frame(uintptr_t physical_addr) {
    __DSB();
    __ISB();

    // Indicate thumb mode for arm execution( not having this leads to a hard fault )
    uintptr_t thumb_address = physical_addr | 1;

    // Cast the address to a function pointer
    client_main = (main_func_t)thumb_address;
}

void CLIENT::client_task(void* pvParameters) {
    // Load frame 0 busy loop
    // Pass the address of frame 0 (not its first byte)
    load_frame((uintptr_t)vmm.sram_frames[0]);

    client_main();  // execute the code
    vTaskDelete(NULL);
}
