#include "mpu_config.h"
#include "debug_led.h"
#include "pal.h"

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/exception.h"

enum FaultType {
    FAULT_IACCVIOL,
    FAULT_MMARVALID
};

// Saved the program counter and address for recall after vmm access
volatile uint32_t saved_fault_pc[2];
volatile uint32_t saved_fault_addr[2];
volatile FaultType saved_fault_type[2];
volatile StackFrame* saved_fault_frame[2]; // So thread mode can modify the stacked registers

extern "C" __attribute__((naked)) void isr_memfault(void);
extern "C" void vmm_fault_trampoline(void);
void core1_fifo_isr();

volatile MpuCommand pending_mpu_cmd;

void core1_setup()
{
    // MemManage Fault is vector index 4. Vector index - 16 = -12. Why do I even need to set this?
    exception_set_exclusive_handler( MEMMANAGE_EXCEPTION, isr_memfault);

    // Setup the mpu
    configure_core1_static_regions();
    configure_rp2350_core1_mpu();

    // Setup core 1's fifo isr
    uint irq_num = SIO_FIFO_IRQ_NUM(1);

    // Remove what stole my IRQ handler.
    irq_handler_t current_handler = irq_get_vtable_handler(irq_num);
    if (current_handler != __unhandled_user_irq) {
        irq_remove_handler(irq_num, current_handler);
    }

    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1), core1_fifo_isr);
    irq_set_priority(irq_num, 0x80);
    irq_set_enabled(SIO_FIFO_IRQ_NUM(1), true);
}

volatile bool mpu_ack_flag = false;
void core1_fifo_isr() {
    while (multicore_fifo_rvalid()) {
        uint32_t cmd_ptr = multicore_fifo_pop_blocking();
        MpuCommand* cmd = (MpuCommand*)cmd_ptr;
        if(cmd == 0 || cmd->region >= 5) {
            return;
        }
        set_addr_exec(cmd->region, cmd->base_addr, cmd->limit_addr, cmd->access);

        __DSB();
        __ISB();

        mpu_ack_flag = true;
        __DMB();
    }
    multicore_fifo_clear_irq();
}

void configure_rp2350_core1_mpu() {
    // The Memory Attribute Indirection Registers need to be setup before any regions can be setup.
    ARM_MPU_SetMemAttr(0, ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(1, 1, 1, 1), ARM_MPU_ATTR_MEMORY_(1, 1, 1, 1)));
    ARM_MPU_SetMemAttr(1, ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE, ARM_MPU_ATTR_DEVICE_nGnRnE));

    // Enables the MemManage Fault( The MPU's page fault ).
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;

    // Enable the actual MPU
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

void configure_core1_static_regions()
{
    const int MPU_REGION_FIRMWARE_CODE_DATA  = 7;
    const int MPU_REGION_API_TABLE      = 6;
    const int MPU_REGION_CORE1_STACK    = 5;

    // ALWAYS ALLOW: OS Vector Table, Code, and Constants
    //  OS Variables (Global state, FreeRTOS variables, API buffers)
    // Origin: 0x20000000 | Limit: The start if vmm.sram_frames[]
    // Execution: ALLOWED | Access: ALLOWED
    set_addr(
        MPU_REGION_FIRMWARE_CODE_DATA, 
        0x20000000, 
        (uint32_t)&__vmm_frames_start - 1, 
        true, 
        true
    );

    // ALWAYS ALLOW: API Jump Table
    // Origin: 0x2007F000 | Limit: 0x2007FFFF (4KB Chunk)
    // Execution: ALLOWED( It contains pointers, but the asm branches to it.) | Access: ALLOWED
    set_addr(
        MPU_REGION_API_TABLE, 
        0x2007F000, 
        0x2007FFFF, 
        true, 
        true
    );

    // ALWAYS ALLOW: Core 1 Stack (SCRATCH_X)
    // Origin: 0x20080000 | Limit: 0x20080FFF (4KB Chunk)
    // Execution: NOT ALLOWED | Access: ALLOWED
    // Note: Core 0 (SCRATCH_Y) is unmapped because Core 0 does not use the MPU.
    set_addr(
        MPU_REGION_CORE1_STACK, 
        0x20080000,
        0x20080FFF,
        true, 
        false
    );
}

