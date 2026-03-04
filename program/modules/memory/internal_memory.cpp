#include "internal_memory.h"

void VMM::clear_page(uint32_t page_id, bool block_until_cleared)
{
    // If the page is dirty, you can't remove it yet, write the page to external memory and handle it when it has been copied out
    uint8_t frame_idx = page_to_frame[page_id];

    if(is_dirty.get(page_id)) {
        MemoryRequest req = {
            MemoryOp::WRITE,
            page_id,     // The virtual page being moved
            frame_to_page[frame_idx],    // The physical SRAM frame used
            sram_frames[page_to_frame[page_id]]
        };
        _external_memory->submit_request(req);
        if(block_until_cleared) {
            vTaskSuspend(NULL);
        } else {
            return; // Just be a lazy clear( not used anymore so who cares? Just remove it when you have time.)
        }
    }

    memset(sram_frames[frame_idx], 0, PAGE_SIZE);
    is_resident.clear(page_id);
    is_dirty.clear(page_id);
    page_to_frame[page_id] = -1;
    frame_to_page[frame_idx] = 0xFFFFFFFF;

    move_to_back(frame_idx);
    --num_occupied_frames;
}

uint8_t VMM::get_available_frame() {
    uint32_t frame;
    if (num_occupied_frames < MAX_PHYSICAL_FRAMES) {
        // RAM is not full: Use the first empty slot
        frame = num_occupied_frames;
        num_occupied_frames++;
    } else {
        // RAM is full
        // Find a slot that isn't dirty so it can be immediately overwritten.
        frame = is_dirty.find_first_zero();
        if(frame == -1) {
            // Remove the last page and wait until it is removed
            clear_page(frame_to_page[MAX_PHYSICAL_FRAMES - 1], true);
            frame = MAX_PHYSICAL_FRAMES - 1;
        } else {
            clear_page(frame_to_page[frame], false);
        }
    }
    return frame;
}

