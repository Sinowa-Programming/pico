#ifndef INTERNAL_MEMORY_H
#define INTERNAL_MEMORY_H
#include "FreeRTOS.h"
#include "semphr.h"
#include <algorithm>
#include <cstring>

#include "memory.hpp"
#include "external_memory.h"    // For sending memory requests

class ExternalMemory;

class VMM {
    int16_t page_to_frame[NUM_PAGES];                       // Virtual Page ID -> physical sram frame idx
    uint32_t frame_to_page[MAX_PHYSICAL_FRAMES];            // Physical sram frame idx -> Virtual Page ID
    uint8_t lru_list[MAX_PHYSICAL_FRAMES];                  // Map of each age index to a frame
    uint8_t frame_ages[MAX_PHYSICAL_FRAMES];                // The ages of each frame
    int8_t num_occupied_frames = 0;

    BitArray is_resident;
    BitArray is_dirty;

    SemaphoreHandle_t vmmMutex;
    TaskHandle_t vmmTaskHandle = NULL;

    ExternalMemory *_external_memory;

    int8_t get_available_frame();

    // ==== Page Fault ====
    void clear_page(uint32_t page_id, bool block_until_cleared);
    // --------------------

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
    uint8_t sram_frames[MAX_PHYSICAL_FRAMES][PAGE_SIZE];    // The physical storage of the data( host side )

    VMM();
    void add_external_memory(ExternalMemory *external_memory) {
        _external_memory = external_memory;
    }
    void start();//const char* taskName = "VMM_Task", uint16_t stackSize = 2048, UBaseType_t priority = tskIDLE_PRIORITY + 1);

    // Communication with External Memory
    void notify_completion(MemoryRequest finished_req);

    // Interface functions for user code
    uint8_t& access(uint32_t virtual_addr, bool is_write);
    uint8_t* get_physical_ptr(uint32_t virtual_addr);
};

#endif  // INTERNAL_MEMORY_H