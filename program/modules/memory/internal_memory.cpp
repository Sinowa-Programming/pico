#include "internal_memory.h"

int8_t VMM::get_available_frame() {
    if (num_occupied_frames < MAX_PHYSICAL_FRAMES) {
        // RAM is not full: Use the first empty slot
        uint8_t frame = lru_list[num_occupied_frames];
        num_occupied_frames++;
        return frame;
    } else {
        // RAM is full: Evict the last occupied slot (the LRU)
        return lru_list[MAX_PHYSICAL_FRAMES - 1];
    }
}

void VMM::clear_page(uint32_t page_id, bool block_until_cleared = false)
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
                clear_page(p_id);
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
}


void VMM::start(const char* taskName = "VMM_Task", uint16_t stackSize = 2048, UBaseType_t priority = tskIDLE_PRIORITY + 1) {
    if (vmmTaskHandle == NULL) {
        xTaskCreate(
            vmmTaskWrapper, // The static bridge
            taskName,
            stackSize,
            this,           // Pass 'this' so the task knows which instance to use
            priority,
            &vmmTaskHandle
        );
    }
}

void VMM::notify_completion(MemoryRequest finished_req) {
    xSemaphoreTake(vmmMutex, portMAX_DELAY);

    // Update state: Page is now officially resident
    if (finished_req.op == MemoryOp::READ) {
        is_resident.set(finished_req.v_page_id);
        page_to_frame[finished_req.v_page_id] = finished_req.frame_index;
        frame_to_page[finished_req.frame_index] = finished_req.v_page_id;

        // Move to MRU (front of list)
        update_lru_access(finished_req.frame_index);

        vTaskResume(finished_req.task);
    }
    else if (finished_req.op == MemoryOp::WRITE) {  // A returned write request means that the page has been marked clear
        clear_page(finished_req.v_page_id);
    }

    xSemaphoreGive(vmmMutex);
}

uint8_t& VMM::access(uint32_t virtual_addr, bool is_write) {
    uint32_t page_id = virtual_addr / PAGE_SIZE;
    uint32_t offset = virtual_addr % PAGE_SIZE;

    uint8_t frame_idx;

    xSemaphoreTake(vmmMutex, portMAX_DELAY);
    // 1. Check if resident. If not, trigger swap-in logic
    if (!is_resident.get(page_id)) {
        // Suspend the task and send the write request
        MemoryRequest req = {
            MemoryOp::READ,
            page_id,     // The virtual page being moved
            0,    // The physical SRAM frame used
            sram_frames[page_to_frame[page_id]],
            xTaskGetCurrentTaskHandle()
        };
        _external_memory->submit_request(req);
        // Suspend the task. When it is woken up, the page will be in local memory
        xSemaphoreGive(vmmMutex);
        vTaskSuspend(NULL);

        // The memory request has been fulfilled.
        frame_idx = req.frame_index;
        page_id = req.v_page_id;
        uint8_t& data = *(req.sram_buffer);

        return data;
    } else {
        xSemaphoreTake(vmmMutex, portMAX_DELAY);
        frame_idx = page_to_frame[page_id];
        update_lru_access(frame_idx);

        if (is_write) {
            is_dirty.set(page_id);
        }

        uint8_t& data = sram_frames[frame_idx][offset];
        xSemaphoreGive(vmmMutex);

        return data;
    }
}


uint8_t* VMM::get_physical_ptr(uint32_t virtual_addr) {
    uint32_t page_id = virtual_addr / PAGE_SIZE;
    uint32_t offset = virtual_addr % PAGE_SIZE;

    // Assumes page is already resident (call access() first to ensure it)
    int16_t frame_idx = page_to_frame[page_id];
    return &sram_frames[frame_idx][offset];
}