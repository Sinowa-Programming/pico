#include "client_map.h"

#include "pal.h"    // Get the sram frames
#include "mpu_config.h"
#include "hardware/sync.h"
#include "RP2350.h"

#include "debug_led.h"

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
    while (get_core_num() != 1) {
        vTaskDelay(1);
    }

    // Make sure the MPU is enabled for Core1 (or whatever core the task is on).
    configure_rp2350_mpu();
    // If a frame has not been loaded via the control path, fall back
    // to frame 0. Normally the loader will call `load_frame()` prior
    // to starting the task so we avoid unconditionally overwriting
    // the prepared entry point here.
    if (client_main != nullptr) {
        load_frame((uintptr_t)vmm.sram_frames[0]);
        client_main();  // execute the code
    }


    while(1) {
        // const char *log = "Hello from the client program run block!";
        // MemoryRequest req = {
        //     .op = MemoryOp::LOG,
        //     .buffer = (uint8_t *)log,
        // };
        // external_memory.submit_request(req);

        _vprintf("Hello from the client program run block!");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}