void VMM::update_mpu_access(uint16_t frame_to_enable)
{
    // If the MPU has already enabled access don't do anything. This shouldn't get hit.
    if(!mpu_enabled.get(frame_to_enable)) {
        return;
    }

    // If the queue is full then there are no available MPU regions. We have to replace one of them.
    uint16_t region_frame[2];

    if(queue_is_full(&mpu_region_frame_fifo)) {
        queue_try_remove(&mpu_region_frame_fifo, region_frame);
    } else {
        // Add a new region to the list
        region_frame[0] = mpu_region_frame_fifo.element_count + 1;  // Starts at 1. This is because 0 is used for the program counter
    }

    // Replace the old region number's frame with the new frame
    region_frame[1] = frame_to_enable;
    queue_try_add(&mpu_region_frame_fifo, region_frame);
    mpu_enabled.set(frame_to_enable);   // Update the bit array

    // Enable access to the new frame
    uint32_t base_addr = frame_to_enable * PAGE_SIZE;
    set_addr_nexec(region_frame[0], base_addr, base_addr + PAGE_SIZE, true);
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


void VMM::run() {
    while (true) {
        // Wake up every 100ms to age the pages
        vTaskDelay(pdMS_TO_TICKS(100));

        // --- Aging Logic ---
        xSemaphoreTake(vmmMutex, portMAX_DELAY);
        for (int i = 0; i < num_occupied_frames; i++) {
            // Age the frame ages
            if (frame_ages[i] < 255 - 1) {
                frame_ages[i]++;
                continue;
            }
            // Proactive Booting:
            // If age == 255 and NOT dirty, we can clear it to make room
            // We are not going to clear dirty frames as it will be accessed again.
            uint32_t p_id = frame_to_page[i];
            if (p_id != 0xFFFFFFFF && !is_dirty.get(p_id)) {
                clear_page(p_id, false);
            }
        }
    }
    xSemaphoreGive(vmmMutex);
}


VMM::VMM() {
    vmmMutex = xSemaphoreCreateMutex();

    for(int i = 0; i < NUM_PAGES; i++) page_to_frame[i] = -1;
    for(int i = 0; i < MAX_PHYSICAL_FRAMES; i++) {
        lru_list[i] = i;
        frame_ages[i] = 0;
        frame_to_page[i] = 0xFFFFFFFF;
    }

    queue_init(&mpu_region_frame_fifo, sizeof(int[2]), 7);  // There are 8 regions, but 1 region is being used by the active program. The rest are being accessed.
}


void VMM::start() {
    if (vmmTaskHandle == NULL) {

        xTaskCreate(
            vmmTaskWrapper, // The static bridge
            "VMM_Task",
            4096,
            this,           // Pass 'this' so the task knows which instance to use
            tskIDLE_PRIORITY + 1,
            &vmmTaskHandle
        );
        vTaskCoreAffinitySet(vmmTaskHandle, SYSTEM_CORE_AFFINITY);
    }
}

void VMM::notify_completion(MemoryRequest *finished_req) {
    xSemaphoreTake(vmmMutex, portMAX_DELAY);

    // Update state: Page is now officially resident
    if (finished_req->op == MemoryOp::READ) {
        is_resident.set(finished_req->v_page_id);
        page_to_frame[finished_req->v_page_id] = finished_req->frame_index;
        frame_to_page[finished_req->frame_index] = finished_req->v_page_id;

        // Move to MRU (front of list)
        update_lru_access(finished_req->frame_index);

        vTaskResume(finished_req->task);
    }
    else if (finished_req->op == MemoryOp::WRITE) {  // A returned write request means that the page has been marked clear
        clear_page(finished_req->v_page_id, false);
    } else if (finished_req->op == MemoryOp::ALLOC) {
        // There should be space if it reaches this point
        xTaskNotifyGive(finished_req->task);
    }

    xSemaphoreGive(vmmMutex);
}


void VMM::access(uint32_t virtual_addr, bool update_mpu = true) {
    // Normalize the address by subtracting the base
    uint32_t relative_addr = virtual_addr - VIRTUAL_MEMORY_BASE;
    uint32_t page_id = relative_addr / PAGE_SIZE;

    int16_t frame_idx;

    xSemaphoreTake(vmmMutex, portMAX_DELAY);
    // Check if resident. If not, trigger swap-in logic
    if (!is_resident.get(page_id)) {
        // Suspend the task and send the write request
        MemoryRequest req = {
            MemoryOp::READ,
            page_id,     // The virtual page being loaded in
            0,    // No frame is being touched this time
            sram_frames[page_to_frame[page_id]],
            xTaskGetCurrentTaskHandle()
        };
        _external_memory->submit_request(req);

        // Suspend the task. When it is woken up, the page will be in local memory
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // The memory request has been fulfilled.
        frame_idx = req.frame_index;
    } else {
        frame_idx = page_to_frame[page_id];
    }
    update_lru_access(frame_idx);

    // For now, I am assuming that all accessed data is dirty
    is_dirty.set(page_id);
    xSemaphoreGive(vmmMutex);

    // Enable the frame access
    if(update_mpu) {
        update_mpu_access(frame_idx);
    }
}


uintptr_t VMM::get_physical_ptr(uint32_t virtual_addr) {
    uint32_t page_id = virtual_addr / PAGE_SIZE;
    uint32_t offset = virtual_addr % PAGE_SIZE;

    // Assumes page is already resident (call access() first to ensure it)
    int16_t frame_idx = page_to_frame[page_id];
    return sram_frames[frame_idx][offset];
}

inline uintptr_t VMM::get_vaddr_from_frame(uint32_t frame_id)
{
    return frame_to_page[frame_id] * PAGE_SIZE + VIRTUAL_MEMORY_BASE;
}

void *VMM::alloc(size_t mem_size)
{
    TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
    // Send the request.
    MemoryRequest req = {
        MemoryOp::ALLOC,
        0,
        mem_size,
        0,
        cur_task
    };
    _external_memory->submit_request(req);
    xTaskNotifyGive(cur_task);

    return (void*)(VIRTUAL_MEMORY_BASE + (req.v_page_id * PAGE_SIZE));  // The data will always be page aligned.
}