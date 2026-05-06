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


void CLIENT::start_client_task() {
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
    // Make sure to all data and instruction accesses are completed
    __DSB();
    __ISB();

    // Cast the address to a function pointer + Make it a thumb instruction
    client_main = (main_func_t)(physical_addr | 1);
}

void CLIENT::client_task(void* pvParameters) {
    // Load frame 0 busy loop
    // Pass the address of frame 0 (not its first byte)
    load_frame((uintptr_t)vmm.sram_frames[0]);

    client_main();  // execute the code
    vTaskDelete(NULL);
}
