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
volatile uint32_t saved_original_mmfar[2];  // In cases when an offset is applied, adjusting just the base register won't work
volatile FaultType saved_fault_type[2];

extern "C" __attribute__((naked)) void isr_memfault(void);
extern "C" __attribute__((naked)) void vmm_fault_trampoline(void);
void core1_fifo_isr();

volatile MpuCommand pending_mpu_cmd;

void core1_setup()
{
    exception_set_exclusive_handler( MEMMANAGE_EXCEPTION, isr_memfault);
    irq_set_priority(MEMMANAGE_EXCEPTION, 0);

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
        if(cmd == 0 || cmd->region >= 4) {
            return;
        }
        if (cmd->clear) {
            mpu_clear_region(cmd->region);
        } else {
            set_addr(cmd->region, cmd->base_addr, cmd->limit_addr, cmd->access, cmd->execute);
        }
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
    // With 0, it will deny any access to unassigned mpu regions
    ARM_MPU_Enable(0);
}

void configure_core1_static_regions()
{
    const int MPU_REGION_FIRMWARE_CODE_DATA  = 7;
    const int MPU_REGION_API_TABLE           = 6;
    const int MPU_REGION_SIO                 = 5;
    const int MPU_REGION_PERI                = 4;

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

    // ALWAYS ALLOW: API Jump Table + Core 1 Stack (SCRATCH_X)
    // Origin: 0x2007F000 | Limit: 0x20080FFF (4KB Chunk)
    // Execution: ALLOWED( It contains pointers, but the asm branches to it.) | Access: ALLOWED
    // Note: Core 0 (SCRATCH_Y) is unmapped because Core 0 does not use the MPU.
    set_addr(
        MPU_REGION_API_TABLE, 
        0x2007F000, 
        0x20080FFF, 
        true, 
        true
    );

    // ALWAYS ALLOW: SIO
    // Origin: 0xD0000000 | Limit: 0xD00001FF
    // Execution: ALLOWED | Access: ALLOWED
    // Note: This is where the fifos are.
    set_addr(
        MPU_REGION_SIO, 
        0xD0000000,
        0xD00001FF,
        true, 
        true
    );

    // ALWAYS ALLOW: Peripherals
    // Origin: 0x40000000 | Limit: 0x5FFFFFFF
    // Execution: ALLOWED | Access: ALLOWED
    // Note: Timers, GPIO, UART, etc.
    set_addr(
        MPU_REGION_PERI, 
        0x40000000,
        0x5FFFFFFF,
        true, 
        true
    );

}

void set_addr(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access, bool execute) {
    uint32_t rbar = 0;
    uint32_t rlar = 0;
    if (access) {
        // Read/Write Allowed
        rbar = ARM_MPU_RBAR(base_address, ARM_MPU_SH_INNER, 0, 1, (execute) ? 0 : 1);
        rlar = ARM_MPU_RLAR(limit_address, 0); // Normal Memory Attributes
    } else {
        // STRICT BLOCK: No Access (AP=0), Non-Shareable, Readonly, Privileged, Execution Never (XN=1)
        // Attribute Index 1 points to Device Memory (nGnRnE) which safely faults on unexpected access
        rbar = ARM_MPU_RBAR(base_address, ARM_MPU_SH_NON, 1, 0, 1); 
        rlar = ARM_MPU_RLAR(limit_address, 1);
    }

    ARM_MPU_SetRegion(region_number, rbar, rlar);

    // Make sure the MPU register writes are complete
    __DSB();
    __ISB();
}

