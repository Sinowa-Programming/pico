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

extern uint32_t __etext;
extern uint32_t __vmm_frames_start;

void core1_setup();
void configure_rp2350_core1_mpu();
void configure_core1_static_regions();

// Sent to core 1 to config it's MPU
struct MpuCommand {
    uint32_t region;
    uint32_t base_addr;
    uint32_t limit_addr;
    bool access;
    bool execute;
    bool clear;
};

extern volatile MpuCommand pending_mpu_cmd;
extern volatile bool mpu_ack_flag;

inline void mpu_clear_region(uint16_t region_number)
{
    ARM_MPU_ClrRegion(region_number);
};

void set_addr(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access, bool execute);

// The hardware and software registers have been split because the FPU on the Arm M33
// dynamically stacks additional registers if the FPU was being used by the thread
// during the fault.
typedef struct {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
} CpuSoftwareFrame;

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} CpuHardwareFrame;

#endif  // MPU_CONFIG_H
