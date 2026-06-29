#ifndef INTERNAL_MEMORY_H
#define INTERNAL_MEMORY_H
#include "FreeRTOS.h"
#include "semphr.h"
#include <pico/util/queue.h>
#include <pico/mutex.h>
#include <pico/sem.h>

#include "inter_core.h"
#include "memory.hpp"
#include "external_memory.h"    // For sending memory requests
#include "virtual_file.h"
#include "mpu_config.h"
#include "static_address_map.h"

extern uint32_t __vmm_frames_start;

class ExternalMemory;

class VMM {
public:
    enum MpuRegionSlot {
        SLOT_EXEC     = 0,
        SLOT_DATA     = 1,
        SLOT_AUX_DATA = 2
    };

    static const uint16_t ADJUSTED_ADDRESS_LIMIT = 20;
private:

    void report_mutex_status();

    // Translation Tables
    int16_t page_to_frame[NUM_PAGES];                       // Virtual Page ID -> physical sram frame idx
    uint32_t frame_to_page[MAX_PHYSICAL_FRAMES];            // Physical sram frame idx -> Virtual Page ID

    // LRU
    uint8_t lru_list[MAX_PHYSICAL_FRAMES];                  // Map of each age index to a frame
    uint8_t frame_ages[MAX_PHYSICAL_FRAMES];                // The ages of each frame
    int8_t num_occupied_frames = 0;

    PageBitArray is_resident;
    PageBitArray is_dirty;

    mutex_t vmmMutex;
    semaphore_t core1_wait_sem;

    TaskHandle_t vmmTaskHandle = NULL;

    ExternalMemory *_external_memory;

    void clear_page(uint32_t page_id);
    uint8_t get_available_frame(bool* is_page_dirty, uint32_t* page_to_write);  // Returns the first available frame's index( Will boot a page is needed. )

    StaticAddressMap<ADJUSTED_ADDRESS_LIMIT> address_map;

    /* === MPU CODE === */
    /* MPU Regions used:
     * Region 0: Region used for instruction execution
     * Region 1: Main region for normal memory access
     * Region 2:  Auxiliary region for dual-access scenarios (e.g., memcpy)
    */
    uint16_t mpu_enabled[3];    // Track which frame is currently enabled for each region (0xFFFF if none)

    /// @brief Updates the rp2350 MPU to enable access to the specified frame.
    /// @param frame_to_enable The frame that access (including execution) will be granted to.
    /// @param use_auxiliary_region If true, uses AUXILIARY_MPU_REGION; otherwise uses MAIN_MPU_REGION
    void update_mpu_access(uint16_t frame_to_enable, MpuRegionSlot slot);
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
    // Aligned in the linker script
    static uint8_t sram_frames[MAX_PHYSICAL_FRAMES][PAGE_SIZE];    // The physical storage of the data( host side ). 32 bit aligned by the linker for the MPU

    VMM();
    void add_external_memory(ExternalMemory *external_memory) {
        _external_memory = external_memory;
    }
    void start();

    // Communication with External Memory
    void notify_completion(MemoryRequest *finished_req);

    /* Interface functions for user code */

    uint16_t *get_mpu_enabled() { return mpu_enabled; };
    void *set_mpu_enabled(volatile uint16_t *new_mpu_enabled);
    StaticAddressMap<ADJUSTED_ADDRESS_LIMIT> *get_address_map() { return &address_map; };

    /// @brief Loads the given address into memory and enables access to it. This is a blocking operation currently. The task will be suspended until it is finished.
    /// @param virtual_addr The virtual memory address of the access
    /// @param slot The mpu region that will be used to cover it. Regions 1 and 2 do not have execution permission.
    ///                             This is useful in cases like memcpy, where you need access to both memory regions at once.
    ///                             You have to clear the auxiliary region manually when done.
    void access(uint32_t virtual_addr, MpuRegionSlot slot = MpuRegionSlot::SLOT_DATA);

    /// @brief Writes all dirty data.
    void write_all_data();

    /// @brief Clears the given MPU region access.
    void clear_region(MpuRegionSlot slot);

    uintptr_t get_physical_ptr(uint32_t virtual_addr);


    /// @brief Get the virtual address of the frame
    /// @param frame_id The frame to get the virtual address of
    /// @return virtual address of the frame
    uintptr_t get_vaddr_from_frame(int16_t frame_id);

    /// @brief Get the frame ID given a physical address
    /// @param paddr The physical address pointing into the frame storage
    /// @return The frame ID that contains this physical address
    uint32_t get_frame_id_from_paddr(uint32_t paddr);

    /// @brief Get the virtual address from a physical pointer
    /// @param paddr The physical address to convert
    /// @return The corresponding virtual address
    uintptr_t get_vaddr_from_paddr(uint32_t paddr);

    /* === memory allocation functions === */

    /// @brief Allocates a page and returns the frame that it is in.
    /// @param mem_size The size of the memory block to be allocated
    /// @return Returns the virtual address to the start of the object
    void *alloc(size_t mem_size);

    void free(uint32_t virtual_addr);

    /// @brief Stores the mapping of the original address and adjusted address as the address hot-patching
    ///     adjusts the base address, which is the address need for free, when the offset access gets larger
    ///     than a page.
    /// @param original_address The original address. The one VMM::alloc provided.
    /// @param adjusted_address The edited address to map from.
    void register_address_alias(uint32_t original_address, uint32_t adjusted_address);
    
    /// @brief Get the original address given the shifted addresses
    /// @param adjusted_address The shifted address
    /// @return The original, unshifted address. If there is no mapping, it just returns the same address back
    uint32_t resolve_alias_to_virtual_base(uint32_t adjusted_address);

    /// @brief Removes the mapping made in resolve_alias_to_virtual_base
    /// @param adjusted_address The shifted address mapping to remove
    void remove_alias_to_virtual_base(uint32_t adjusted_address);
    /* ========================= */
};

#endif  // INTERNAL_MEMORY_H