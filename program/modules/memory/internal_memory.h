#ifndef INTERNAL_MEMORY_H
#define INTERNAL_MEMORY_H
#include "FreeRTOS.h"
#include "semphr.h"
#include <pico/util/queue.h>

#include "memory.hpp"
#include "external_memory.h"    // For sending memory requests
#include "virtual_file.h"
// #include "mpu_config.h"

extern inline void set_addr_exec(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access);
extern inline void set_addr_nexec(uint16_t region_number, uint32_t base_address, uint32_t limit_address, bool access);
class ExternalMemory;

class VMM {
    // File
    uint32_t file_page_frame_base;      // The file offset base that is loaded.
    uint8_t file_frame[PAGE_SIZE];  // The physical frame location of a file

    // Translation Tables
    int16_t page_to_frame[NUM_PAGES];                       // Virtual Page ID -> physical sram frame idx
    uint32_t frame_to_page[MAX_PHYSICAL_FRAMES];            // Physical sram frame idx -> Virtual Page ID

    // LRU
    uint8_t lru_list[MAX_PHYSICAL_FRAMES];                  // Map of each age index to a frame
    uint8_t frame_ages[MAX_PHYSICAL_FRAMES];                // The ages of each frame
    int8_t num_occupied_frames = 0;

    PageBitArray is_resident;
    PageBitArray is_dirty;

    SemaphoreHandle_t vmmMutex;
    TaskHandle_t vmmTaskHandle = NULL;

    ExternalMemory *_external_memory;

    void clear_page(uint32_t page_id, bool block_until_cleared);
    uint8_t get_available_frame();  // Returns the first available frame's index( Will boot a page is needed. )

    /* === MPU CODE === */
    /* Only MPU Regions 1-7 are used by the access code. Region 0 is for the current frame the program counter is in.*/
    queue_t mpu_region_frame_fifo;    // This is an awful strategy, but I don't want the awful overhead of counting accesses to each page. Each entry is a [region number, frame_idx]
    FrameBitArray mpu_enabled;   // Array for if the mpu has already enabled a specific page. Operates in the Frame space.
    void update_mpu_access(uint16_t frame_to_enable);
    /* =========== */

    // ==== LRU code ====
    void move_to_back(uint8_t frame_idx);
    void update_lru_access(uint8_t frame_idx); // Moves a frame to the "Most Recently Used" (top) position
    //---------------------

    // Ages the frames and preemptively boots them.
    void run();

    // Static trampoline function
    static void vmmTaskWrapper(void* pvParameters) {
        VMM* vmmInstance = static_cast<VMM*>(pvParameters);
        vmmInstance->run();
    }

public:
    __attribute__((aligned(4))) uint8_t sram_frames[MAX_PHYSICAL_FRAMES][PAGE_SIZE];    // The physical storage of the data( host side ). 4 byte aligned for arm instructions

    VMM();
    void add_external_memory(ExternalMemory *external_memory) {
        _external_memory = external_memory;
    }
    void start();

    // Communication with External Memory
    void notify_completion(MemoryRequest *finished_req);

    /* Interface functions for user code */

    /// @brief Loads the given address into memory and enables access to it. This is a blocking operation currently. The task will be suspended until it is finished.
    /// @param virtual_addr The virtual memory address of the access
    /// @param update_mpu Tells the MPU to enable access to the newly loaded frame using regions 1 - 7.
    /// @return The frame idx that contains the data.
    void access(uint32_t virtual_addr, bool update_mpu = true);

    uintptr_t get_physical_ptr(uint32_t virtual_addr);


    /// @brief Get the virtual address of the frame
    /// @param frame_id The frame to get the virtual address of
    /// @return virtual address of the frame
    uintptr_t get_vaddr_from_frame(uint32_t frame_id);

    /* === memory allocation functions === */

    /// @brief Allocates a page and returns the frame that it is in.
    /// @param mem_size The size of the memory block to be allocated
    /// @return Returns the virtual address to the start of the object
    void *alloc(size_t mem_size);
    /* ========================= */

    /* === File Access Functions === */
    uint8_t* get_file_frame() { return file_frame; };

    void file_access(uint32_t file_offset);

};

#endif  // INTERNAL_MEMORY_H