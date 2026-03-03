#include "mpu_config.h"

void configure_rp2350_mpu()
{
    // Enables the MemManage Fault( The MPU's page fault ).
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
}


void set_addr(uint8_t region_number, uint32_t base_address, uint32_t limit_address, bool access) {
    uint32_t rbar = 0;
    uint32_t rlar = 0;
    if(access) {
        // RO=0 (Read/Write), NP=1 (Unprivileged allowed), XN=1 (No execution)
        rbar = ARM_MPU_RBAR(base_address, ARM_MPU_SH_NON, ARM_MPU_AP_RW, ARM_MPU_AP_NP, ARM_MPU_EX);
        // MAIR Index 1 (Assuming Device Memory / Peripherals)
        rlar = ARM_MPU_RLAR(limit_address, 1);
    }

    taskENTER_CRITICAL();
    if (access) {
        ARM_MPU_SetRegion(region_number, rbar, rlar);
    } else {
        ARM_MPU_ClrRegion(region_number);   // If cleared and no default background region is enabled then it will be no access
    }

    // Make sure the MPU register writes are complete
    __DSB();
    __ISB();

    taskEXIT_CRITICAL();
}

// Tell the compiler not to generate standard prologue/epilogue
__attribute__((naked)) void MemManage_Handler(void) {
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
void MemManage_Handler_C(StackFrame *frame) {
    // Check if the MMFAR (Fault Address Register) is valid
    if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) {
        uint32_t fault_address = SCB->MMFAR;
        vmm.access(fault_address);   // Make sure the page is resident( load it if not. )
        uintptr_t physical_addr = vmm.get_physical_ptr(fault_address);   // Get the address of the frame

        // Update the offending register/sram location with the new address

        // 1. You must decode the instruction at frame->pc to find the base register index.
        // Let's assume your decoder returns the register index (0-15) that caused the fault.
        uint8_t faulting_reg_index = decode_instruction_base_register(frame->pc);

        // 2. Update the offending register with the new physical address
        uint32_t *target_reg = get_reg_ptr(frame, faulting_reg_index);
        if (target_reg != NULL) {
            *target_reg = physical_addr;
        }

        // Clear the MMFAR valid bit so it doesn't immediantly refire
        SCB->CFSR |= SCB_CFSR_MMARVALID_Msk;
    } else {
        while(1) {
            sleep_ms(1000);
            printf("Memory fault! Unknown Address. Time to spinlock!\n");
        }
    }
}
