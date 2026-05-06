#ifndef MEMORY_H
#define MEMORY_H
#include "FreeRTOS.h"
#include "semphr.h"

#define SYSTEM_CORE_AFFINITY (1U << 0)

#define VIRTUAL_MEMORY_SIZE  (40 * 1024 * 1024) // 40MB
#define PAGE_SIZE            (4096)
#define NUM_PAGES            (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)
#define MAX_PHYSICAL_FRAMES  (64)

// Constant for the base address assigned in the linker script
#define VIRTUAL_MEMORY_BASE 0x20082000

// Internal defines
// The amount of bits in the bit array that is resident in the device( They will change the most. ) Uhh...I need to clean this up.
const uint32_t MAX_RESIDENT_BITS = MAX_PHYSICAL_FRAMES / 32;

typedef enum {
    READ,       // The device wants to pull data from the host
    WRITE,      // The device wants to write data to the host
    ALLOC,      // The device needs the host to provide it with an empty page
    FREE,       // The device wants the host to label a sections of memory as unallocated.

    LOG,        // Send printf string to the host

    // For files specifically. These will affect the file buffer.
    FREAD,
    FWRITE,
    FOPEN,
    FCLOSE
} MemoryOp;

struct MemoryRequest {
    MemoryOp op;
    /*
    READ | WRITE: The virtual page being operated on.
    ALLOC: Returns the newly provided page id.
    FOPEN: Pointer to the file name
    FREAD | FWRITE: The file offset
    FREE: The virtual address to free
    */
    uint32_t arg1; // Generic argument 1 (used for virtual page id, file offset, or filename pointer)

    /*
    READ: It is overwritten
    ALLOC: Memory request size
    FOPEN: Returned file size
    FREAD | FWRITE: The size of the area to load/save.
    */
    uint32_t arg2; // Generic argument 2 (used for returned frame index, file size, or remote file id)

    /*
    FOPEN: Returns Remote File ID
    FCLOSE: The Remote File ID to close.
    FREAD: Remote File ID
    FWRITE: Remote File ID
    */
    uint32_t arg3;

    // If the operation is writing to or reading from the buffer then this is used.
    // Currently used by: READ | WRITE | FWRITE | FREAD
    // Pointer to the page in memory
    uint8_t* buffer;
    TaskHandle_t task;      // The task that owns the request
    MemoryRequest *req;     // Pointer to the address
};

template <size_t N>
class BitArray {
    protected:
        static constexpr size_t WordCount = (N + 31) / 32;
        uint32_t storage[WordCount] = {0};

    public:
        inline void set(uint32_t i)   { storage[i >> 5] |= (1UL << (i & 31)); }
        inline void clear(uint32_t i) { storage[i >> 5] &= ~(1UL << (i & 31)); }
        inline bool get(uint32_t i)   { return storage[i >> 5] & (1UL << (i & 31)); }
        int32_t find_first_zero() const {
            for (uint32_t i = 0; i < MAX_RESIDENT_BITS; i++) {
                uint32_t inv = ~storage[i]; // Invert to look for the lowest 1

                if (inv != 0) {
                    // __builtin_ctz counts trailing zeros.
                    // On M33, the compiler maps this to RBIT + CLZ.
                    return (i * 32) + __builtin_ctz(inv);
                }
            }
            return -1;
        }
};

using PageBitArray  = BitArray<NUM_PAGES>;
using FrameBitArray = BitArray<MAX_PHYSICAL_FRAMES>;

#endif // MEMORY_H