#ifndef CLIENT_MAP_H
#define CLIENT_MAP_H

#include "FreeRTOS.h"
#include "task.h"

#define CLIENT_PRIORITY         (configMAX_PRIORITIES - 1) // High priority recommended

namespace CLIENT {
    // Calculate size in WORDS (100KB = 100,000 bytes / 4 = 25,000 words)
    const int CLIENT_TASK_STACK_SIZE = 5000;//25000;

    typedef void (*main_func_t)(void);

    extern TaskHandle_t client_task_tcb;

    void start_client_task();

    void load_frame(uintptr_t physical_addr);

    void client_task(void* pvParameters);    // The task that is running on the PAL
}
#endif // CLIENT_MAP_H