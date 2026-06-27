#include "internal_memory.h"
#include <cmath>
#include <cstring>
#include <pico/multicore.h>

#include "debug_led.h"
#include "pal.h"    // For vprintf

__attribute__((section(".vmm_frames"))) uint8_t VMM::sram_frames[MAX_PHYSICAL_FRAMES][PAGE_SIZE];

void VMM::report_mutex_status() {
    if (mutex_try_enter(&vmmMutex, NULL)) {
        ws2812_send_pixel(255, 165, 0); // Orange
        mutex_exit(&vmmMutex);
    } else {
        ws2812_send_pixel(255, 0, 0); // Red
    }

    sleep_ms(1000);
    ws2812_send_pixel(0, 0, 0);
}

void VMM::clear_page(uint32_t page_id)
{
    uint8_t frame_idx = page_to_frame[page_id];

    // Wipe mappings instantly
    memset(sram_frames[frame_idx], 0, PAGE_SIZE);
    is_resident.clear(page_id);
    is_dirty.clear(page_id);
    page_to_frame[page_id] = -1;
    frame_to_page[frame_idx] = 0xFFFFFFFF;

    move_to_back(frame_idx);
    --num_occupied_frames;
}

uint8_t VMM::get_available_frame(bool* is_page_dirty, uint32_t* page_to_write) {
    *is_page_dirty = false;
    uint32_t frame = -1;

    if (num_occupied_frames < MAX_PHYSICAL_FRAMES) {
        frame = num_occupied_frames;
        num_occupied_frames++;
        return frame;
    } 
    for (int i=0; i < MAX_PHYSICAL_FRAMES; ++i) {
        uint8_t frame_id = lru_list[i];
        uint32_t page_id = frame_to_page[frame_id];

        // Evict if the page is valid and NOT dirty.
        if (page_id != 0xFFFFFFFF && !is_dirty.get(page_id)) {
            frame = frame_id;
            break;
        }
    }

    if(frame == -1) {
        // RAM is full and all frames are dirty. Evict the LRU frame.
        frame = lru_list[MAX_PHYSICAL_FRAMES - 1];
        *page_to_write = frame_to_page[frame];
        *is_page_dirty = true; // Signal the caller to write the data

        is_resident.clear(*page_to_write);
        is_dirty.clear(*page_to_write);
        page_to_frame[*page_to_write] = -1;
        frame_to_page[frame] = 0xFFFFFFFF;

    } else {
        // It's a clean frame, just wipe the metadata safely
        clear_page(frame_to_page[frame]);
    }

    return frame;
}

void VMM::update_mpu_access(uint16_t frame_to_enable, MpuRegionSlot slot)
{
    // Select the appropriate MPU region
    uint16_t mpu_region = slot;

    if(mpu_enabled[slot] == frame_to_enable) {
        return;
    }

    // Enable access to the new frame
    uint32_t base_addr = (uint32_t)sram_frames[frame_to_enable];
    uint32_t limit_addr = (base_addr + PAGE_SIZE) - 1;

    pending_mpu_region_config.region = mpu_region;
    pending_mpu_region_config.base_addr = base_addr;
    pending_mpu_region_config.limit_addr = limit_addr;
    pending_mpu_region_config.access = true;
    pending_mpu_region_config.execute = (slot == MpuRegionSlot::SLOT_EXEC);
    pending_mpu_region_config.clear = false;

    pending_core_cmd.type = InterCoreCommandType::MpuSetRegion;

    mpu_ack_flag = false;
    __DMB();
    
    if (get_core_num() == 0) {
        multicore_fifo_push_blocking((uint32_t)&pending_core_cmd);

        // Wait for ACK from core1
        while (!mpu_ack_flag) {
            tight_loop_contents();
        }
    } else {
        set_addr(pending_mpu_region_config.region, pending_mpu_region_config.base_addr, pending_mpu_region_config.limit_addr, pending_mpu_region_config.access, pending_mpu_region_config.execute);
        __DSB();
        __ISB();
    }

    // Track which frame is now enabled for this region
    mpu_enabled[slot] = frame_to_enable;
}

