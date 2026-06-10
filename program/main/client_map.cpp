#include "client_map.h"

#include "pal.h"    // Get the sram frames
#include "mpu_config.h"
#include "hardware/sync.h"
#include "RP2350.h"
#include "pico/multicore.h"

#include "debug_led.h"

// Limit visibility to only this file
namespace {
    CLIENT::main_func_t client_main = nullptr;
}

volatile bool CLIENT::task_enabled = false;


void CLIENT::setup_client_task(){
    multicore_reset_core1();
    multicore_launch_core1(client_task);
}

void CLIENT::start_client_task() {
    task_enabled = true;
    __DMB();
}

void CLIENT::load_frame(uintptr_t physical_addr) {
    // Make sure to all data and instruction accesses are completed
    __DSB();
    __ISB();

    // Cast the address to a function pointer + Make it a thumb instruction
    client_main = (main_func_t)(physical_addr | 1);
}

void CLIENT::client_task() {
    while (get_core_num() != 1) {
        __asm volatile("nop");
    }
    
    core1_setup();
    
    task_enabled = false;
    __DMB();
    
    while(!task_enabled);

    if(client_main != nullptr) {
        ws2812_send_pixel(100,100,100);
        client_main();  // execute the code
    }

    while(1) {
        _vprintf("The client program has closed or is null.");

        _vsleep(1000);
    }
}
