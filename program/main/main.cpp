#include <pico/stdio.h>
#include <pico/stdlib.h>
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pal.h"
#include "usb_comm.h"
#include "memory.hpp"
#include "client_map.h"

ExternalMemory external_memory;
VMM vmm;

int main()
{
    // Setup the external communication
    usb_comm_setup();

    // Setup the memory managers
    vmm = VMM();
    external_memory = ExternalMemory(&vmm, (uint32_t)10);
    vmm.add_external_memory(&external_memory);

    // Start the client process
    start_client_task();

    // Start the processor
    vTaskStartScheduler();
    return 0;
}

