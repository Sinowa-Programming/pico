#include "client_map.h"

#include "pal.h"    // Get the sram frames

TaskHandle_t client_task_tcb;

void start_client_task() {
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
    // Until I find out what registers to clear to invalidate the Instruction cache, I will just leave it commented out.
    // __dsb();
    // SCB_InvalidateICache();
    // __isb();

    // Indicate thumb mode for arm execution( not having this leads to a hard fault )
    uintptr_t thumb_address = addr | 1;

    // Cast the address to a function pointer
    client_main = (main_func_t)thumb_address;
}

void client_task(void* pvParameters) {
    main_func_t client_main;

    // Load frame 0 busy loop
    load_frame(*(vmm.sram_frames[0]), client_main);

    client_main();  // execute the code

    vTaskDelete(NULL);
}