void VMM::clear_region(MpuRegionSlot slot)
{
    uint16_t mpu_region = slot;

    // Disable access to the MPU region
    pending_mpu_region_config.region = mpu_region;
    pending_mpu_region_config.base_addr = 0;
    pending_mpu_region_config.limit_addr = 0;
    pending_mpu_region_config.access = false;
    pending_mpu_region_config.execute = false;
    pending_mpu_region_config.clear = true;

    pending_core_cmd.type = InterCoreCommandType::MpuClearRegion;

    mpu_ack_flag = false;
    __DMB();
    
    if (get_core_num() == 0) {
        multicore_fifo_push_blocking((uint32_t)&pending_core_cmd);

        // Wait for ACK from core1
        while (!mpu_ack_flag) {
            tight_loop_contents();
        }
    } else {
        mpu_clear_region(mpu_region);
        __DSB();
        __ISB();
    }

    // Mark the region as having no frame enabled
    mpu_enabled[slot] = 0xFFFF;
}

// Move the empty frame to the back and decrement the frame size, effectively deleting the frame from the LRU
void VMM::move_to_back(uint8_t frame_idx) {
    int pos = -1;
    for (int i = 0; i < num_occupied_frames; i++) {
        if (lru_list[i] == frame_idx) {
            pos = i;
            break;
        }
    }

    if (pos != -1) {
        // Shift everything after this frame one position left
        for (int i = pos; i < num_occupied_frames - 1; i++) {
            lru_list[i] = lru_list[i + 1];
        }
        // Place the freed frame at the boundary of occupied/unoccupied space
        lru_list[num_occupied_frames - 1] = frame_idx;
        num_occupied_frames--;
    }
}

// Moves a frame to the "Most Recently Used" (top) position
void VMM::update_lru_access(uint8_t frame_idx) {
    int pos = -1;
    for (int i = 0; i < MAX_PHYSICAL_FRAMES; i++) {
        if (lru_list[i] == frame_idx) {
            pos = i;
            break;
        }
    }
    // Shift everything down and put frame_idx at 0
    if (pos != -1) {
        for (int i = pos; i > 0; i--) {
            lru_list[i] = lru_list[i-1];
        }
        lru_list[0] = frame_idx;
        frame_ages[frame_idx] = 0; // Reset age on access
    }
}


void VMM::run()
{
    while (true) {
        // Wake up every 100ms to age the pages
        vTaskDelay(pdMS_TO_TICKS(100));

        // --- Aging Logic ---
        mutex_enter_blocking(&vmmMutex);
        for (int i = 0; i < num_occupied_frames; i++) {
            // Age the frame ages
            if (frame_ages[i] < 255 - 1) {
                frame_ages[i]++;
                continue;
            }
            // Proactive Booting:
            // If age == 255 and NOT dirty, we can clear it to make room
            // We are not going to clear dirty frames as it will be accessed again.
            // uint32_t p_id = frame_to_page[i];
            // if (p_id != 0xFFFFFFFF && !is_dirty.get(p_id)) {
            //     clear_page(p_id);
            // }
        }
        mutex_exit(&vmmMutex);
    }
}

VMM::VMM() {
    mutex_init(&vmmMutex);
    sem_init(&core1_wait_sem, 0, 1);

    for(int i = 0; i < NUM_PAGES; i++) page_to_frame[i] = -1;
    for(int i = 0; i < MAX_PHYSICAL_FRAMES; i++) {
        lru_list[i] = i;
        frame_ages[i] = 0;
        frame_to_page[i] = 0xFFFFFFFF;
    }

    mpu_enabled[0] = 0xFFFF;  // MAIN_MPU_REGION - no frame enabled
    mpu_enabled[1] = 0xFFFF;  // AUXILIARY_MPU_REGION - no frame enabled
}


void VMM::start() {
    if (vmmTaskHandle == NULL) {

        xTaskCreate(
            vmmTaskWrapper, // The static bridge
            "VMM_Task",
            512,    // 2kb
            this,           // Pass 'this' so the task knows which instance to use
            tskIDLE_PRIORITY + 1,
            &vmmTaskHandle
        );
    }
#if(configNUMBER_OF_CORES == 2)
    vTaskCoreAffinitySet(vmmTaskHandle, SYSTEM_CORE_AFFINITY);
#endif
}

