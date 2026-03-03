#include <pico/stdio.h>
#include <pico/stdlib.h>
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pal.h"
#include "usb_comm.h"
#include "memory.hpp"

ExternalMemory external_memory;
VMM vmm;
uint32_t VIRTUAL_CODE_MEMORY_END = 0xFFFFFFFF;

int main()
{
    usb_comm_setup();

    vmm = VMM();
    external_memory = ExternalMemory(&vmm, (uint32_t)10);
    vmm.add_external_memory(&external_memory);

    vTaskStartScheduler();
    return 0;
}

