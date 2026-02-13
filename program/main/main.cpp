#include <pico/stdio.h>
#include <pico/stdlib.h>
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"
#include "pal.h"

#include "usb_comm.h"

ExternalMemory external_memory;
VMM vmm;

int main()
{
    usb_comm_setup();

    vmm = VMM();
    external_memory = ExternalMemory(&vmm, (uint32_t)10);
    vmm.add_external_memory(&external_memory);

    vTaskStartScheduler();
    return 0;
}

