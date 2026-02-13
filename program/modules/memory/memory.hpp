#ifndef MEMORY_H
#define MEMORY_H
#include "FreeRTOS.h"
#include "semphr.h"

#define SYSTEM_CORE_AFFINITY (1U << 0)

#define VIRTUAL_MEMORY_SIZE  (40 * 1024 * 1024)
#define PAGE_SIZE            (4096)
#define NUM_PAGES            (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)
#define MAX_PHYSICAL_FRAMES  (64)
#define BIT_ARRAY_SIZE       ((NUM_PAGES + 31) / 32)

// Constant for the base address assigned in the linker script
#define VIRTUAL_MEMORY_BASE 0x20082000


typedef enum {
    READ,
    WRITE
} MemoryOp;

struct MemoryRequest {
    MemoryOp op;
    uint32_t v_page_id;     // The virtual page being moved
    uint32_t frame_index;    // The physical SRAM frame used. If the op is read, then it is overwritten
    uint8_t* sram_buffer;   // Pointer to the page in memory
    TaskHandle_t task = nullptr;      // The task that owns the request
};

class BitArray {
    uint32_t storage[BIT_ARRAY_SIZE] = {0};
    public:
        inline void set(uint32_t i)   { storage[i >> 5] |= (1UL << (i & 31)); }
        inline void clear(uint32_t i) { storage[i >> 5] &= ~(1UL << (i & 31)); }
        inline bool get(uint32_t i)   { return storage[i >> 5] & (1UL << (i & 31)); }
};

#endif // MEMORY_H