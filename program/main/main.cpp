#include <pico/stdio.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pal.h"
#include "pal_stdlib.h"
#include "usb_comm.h"
#include "memory.hpp"
#include "client_map.h"

#include "debug_led.h"

ExternalMemory external_memory;
VMM vmm;
VFM vfm;

// The API table the client will use
extern const FirmwareJMPTable api_table;

int main()
{
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Setup the external communication
    ws2812_send_pixel(0, 0, 0);
    usb_comm_setup();
    sleep_ms(500);
    ws2812_send_pixel(255, 0, 0);
    sleep_ms(500);

    // Setup the memory managers
    ws2812_send_pixel(0, 0, 0);
    vmm = VMM();
    vfm = VFM();
    external_memory = ExternalMemory(&vmm, &vfm, (uint32_t)10);
    vmm.add_external_memory(&external_memory);

    // Start the memory managers
    external_memory.start();
    vmm.start();

    sleep_ms(500);
    ws2812_send_pixel(0, 0, 255);
    sleep_ms(500);

    // Start the core 1 task
    ws2812_send_pixel(0, 0, 0);
    CLIENT::setup_client_task();
    sleep_ms(500);
    ws2812_send_pixel(0, 255, 0);

    // Setup the API table pointer
    FirmwareJMPTable* client_api = FW_API;
    memcpy(client_api, &api_table, sizeof(FirmwareJMPTable));

    // Start the processor
    vTaskStartScheduler();

    return 0;
}

extern "C" {
/* configSUPPORT_STATIC_ALLOCATION is set to 1, so the application must provide an
implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/* Required for the SMP (Multi-core) port of FreeRTOS for the secondary core */
// void vApplicationGetPassiveIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
//                                            StackType_t **ppxIdleTaskStackBuffer,
//                                            uint32_t *pulIdleTaskStackSize,
//                                            BaseType_t xPassiveCoreNum )
// {
//     /* RP2350 has 2 cores, so we only need 1 passive idle task */
//     static StaticTask_t xIdleTaskTCB;
//     static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

//     *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
//     *ppxIdleTaskStackBuffer = uxIdleTaskStack;
//     *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
// }

/* configSUPPORT_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

}