void set_addr(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access, bool execute) {
    uint32_t rbar = 0;
    uint32_t rlar = 0;
    if(access) {
        // SH = 3 (Shareable. No d-cache so it doesn't do anything),
        // RO=0 (Read/Write), NP=1 (Privileged), XN=1 (No execution)
        rbar = ARM_MPU_RBAR(base_address, ARM_MPU_SH_INNER, 0, 1, (execute) ? 0 : 1);
        // 0 = normal memory( you can execute ). 1 = device memory( no execution )
        rlar = ARM_MPU_RLAR(limit_address, 0);
    }

    if (access) {
        ARM_MPU_SetRegion(region_number, rbar, rlar);
    } else {
        ARM_MPU_ClrRegion(region_number);   // If cleared and no default background region is enabled then it will be no access
    }

    // Make sure the MPU register writes are complete
    __DSB();
    __ISB();
}

// Tell the compiler not to generate standard prologue/epilogue
extern "C" __attribute__((naked)) void isr_memfault(void) {
    __asm volatile (
        // 1. Determine which stack was in use before the fault
        // LR (EXC_RETURN) bit 2 indicates if we came from MSP (0) or PSP (1)
        // #4 isolates bit 2 to determine the EXC_RETURN
        " tst lr, #4                                \n"
        " ite eq                                    \n"
        " mrseq r0, msp                             \n"
        " mrsne r0, psp                             \n"

        // 2. Push R4-R11 onto that stack to complete the frame
        // stmdb (Store Multiple Decrement Before) grows the stack downwards
        " stmdb r0!, {r4-r11}                       \n"

        // 3. Update the actual stack pointer so it's saved
        " tst lr, #4                                \n"
        " ite eq                                    \n"
        " msreq msp, r0                             \n"
        " msrne psp, r0                             \n"

        // 4. r0 now points to the bottom of our FullStackFrame. Call C.
        " bl MemManage_Handler_C                    \n"

        // 5. C is done. Get the stack pointer back into r0.
        " tst lr, #4                                \n"
        " ite eq                                    \n"
        " mrseq r0, msp                             \n"
        " mrsne r0, psp                             \n"

        // 6. Pop R4-R11 off the stack to update the hardware registers
        " ldmia r0!, {r4-r11}                       \n"

        // 7. Update the stack pointer back to where the hardware expects it
        " tst lr, #4                                \n"
        " ite eq                                    \n"
        " msreq msp, r0                             \n"
        " msrne psp, r0                             \n"

        // 8. Return from exception (CPU pops R0-R3, R12, LR, PC, xPSR)
        " bx lr                                     \n"
    );
}

// Helper to get a pointer from any frame register
inline static uint32_t* get_reg_ptr(StackFrame *frame, uint8_t reg_index) {
    switch (reg_index) {
        case 0: return &frame->r0;
        case 1: return &frame->r1;
        case 2: return &frame->r2;
        case 3: return &frame->r3;
        case 4: return &frame->r4;
        case 5: return &frame->r5;
        case 6: return &frame->r6;
        case 7: return &frame->r7;
        case 8: return &frame->r8;
        case 9: return &frame->r9;
        case 10: return &frame->r10;
        case 11: return &frame->r11;
        case 12: return &frame->r12;
        case 13:                        // R13 (SP) technically isn't here, but LR is 14
        case 14: return &frame->lr;
        case 15: return &frame->pc;
        default: return NULL;
    }
}