// Tell the compiler not to generate standard prologue/epilogue
extern "C" __attribute__((naked)) void isr_memfault(void) {
    __asm volatile (
        " tst lr, #4            \n"     // if (lr & 4) { // Check bit 2
        " ite eq                \n"     //      // (if then else instruction)
        " mrseq r0, msp         \n"     //      r0 = Main Stack Pointer
                                        // } else {
        " mrsne r0, psp         \n"     //      r0 = Process Stack Pointer
                                        // }
        " stmdb r0!, {r4-r11}   \n"     // Save r11 - r4 to the stack pointed at by r0. r0 now points at r4's location.


        " tst lr, #4            \n"     // if (lr & 4) { // Check bit 2
        " ite eq                \n"     //      // (if then else instruction)
        " msreq msp, r0         \n"     //      msp = r0 // r0 points to r4 now!
                                        // } else {
        " msrne psp, r0         \n"     //      psp = r0 // r0 points to r4 now!
                                        // }
        
        " push {r3, lr}         \n"     // Push lr to the stack. r3 needed for 8 bit alignment
        " mov r1, lr            \n"     // r1 = lr

        // r0 = address of the start of CpuSoftwareFrame
        // r1 = EXC_RETURN
        " bl MemManage_Handler_C                    \n"

        " pop {r3, lr}          \n"     // Restore lr

        " tst lr, #4            \n"     // if (lr & 4) { // Check bit 2
        " ite eq                \n"     //      // (if then else instruction)
        " mrseq r0, msp         \n"     //      r0 = Main Stack Pointer
                                        // } else {
        " mrsne r0, psp         \n"     //      r0 = Process Stack Pointer
                                        // }

        " ldmia r0!, {r4-r11}   \n"     // pop r4 - r11 to the stack pointed at by r0. r0 now points at r11's location.

        " tst lr, #4            \n"     // if (lr & 4) { // Check bit 2
        " ite eq                \n"     //      // (if then else instruction)
        " msreq msp, r0         \n"     //      msp = r0 // r0 points to r4 now!
                                        // } else {
        " msrne psp, r0         \n"     //      psp = r0 // r0 points to r4 now!
                                        // }

        " bx lr                 \n"
    );
}

// Helper to get a pointer from any frame register
inline static uint32_t* get_reg_ptr(CpuSoftwareFrame *sw_frame, CpuHardwareFrame *hw_frame, uint8_t reg_index) {
    switch (reg_index) {
        // Hardware-saved registers
        case 0:  return &hw_frame->r0;
        case 1:  return &hw_frame->r1;
        case 2:  return &hw_frame->r2;
        case 3:  return &hw_frame->r3;
        case 12: return &hw_frame->r12;
        case 14: return &hw_frame->lr;
        case 15: return &hw_frame->pc;

        // Software-saved registers
        case 4:  return &sw_frame->r4;
        case 5:  return &sw_frame->r5;
        case 6:  return &sw_frame->r6;
        case 7:  return &sw_frame->r7;
        case 8:  return &sw_frame->r8;
        case 9:  return &sw_frame->r9;
        case 10: return &sw_frame->r10;
        case 11: return &sw_frame->r11;

        default: return NULL; // R13 (SP) or invalid
    }
}