void VMM::notify_completion(MemoryRequest *finished_req) {
    mutex_enter_blocking(&vmmMutex);

    // Update state: Page is now officially resident
    if (finished_req->op == MemoryOp::READ) {
        is_resident.set(finished_req->arg1);
        page_to_frame[finished_req->arg1] = finished_req->arg2;
        frame_to_page[finished_req->arg2] = finished_req->arg1;

        // Move to MRU (front of list)
        update_lru_access(finished_req->arg2);
    } else if (finished_req->op == MemoryOp::FREE) {
        // Mark the page and it's attached frame clear for future use if it is present
        uint32_t page_id = (finished_req->arg1 - VIRTUAL_MEMORY_BASE) / PAGE_SIZE;
        if(is_resident.get(page_id)) {
            uint32_t frame = page_to_frame[page_id];
            is_resident.clear(page_id);
            is_dirty.clear(page_id);
            page_to_frame[page_id] = -1;
            frame_to_page[frame] = 0xFFFFFFFF;
        }
    } else if (finished_req->op == MemoryOp::WRITE) {
        is_dirty.clear(finished_req->arg1);
    }

    if (finished_req->from_core1 && finished_req->op != MemoryOp::LOG && finished_req->op != MemoryOp::FREE) {
        sem_release(&core1_wait_sem);
    } else if (finished_req->task) {
        xTaskNotifyGive(finished_req->task);
    }

    mutex_exit(&vmmMutex);
}


