#ifndef WRAPPER_PTHREAD_EXTENSION
#define WRAPPER_PTHREAD_EXTENSION

#include <pico/stdlib.h>

// Replaces TaskHandle_t
/**
 * Each MCU is only setup to run on one program at a time on core 1. This
 * means that to access a task, all you need is the program's id, which is assigned
 * when start_client_task() is called.
 */
typedef struct {
    uint8_t mcu_id;
} PicoTaskHandle_t;


#endif  // WRAPPER_PTHREAD_EXTENSION