// This name is a standard CMSIS defined exception handler.
// The linker automatically maps it to the vector table.
extern "C" void MemManage_Handler_C(CpuSoftwareFrame *sw_frame, uint32_t exc_return) {
    ws2812_send_pixel(0,0,255);
    uint32_t core_id = get_core_num();

    // Determine where the Hardware Frame begins on the stack.
    // Should sit 32 bytes above the software frame.
    uintptr_t hw_frame_addr = (uintptr_t)sw_frame + sizeof(CpuSoftwareFrame);
    if((exc_return & (1 << 4)) == 0) {
        hw_frame_addr += 72;    // Skip s0-s15 + fpsrc
    }

    CpuHardwareFrame *hw_frame = (CpuHardwareFrame *)hw_frame_addr;

    // Check for Instruction Fetch Fault (PC ticked over the frame boundary)
    if (SCB->CFSR & SCB_CFSR_IACCVIOL_Msk) {
        saved_fault_type[core_id] = FAULT_IACCVIOL;
        saved_fault_pc[core_id] = hw_frame->pc;

        // Hijack PC to jump to trampoline
        hw_frame->pc = (uint32_t)&vmm_fault_trampoline | 1;

        // Clear the exception bit
        SCB->CFSR |= SCB_CFSR_IACCVIOL_Msk;
        return;
    }

    // Check if the MMFAR (Fault Address Register) is valid
    if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) {
        uint32_t fault_addr = SCB->MMFAR;
        saved_original_mmfar[core_id] = fault_addr;

        uint32_t frames_start = (uint32_t)&__vmm_frames_start;
        uint32_t frames_end = frames_start + sizeof(VMM::sram_frames);

        if(1) {//fault_addr >= VIRTUAL_MEMORY_BASE || (fault_addr >= frames_start && fault_addr <= frames_end)) {
            saved_fault_type[core_id] = FAULT_MMARVALID;
            saved_fault_addr[core_id] = fault_addr;
            saved_fault_pc[core_id] = hw_frame->pc;

            // Hijack PC to jump to trampoline
            hw_frame->pc = (uint32_t)&vmm_fault_trampoline | 1;

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


extern "C" uint32_t vmm_fault_handler_thread_mode(CpuSoftwareFrame *sw_frame, CpuHardwareFrame *hw_frame) {
    ws2812_send_pixel(0,0,255);
    uint32_t core_id = get_core_num();

    // -- Handle Instruction Fetch Fault --
    if (saved_fault_type[core_id] == FAULT_IACCVIOL) {
        uintptr_t faulted_physical_pc = saved_fault_pc[core_id];

        // Find the frame that the CPU thought it was entering
        // uint32_t previous_frame_idx = (faulted_physical_pc - (uintptr_t)vmm.sram_frames) / PAGE_SIZE - 1;
        uint32_t new_virtual_addr = vmm.get_vaddr_from_paddr(faulted_physical_pc);

        // Calculate the NEW virtual address the PC wants to execute
        // uint32_t new_virtual_addr = previous_virtual_page + PAGE_SIZE;

        // Enable execution + access of the at the new address
        vmm.access(new_virtual_addr, VMM::MpuRegionSlot::SLOT_EXEC);
        uintptr_t new_physical_addr = vmm.get_physical_ptr(new_virtual_addr);

        // Return the NEW target PC so the assembly trampoline jumps directly to it
        return new_physical_addr | faulted_physical_pc | 1;
    }
    
    // -- Handle Data Access Fault --
    else if (saved_fault_type[core_id] == FAULT_MMARVALID) {
        uint32_t fault_address = saved_fault_addr[core_id];

        // Get the register holding the faulty address
        uint8_t faulting_reg_index = decode_instruction_base_register(saved_fault_pc[core_id]);

        // Get the register pointer (modifying the stacked frame directly)
        uint32_t *target_reg = get_reg_ptr(sw_frame, hw_frame, faulting_reg_index);
        // assert(target_reg != NULL);

        // Update the register in the stacked frame so ( physical_addr + offset == target_addr). Reset it
        // before the offset was applied by the instruction(this is for offsetting instructions like
        // ldr r3, [r4, #4] when they cross over the page boundary).
        // In the case that the offset is what causes the page fault, adjust the base address so
        // ( (base_addr - adjustment_offset) + offset = target_addr )
        if (target_reg != NULL) {
            uint32_t cur_reg_val = *target_reg;
            uint32_t frames_start = (uint32_t)&__vmm_frames_start;
            uint32_t frames_end = frames_start + sizeof(VMM::sram_frames);
            uint32_t index_offset = (fault_address - cur_reg_val);  // For array accesses. Ex: arr[23] will have an offset of 23 * sizeof(type)

            // Return the virtual base if the physical base has been shifted. Else return virt_addr_base( which is either already virtual or a physical frame)
            uint32_t virt_addr_base = vmm.resolve_alias_to_virtual_base(cur_reg_val);
            if(virt_addr_base != cur_reg_val && index_offset == 0) {
                // If it resolved and there is no offset, then there is no need for the mapping anymore
                vmm.remove_alias_to_virtual_base(cur_reg_val);
                *target_reg = vmm.get_physical_ptr(virt_addr_base); // Restore the original physical address
            }

            // If the base address is still physical, then it hasn't been shifted.
            // Make the virt_addr_base virtual is it isn't already.
            if (virt_addr_base >= frames_start && virt_addr_base <= frames_end) {
                virt_addr_base = vmm.get_vaddr_from_paddr(virt_addr_base);
            }

            // uint32_t intended_vaddr = fault_address;
            uint32_t intended_vaddr = virt_addr_base + index_offset; // virt_addr_base + offset

            vmm.access(intended_vaddr, VMM::MpuRegionSlot::SLOT_DATA);
            

            // Only apply the offset backwards if it crosses a page boundary. Mask the lower inter-page bits away.
            uint32_t orig_fault_addr = saved_original_mmfar[core_id];
            if ((cur_reg_val & ~(PAGE_SIZE - 1)) != (orig_fault_addr & ~(PAGE_SIZE - 1))) {   // Pages are different
                 // Find out how large the offset it. Ex: the "i" in arr[i]
                 // target_addr = base_addr w/offset - (New Page loc - Old page loc)
                *target_reg = vmm.get_physical_ptr(intended_vaddr) - (orig_fault_addr - cur_reg_val);
                vmm.register_address_alias(virt_addr_base, *target_reg);
            } else if (!(cur_reg_val >= frames_start && cur_reg_val <= frames_end)) { // No boundary crossed. Directly translate as virtual and in page bounds
                *target_reg = vmm.get_physical_ptr(cur_reg_val);
            }
        }

        // Return the ORIGINAL target PC to retry the instruction
        return saved_fault_pc[core_id] | 1; 
    }

    return saved_fault_pc[core_id] | 1;
}

extern "C" __attribute__((naked)) void vmm_fault_trampoline(void) {
    __asm volatile (
        // 64 bytes = sizeof(CpuSoftwareFrame) + sizeof(CpuHardwareFrame)
        //          = 32 bytes + 24 bytes(r0-r3, r12, lr) + 4 bytes padding + 4 bytes target pc
        " sub sp, sp, #64          \n"

        // Save software frames
        " str r4,   [sp, #0]       \n"
        " str r5,   [sp, #4]       \n"
        " str r6,   [sp, #8]       \n"
        " str r7,   [sp, #12]      \n"
        " str r8,   [sp, #16]      \n"
        " str r9,   [sp, #20]      \n"
        " str r10,  [sp, #24]      \n"
        " str r11,  [sp, #28]      \n"

        // Save hardware frames
        " str r0,   [sp, #32]      \n"
        " str r1,   [sp, #36]      \n"
        " str r2,   [sp, #40]      \n"
        " str r3,   [sp, #44]      \n"
        " str r12,  [sp, #48]      \n"
        " str lr,   [sp, #52]      \n"
        // [sp, #56] is PC. Return will overwrite it.
        // [sp, #60] is xPSR 

        
        " mov r0, sp                        \n" // Arg 0 = Pointer to CpuSoftwareFrame
        " add r1, sp, #32                   \n" // Arg 1 = Pointer to CpuHardwareFrame
        " bl vmm_fault_handler_thread_mode  \n"

        // New PC was returned in r0
        " str r0,   [sp, #60]      \n"

        // Restore registers
        " pop {r4-r11}             \n" // Restores software frame, SP += 32
        " pop {r0-r3, r12, lr}     \n" // Restores hardware frame, SP += 24
        " add sp, sp, #4           \n" // Skip the 4 bytes of padding, SP += 4
        " pop {pc}                 \n" // Pop the Target PC directly into the Program Counter
                                       // SP += 4. (Total stack restored = 64 bytes)
    );
}