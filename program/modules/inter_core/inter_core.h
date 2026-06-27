#ifndef INTER_CORE_H
#define INTER_CORE_H

#include <cstdint>

enum class InterCoreCommandType : uint8_t {
    MpuSetRegion = 0,
    MpuClearRegion = 1,
    ClientStore = 2,
    ClientLoad = 3
};

// Sent to core 1 to config it's MPU
struct MpuRegionConfig {
    uint16_t region;
    uint32_t base_addr;
    uint32_t limit_addr;
    bool access;
    bool execute;
    bool clear;
};

struct CoreStoreCMD {
    bool pause;
};

struct InterCoreCommand {
    InterCoreCommandType type;
};

void core1_setup();
void core1_fifo_isr();
extern "C" void isr_memfault(void);

extern volatile MpuRegionConfig pending_mpu_region_config;
extern volatile CoreStoreCMD pending_store_cmd;
extern volatile InterCoreCommand pending_core_cmd;
extern volatile bool mpu_ack_flag;

#endif // INTER_CORE_H
