#ifndef MPU_CONFIG_H
#define MPU_CONFIG_H

#include "pico/stdlib.h"
#include "hardware/structs/mpu.h"
#include "RP2350.h"

#include "FreeRTOS.h"
#include "task.h"

#include "memory.hpp"
#include "internal_memory.h"
#include "thumb2_instruction_decode.h"

extern class VMM vmm;

void configure_rp2350_mpu();

inline void set_addr_exec(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access);
inline void set_addr_nexec(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access);

// The stack frame that a fault handler is given
typedef struct {
    // Software saved registers
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    // Hardware saved registers (Pushed by the CPU)
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} StackFrame;

// Page fault interrupt handler
void MemManage_Handler(void);
void MemManage_Handler_C(StackFrame *frame);

#endif  // MPU_CONFIG_H