// This name is a standard CMSIS defined exception handler.
// The linker automatically maps it to the vector table.
extern "C" void MemManage_Handler_C(StackFrame *frame) {
    ws2812_send_pixel(0,0,255);
    uint32_t core_id = get_core_num();

    // Check for Instruction Fetch Fault (PC ticked over the frame boundary)
    if (SCB->CFSR & SCB_CFSR_IACCVIOL_Msk) {
        saved_fault_type[core_id] = FAULT_IACCVIOL;
        saved_fault_pc[core_id] = frame->pc;

        // Hijack PC to jump to trampoline
        frame->pc = (uint32_t)&vmm_fault_trampoline | 1;

        // Clear the exception bit
        SCB->CFSR = SCB_CFSR_IACCVIOL_Msk;
        return;
    }

    // Check if the MMFAR (Fault Address Register) is valid
    if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) {
        uint32_t fault_addr = SCB->MMFAR;
        if(fault_addr >= VIRTUAL_MEMORY_BASE) {
            saved_fault_type[core_id] = FAULT_MMARVALID;
            saved_fault_addr[core_id] = SCB->MMFAR;
            saved_fault_pc[core_id] = frame->pc;
            // Hijack PC to jump to trampoline
            frame->pc = (uint32_t)&vmm_fault_trampoline | 1;

            // Clear the exception bit + Data access violation
            SCB->CFSR |= SCB_CFSR_MMARVALID_Msk | SCB_CFSR_DACCVIOL_Msk;
            return;
        } else {
            while(1) {
                sleep_ms(1000);
                ws2812_send_pixel(255,0,0);
                sleep_ms(1000);
                ws2812_send_pixel(0,0,0);
            }
        }
    } 

    while(1) {
        sleep_ms(1000);
        // printf("Memory fault! Unknown Address. Time to spinlock!\n");
        ws2812_send_pixel(255,0,0);
        sleep_ms(1000);
        ws2812_send_pixel(0,0,0);
    }
}


extern "C" uint32_t vmm_fault_handler_thread_mode(StackFrame *frame) {
    ws2812_send_pixel(0,0,255);
    uint32_t core_id = get_core_num();

    // -- Handle Instruction Fetch Fault --
    if (saved_fault_type[core_id] == FAULT_IACCVIOL) {
        uintptr_t faulted_physical_pc = saved_fault_pc[core_id];

        // Find the frame that the CPU thought it was entering
        uint32_t previous_frame_idx = (faulted_physical_pc - (uintptr_t)vmm.sram_frames) / PAGE_SIZE - 1;
        uint32_t previous_virtual_page = vmm.get_vaddr_from_frame(previous_frame_idx);

        // Calculate the NEW virtual address the PC wants to execute
        uint32_t new_virtual_addr = previous_virtual_page + PAGE_SIZE;

        // Enable execution + access of the at the new address
        vmm.access(new_virtual_addr);
        uintptr_t new_physical_addr = vmm.get_physical_ptr(new_virtual_addr);

        // Return the NEW target PC so the assembly trampoline jumps directly to it
        return new_physical_addr | (faulted_physical_pc & 0x01);
    }
    
    // -- Handle Data Access Fault --
    else if (saved_fault_type[core_id] == FAULT_MMARVALID) {
        uint32_t fault_address = saved_fault_addr[core_id];

        // Get the register holding the faulty address
        uint8_t faulting_reg_index = decode_instruction_base_register(saved_fault_pc[core_id]);

        // Get the register pointer (modifying the stacked frame directly)
        uint32_t *target_reg = get_reg_ptr(frame, faulting_reg_index);

        if (target_reg != NULL) {
            vmm.access(fault_address);   
            uintptr_t physical_addr = vmm.get_physical_ptr(fault_address);   
            
            // Update the register in the stacked frame
            *target_reg = physical_addr;
        }

        // Return the ORIGINAL target PC to retry the instruction
        return saved_fault_pc[core_id] | 1; 
    }

    return saved_fault_pc[core_id] | 1;
}

// Reconstructs a full StackFrame struct so get_reg_ptr works
__attribute__((naked)) void vmm_fault_trampoline(void) {
    __asm volatile (
        // Allocate 8 bytes for 'xpsr' and 'pc'
        " sub sp, sp, #8 \n"

        " push {r12, lr} \n"

        // SP now points exactly to the start of a StackFrame struct!
        " push {r0-r3} \n"

        // 4. Pass SP as the first argument (r0) to the C function
        " mov r0, sp \n"

        // Call the C function. When it finishes, R0 will contain the target PC.
        " bl vmm_fault_handler_thread_mode \n"

        // Store the returned target PC into the 'pc' slot of our StackFrame.
        // (offset 24 is the PC slot: r0-r3=16, r12=4, lr=4 -> 16+4+4 = 24)
        " str r0, [sp, #24] \n"

        " pop {r0-r3, r12, lr} \n"
        " pop {pc} \n"
    );
}