void VMM::access(uint32_t virtual_addr, MpuRegionSlot slot) {
    // Normalize the address by subtracting the base
    uint32_t relative_addr = virtual_addr - VIRTUAL_MEMORY_BASE;
    uint32_t page_id = relative_addr / PAGE_SIZE;

    int16_t frame_idx;

    mutex_enter_blocking(&vmmMutex);
    // Check if resident. If not, trigger swap-in logic
    if (!is_resident.get(page_id)) {
        // Select an available physical frame and tell the external memory
        // to place the requested page into that frame's buffer.

        // Find a frame and check if it needs to be flushed
        bool does_page_need_to_write;
        uint32_t page_to_write;
        uint8_t frame = get_available_frame(&does_page_need_to_write, &page_to_write);

        if (does_page_need_to_write) {
            MemoryRequest write_req = {
                .op = MemoryOp::WRITE,
                .arg1 = page_to_write,
                .buffer = sram_frames[frame],
                .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
            };
            write_req.req = &write_req;

            mutex_exit(&vmmMutex);
            _external_memory->submit_request(write_req);
            // Suspend the task. When it is woken up, the page will be in local memory
            if (get_core_num() == 1) {
                sem_acquire_blocking(&core1_wait_sem);
            } else {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
            mutex_enter_blocking(&vmmMutex);
        }

        // Reserve mapping early so other tasks knows where the page will live
        page_to_frame[page_id] = frame;
        frame_to_page[frame] = page_id;
        mutex_exit(&vmmMutex);

        MemoryRequest read_req = {
            .op = MemoryOp::READ,
            .arg1 = page_id,    // The page id to read
            .arg2 = frame,      // The frame it will live in...Why is this here? TODO: Investigate whether arg2 is necessary
            .buffer = sram_frames[frame],
            .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
        };
        read_req.req = &read_req;
        _external_memory->submit_request(read_req);


        if (get_core_num() == 1) {
            sem_acquire_blocking(&core1_wait_sem);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        mutex_enter_blocking(&vmmMutex);
        frame_idx = read_req.arg2;
    } else {
        frame_idx = page_to_frame[page_id];
    }
    update_lru_access(frame_idx);

    // For now, I am assuming that all accessed data is dirty
    is_dirty.set(page_id);
    mutex_exit(&vmmMutex);

    // Enable the frame access
    update_mpu_access(frame_idx, slot);
}

void VMM::write_all_data()
{
    mutex_enter_blocking(&vmmMutex);
    for(int frame_num = 0; frame_num < MAX_PHYSICAL_FRAMES; ++frame_num) {
        uint32_t page_id = frame_to_page[frame_num];

        if(page_id == 0xFFFFFFFF || !is_dirty.get(page_id)) {
            continue;
        }

        MemoryRequest write_req = {
            .op = MemoryOp::WRITE,
            .arg1 = page_id,
            .buffer = sram_frames[frame_num],
            .task = NULL // Fire and forget
        };

        mutex_exit(&vmmMutex);
        _external_memory->submit_request(write_req);
        mutex_enter_blocking(&vmmMutex);
    }

    bool is_core1 = (get_core_num() == 1);
    MemoryRequest sync_req = {
        .op = MemoryOp::SYNC,
        .arg1 = 0,  // VMM
        .task = is_core1 ? NULL : xTaskGetCurrentTaskHandle()
    };

    mutex_exit(&vmmMutex);
    _external_memory->submit_request(sync_req);

    if (is_core1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

uintptr_t VMM::get_physical_ptr(uint32_t virtual_addr) {
    uint32_t page_id = (virtual_addr - VIRTUAL_MEMORY_BASE) / PAGE_SIZE;
    uint32_t offset = virtual_addr % PAGE_SIZE;

    // Assumes page is already resident (call access() first to ensure it)
    int16_t frame_idx = page_to_frame[page_id];
    
    // Return the address within the physical SRAM frame for the given virtual address
    return (uintptr_t)(sram_frames[frame_idx] + offset);
}

uintptr_t VMM::get_vaddr_from_frame(int16_t frame_id)
{
    return frame_to_page[frame_id] * PAGE_SIZE + VIRTUAL_MEMORY_BASE;
}

uint32_t VMM::get_frame_id_from_paddr(uint32_t paddr)
{
    // Calculate the offset from the start of the frame storage
    uintptr_t offset = paddr - (uintptr_t)&__vmm_frames_start;
    
    // Divide by PAGE_SIZE to get the frame ID
    return offset / PAGE_SIZE;
}

uintptr_t VMM::get_vaddr_from_paddr(uint32_t paddr)
{
    // Calculate the offset from the start of the frame storage
    uintptr_t offset = paddr - (uintptr_t)&__vmm_frames_start;
    
    // Get the frame ID and offset within the frame
    uint32_t frame_id = offset / PAGE_SIZE;
    uint32_t offset_in_frame = offset % PAGE_SIZE;
    
    // Get the virtual address of the frame and add the offset
    return get_vaddr_from_frame(frame_id) + offset_in_frame;
}

void *VMM::alloc(size_t mem_size)
{
    int is_core1 = (get_core_num() == 1);
    // Send the request.
    
    MemoryRequest req = {
        .op = MemoryOp::ALLOC,
        .arg2 = mem_size,   // arg2: requested memory size
        .task = is_core1 ? NULL : xTaskGetCurrentTaskHandle()    // active task
    };
    req.req = &req;
    _external_memory->submit_request(req);
    if (is_core1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    return (void*)(req.arg1);
}

void VMM::free(uint32_t virtual_addr)
{
    address_map.remove_by_original_address(virtual_addr);
    MemoryRequest req = {
        MemoryOp::FREE,
        .arg1 = virtual_addr,   // arg1: requested memory size
        .task = NULL    // Lazy free
    };

    _external_memory->submit_request(req);
}

void VMM::register_address_alias(uint32_t original_address, uint32_t adjusted_address)
{
    address_map.add_entry(original_address, adjusted_address);
}

uint32_t VMM::resolve_alias_to_virtual_base(uint32_t adjusted_address) {
    uint32_t idx = address_map.get_index_from_adjusted_address(adjusted_address);
    if(idx != 0xFFFFFFFF) {
        return address_map.get_original_address_from_index(idx);
    }
    return adjusted_address; // If no alias exists, return the address as-is
}

void VMM::remove_alias_to_virtual_base(uint32_t adjusted_address)
{
    address_map.remove_by_adjusted_address(adjusted_address);
}
