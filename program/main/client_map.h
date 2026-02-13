#ifndef CLIENT_MAP_H
#define CLIENT_MAP_H

#include "FreeRTOS.h"
#include "task.h"
#include "hardware/sync.h"


#define CLIENT_CORE_AFFINITY  (1U << 1) // Core 1
#define CLIENT_PRIORITY         (configMAX_PRIORITIES - 1) // High priority recommended
// Calculate size in WORDS (250KB = 256,000 bytes / 4 = 64,000 words)
const int CLIENT_TASK_STACK_SIZE = 64000;

typedef void (*main_func_t)(void);

extern TaskHandle_t client_task_tcb;

void start_client_task();

void load_frame(uintptr_t addr, main_func_t &client_main);

void client_task(void* pvParameters);    // The task that is running on the PAL

#endif // CLIENT_MAP_H