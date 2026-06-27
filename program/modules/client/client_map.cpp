#include "client_map.h"

#include "pal.h"    // Get the sram frames
#include "mpu_config.h"
#include "hardware/sync.h"
#include "RP2350.h"
#include "pico/multicore.h"

#include "debug_led.h"

#include "hardware/irq.h"
#include "hardware/exception.h"


// Limit visibility to only this file
namespace {
    CLIENT::main_func_t client_main = nullptr;
}

volatile bool CLIENT::task_enabled = false;

void CLIENT::setup_client_task(){
    // Drain Core 0's RX FIFO to remove any stale data from previous runs/resets
    while (multicore_fifo_rvalid()) {
        (void)multicore_fifo_pop_blocking();
    }
    
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
    client_isr_setup();
    
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

void CLIENT::pause_client_isr()
{
    while(client_paused) {
        tight_loop_contents();
    }
}

volatile bool CLIENT::pause_on_client_store = false;
volatile bool CLIENT::external_mem_notify_completion = false;
volatile CLIENT::ClientPCBStatic client_pcb_snapshot;
volatile CLIENT::ClientPCBFPU client_pcb_fpu_snapshot;

void CLIENT::client_isr_setup()
{
    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1), store_client_isr);
    irq_set_priority(STORE_IRQ_NUM, 0x80);
    irq_set_enabled(SIO_FIFO_IRQ_NUM(1), true);

    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1), load_client_isr);
    irq_set_priority(LOAD_IRQ_NUM, 0x80);
    irq_set_enabled(SIO_FIFO_IRQ_NUM(1), true);

    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1), pause_client_isr);
    irq_set_priority(PAUSE_IRQ_NUM, 0xFF);  // So the load/store + FIFO isr can replace it.
    irq_set_enabled(SIO_FIFO_IRQ_NUM(1), true);
}


void CLIENT::store_client_isr()
{
    __asm volatile (
        " mrs r0, psp           \n"     // r0 = Process Stack Pointer

        " tst lr, #0x10         \n"     // if( (1 << 4) == 0) {
        " it eq                 \n"     //
        " vstmdbeq r0!, {s16-s31}   \n" //      Push s16-s31
                                        // }

        " stmdb r0!, {r4-r11}   \n"     // Save r11 - r4 to the stack pointed at by r0. r0 now points at r4's location.

        " mrs psp, r0           \n"     // psp = r0

        " push {r0, lr}         \n"     // Push lr to the MSP stack. r0 needed for 8 bit alignment
        " mov r1, lr            \n"     // r1 = exc_return

        // r0 = address of the start of CpuSoftwareFrame
        // r1 = EXC_RETURN
        "bl store_client_C"

        " pop {r0, pc}          \n"
    );
}

void CLIENT::store_client_C(CpuSoftwareFrame *sw_frame, uint32_t exc_return)
{
    client_pcb_snapshot.process_id = process_id;

    // Populate the ClientPCBFPU and ClientPCBStatic regs
    bool fpu_active = ((exc_return & (1 << 4)) == 0);
    client_pcb_snapshot.fpu_active = fpu_active;

    uintptr_t current_addr = (uintptr_t)sw_frame;

    memcpy((void *)&(client_pcb_snapshot.cpu_soft_regs), (void *)current_addr, sizeof(CpuSoftwareFrame));
    current_addr += sizeof(CpuSoftwareFrame);

    if (fpu_active) {
        memcpy((void *)&(client_pcb_fpu_snapshot.fpu_soft_regs), (void *)current_addr, sizeof(SoftwareFpuFrame));
        current_addr += sizeof(SoftwareFpuFrame);
    }

    memcpy((void *)&(client_pcb_snapshot.cpu_hard_regs), (void *)current_addr, sizeof(CpuHardwareFrame));
    current_addr += sizeof(CpuHardwareFrame);

    if (fpu_active) {
        memcpy((void *)&(client_pcb_fpu_snapshot.fpu_hard_regs), (void *)current_addr, sizeof(CpuFpuHardwareFrame));
    }

    // Populate remaining ClientPCBStatic values

    // Copy list of MPU enabled pages
    memcpy((void *)&(client_pcb_snapshot.active_pages), vmm.get_mpu_enabled(), sizeof(client_pcb_snapshot.active_pages));

    // Copy Address Map
    client_pcb_snapshot.addr_map_size = vmm.get_address_map()->get_element_count();
    memcpy((void *)client_address_map_snapshot, vmm.get_address_map(), sizeof(StaticAddressMap<VMM::ADJUSTED_ADDRESS_LIMIT>::AddressMap) * client_pcb_snapshot.addr_map_size);

    // Copy Virtual Files
    client_pcb_snapshot.open_file_cnt = vfm.get_open_file_cnt();
    memcpy((void *)client_virtual_file_snapshot, vfm.get_file_data(), sizeof(VirtualFile) * client_pcb_snapshot.open_file_cnt);

    // Send the Data to the External Memory manager to handle.
    MemoryRequest req {
        .op = MemoryOp::CLIENT_STORE,
        .arg1 = (uint32_t)&client_pcb_snapshot,
        .arg2 = (uint32_t)&client_pcb_fpu_snapshot,
        .arg3 = (uint32_t)&client_address_map_snapshot,
        .buffer = (uint8_t *)&client_virtual_file_snapshot,
        .task = NULL
    };
    external_memory.submit_request(req);
    external_mem_notify_completion = false;

    vmm.write_all_data();
    while(!external_mem_notify_completion) {
        tight_loop_contents();
    }

    if(pause_on_client_store) {
        client_paused = true;
        // Launch the pause isr
        scb_hw->icsr = (1 << CLIENT::PAUSE_IRQ_NUM);
    }
}

void CLIENT::load_client_isr()
{
    
}

void CLIENT::load_client_C()
{
}