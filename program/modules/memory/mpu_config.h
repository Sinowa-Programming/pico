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

#include "inter_core.h"

extern class VMM vmm;

extern uint32_t __etext;
extern uint32_t __vmm_frames_start;

void configure_rp2350_core1_mpu();
void configure_core1_static_regions();

inline void mpu_clear_region(uint16_t region_number)
{
    ARM_MPU_ClrRegion(region_number);
};

void set_addr(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access, bool execute);

// The hardware and software registers have been split because the FPU on the Arm M33
// dynamically stacks additional registers if the FPU was being used by the thread
// during the fault. The FPU frames are only pushed if the FPU is active.
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

typedef struct __attribute__((packed)) {
    float s16;
    float s17;
    float s18;
    float s19;
    float s20;
    float s21;
    float s22;
    float s23;
    float s24;
    float s25;
    float s26;
    float s27;
    float s28;
    float s29;
    float s30;
    float s31;
} SoftwareFpuFrame;

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

typedef struct {
    float s0;
    float s1;
    float s2;
    float s3;
    float s4;
    float s5;
    float s6;
    float s7;
    float s8;
    float s9;
    float s10;
    float s11;
    float s12;
    float s13;
    float s14;
    float s15;
    uint32_t fpscr;
    uint32_t reserved; // Maintain 8 byte alignment
} CpuFpuHardwareFrame;

#endif  // MPU_CONFIG_H
