#include "client_map.h"

#include "pal.h"    // Get the sram frames
#include "mpu_config.h"
#include "hardware/sync.h"
#include "RP2350.h"

#include "debug_led.h"

// Limit visibility to only this file
namespace {
    CLIENT::main_func_t client_main = nullptr;

    StackType_t client_task_stack[CLIENT::CLIENT_TASK_STACK_SIZE] __attribute__((aligned(32)));

    // Define the initial MPU regions for the task (VMM will populate them)
    const MemoryRegion_t initial_regions[portNUM_CONFIGURABLE_REGIONS] = {
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 }
    };
}

TaskHandle_t CLIENT::client_task_tcb = NULL;


void CLIENT::start_client_task() {
    if(client_task_tcb != NULL) {
        vTaskDelete(client_task_tcb);
    }

    TaskParameters_t client_task_parameters = {
        .pvTaskCode = client_task,
        .pcName = "client_task",
        .usStackDepth = CLIENT_TASK_STACK_SIZE,
        .pvParameters = NULL,
        .uxPriority = CLIENT_PRIORITY | portPRIVILEGE_BIT, // Remove privilege bit for user mode
        .puxStackBuffer = client_task_stack,
        .xRegions = {
            initial_regions[0],
            initial_regions[1],
            initial_regions[2]
        }
    };

    xTaskCreateRestricted(&client_task_parameters, &client_task_tcb);
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

    // It is expected that a program has been loaded using load_frame
    if (client_main != nullptr) {
        ws2812_send_pixel(100,100,100);
        client_main();  // execute the code
    }


    while(1) {
        _vprintf("Hello from test_program_main!");

        _vsleep(1000);
    }

    vTaskDelete(NULL);
}
