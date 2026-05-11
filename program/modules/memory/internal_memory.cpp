#include "internal_memory.h"
#include <cmath>
#include <cstring>

#include "debug_led.h"

void VMM::clear_page(uint32_t page_id, bool block_until_cleared)
{
    // If the page is dirty, you can't remove it yet, write the page to external memory and handle it when it has been copied out
    uint8_t frame_idx = page_to_frame[page_id];

    if(is_dirty.get(page_id)) {
        MemoryRequest req = {
            .op = MemoryOp::WRITE,
            .arg1 = frame_to_page[frame_idx],               // The virtual page being moved
            .buffer = sram_frames[page_to_frame[page_id]]   // The sram frame
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

void VMM::update_mpu_access(uint16_t frame_to_enable, bool executable)
{
    // If the MPU has already enabled access, do nothing.
    if (mpu_enabled.get(frame_to_enable)) {
        return;
    }

    // If this frame is an executable, use MPU region 0 directly.
    if (executable) {
        uint32_t base_addr = (uint32_t)sram_frames[frame_to_enable];
        set_addr_nexec(0, base_addr, base_addr + PAGE_SIZE, true);
        mpu_enabled.set(frame_to_enable);
        return;
    }

    // If the queue is full then there are no available MPU regions. We have to replace one of them.
    uint16_t region_frame[2];

    if(queue_is_full(&mpu_region_frame_fifo)) {
        queue_try_remove(&mpu_region_frame_fifo, region_frame);
        mpu_enabled.clear(region_frame[1]);
    } else {
        // Add a new region to the list
        region_frame[0] = mpu_region_frame_fifo.element_count + 1;  // Starts at 1. This is because 0 is used for the program counter
    }

    // Replace the old region number's frame with the new frame
    region_frame[1] = frame_to_enable;
    queue_try_add(&mpu_region_frame_fifo, region_frame);
    mpu_enabled.set(frame_to_enable);   // Update the bit array

    // Enable access to the new frame
    uint32_t base_addr = get_physical_ptr(frame_to_enable);
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


void VMM::run()
{
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
        xSemaphoreGive(vmmMutex);
    }
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

    queue_init(&file_lru_fifo, sizeof(uint16_t), MAX_VIRTUAL_FILES);
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
        is_resident.set(finished_req->arg1);
        page_to_frame[finished_req->arg1] = finished_req->arg2;
        frame_to_page[finished_req->arg2] = finished_req->arg1;

        // Move to MRU (front of list)
        update_lru_access(finished_req->arg2);
    }
    else if (finished_req->op == MemoryOp::WRITE) {  // A returned write request means that the page has been marked clear
        clear_page(finished_req->arg1, false);
    } else if (finished_req->op == MemoryOp::ALLOC) {
        // There should be space if it reaches this point
    }

    if(finished_req->task) {
        xTaskNotifyGive(finished_req->task);
    }

    xSemaphoreGive(vmmMutex);
}


void VMM::access(uint32_t virtual_addr, bool update_mpu) {
    // Normalize the address by subtracting the base
    uint32_t relative_addr = virtual_addr - VIRTUAL_MEMORY_BASE;
    uint32_t page_id = relative_addr / PAGE_SIZE;

    int16_t frame_idx;

    xSemaphoreTake(vmmMutex, portMAX_DELAY);
    // Check if resident. If not, trigger swap-in logic
    if (!is_resident.get(page_id)) {
        // Select an available physical frame and tell the external memory
        // to place the requested page into that frame's buffer.
        uint8_t frame = get_available_frame();

        // Reserve mapping early so other code knows where the page will live
        page_to_frame[page_id] = frame;
        frame_to_page[frame] = page_id;

        MemoryRequest req = {
            .op = MemoryOp::READ,
            .arg1 = page_id,     // The virtual page being loaded in
            .arg2 = frame,       // Provide the target physical frame index
            .buffer = sram_frames[frame],
            .task = xTaskGetCurrentTaskHandle()
        };
        req.req = &req;
        _external_memory->submit_request(req);

        // Suspend the task. When it is woken up, the page will be in local memory
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // The memory request has been fulfilled.
        frame_idx = req.arg2;
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
    } else {
        update_mpu_access(frame_idx, true);
    }
}


uintptr_t VMM::get_physical_ptr(uint32_t virtual_addr) {
    uint32_t page_id = virtual_addr / PAGE_SIZE;
    uint32_t offset = virtual_addr % PAGE_SIZE;

    // Assumes page is already resident (call access() first to ensure it)
    int16_t frame_idx = page_to_frame[page_id];
    // Return the address within the physical SRAM frame for the given virtual address
    return (uintptr_t)(sram_frames[frame_idx] + offset);
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
        .arg2 = mem_size,   // arg2: requested memory size
        .task = cur_task    // active task
    };
    req.req = &req;
    _external_memory->submit_request(req);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    return (void*)(VIRTUAL_MEMORY_BASE + (req.arg1 * PAGE_SIZE));  // The data will always be page aligned.
}

void VMM::free(uint32_t virtual_addr)
{
    MemoryRequest req = {
        MemoryOp::FREE,
        .arg1 = virtual_addr,   // arg1: requested memory size
        .task = NULL    // Lazy free
    };

    _external_memory->submit_request(req);
}

VirtualFile* VMM::file_open(const char *file, char* mode)
{
    uint32_t file_hash = hash(file);

    // Check if the file is already loaded
    for(VirtualFile &loaded_file : file_data) {
        if(loaded_file.file_hash == file_hash) {
            return &loaded_file;
        }
    }

    // Load the file externally
    MemoryRequest req = {
        .op = MemoryOp::FOPEN,
        .arg1 = (uint32_t)file, // Filename of file to open
        .task = xTaskGetCurrentTaskHandle()
    };
    req.req = &req;
    _external_memory->submit_request(req);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait until the file has been loaded

    uint32_t file_size = req.arg2;
    if (file_size == 0) return NULL;

    // File LRU
    uint16_t file_id;
    if(queue_is_full(&file_lru_fifo)) {
        // Remove the old file from the loaded page
        queue_try_remove(&file_lru_fifo, &file_id);

        VirtualFile file_to_remove = file_data[file_id];

        // Flush the file data if it was open as write.
        if(file_to_remove.flags != "r") {
            MemoryRequest write_req = {
                .op = MemoryOp::FWRITE,
                .arg1 = file_to_remove.offset,      // File offset
                .arg2 = PAGE_SIZE,                  // Size of the buffer
                .arg3 = file_to_remove.remote_id,   // File id on the server
                .buffer = file_frames[file_id],     // Pointer to the buffer
                .task = xTaskGetCurrentTaskHandle()
            };
            req.req = &req;
            _external_memory->submit_request(write_req);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait until the file has been written
        }
    } else {
        file_id = file_lru_fifo.element_count;
    }

    queue_try_add(&file_lru_fifo, &file_id);

    // Initialize the local VirtualFile entry
    file_data[file_id].file_hash = file_hash;
    file_data[file_id].descriptor = file_id;
    file_data[file_id].offset = 0;
    file_data[file_id].flags = mode;
    file_data[file_id].remote_id = req.arg3;

    return &(file_data[file_id]);
}

size_t VMM::file_write(const void* __restrict__ buffer, size_t size, size_t count, VirtualFile *__restrict__ stream)
{
    if(size == 0 || count == 0) {
        return 0;
    }

    size_t total_bytes = size * count;

    size_t current_byte=0;
    for(current_byte; current_byte < total_bytes - VIRTUAL_FILE_PAGE_SIZE; current_byte += VIRTUAL_FILE_PAGE_SIZE) {

        size_t chunk_size = MIN(total_bytes - current_byte, VIRTUAL_FILE_PAGE_SIZE);

        // Copy the data from the user's buffer into the dedicated memory page
        memcpy(file_frames[stream->descriptor], buffer + current_byte, chunk_size);

        MemoryRequest req = {
            .op = MemoryOp::FWRITE,
            .arg1 = stream->offset,                     // File offset to start writing from
            .arg2 = chunk_size,                         // Total bytes to write
            .arg3 = stream->remote_id,                  // File id on the server
            .buffer = file_frames[stream->descriptor],  // Frame to write
        };
        _external_memory->submit_request(req);
    }

    memcpy(file_frames[stream->descriptor], buffer + current_byte, total_bytes - current_byte);
    MemoryRequest req = {
        .op = MemoryOp::FWRITE,
        .arg1 = stream->offset,                     // File offset to start writing from
        .arg2 = total_bytes - current_byte,         // Total bytes to write
        .arg3 = stream->remote_id,
        .buffer = file_frames[stream->descriptor],  // Frame to write
        .task = xTaskGetCurrentTaskHandle()
    };
    req.req = &req;
    _external_memory->submit_request(req);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Pause the task until all the writing is complete

    stream->offset += total_bytes;

    return count;
}

size_t VMM::file_read(void *ptr, size_t size, size_t count, VirtualFile *stream)
{
    if(size == 0 || count == 0) {
        return 0;
    }

    size_t total_bytes = size * count;

    size_t current_byte=0;
    for(current_byte; current_byte < total_bytes; current_byte += VIRTUAL_FILE_PAGE_SIZE) {

        size_t chunk_size = MIN(total_bytes - current_byte, VIRTUAL_FILE_PAGE_SIZE);

        MemoryRequest req = {
            .op = MemoryOp::FREAD,
            .arg1 = stream->offset,                     // File offset to start writing from
            .arg2 = chunk_size,                         // Total bytes to write
            .arg3 = stream->remote_id,                  // The file to read
            .buffer = file_frames[stream->descriptor],  // Frame to write
            .task = xTaskGetCurrentTaskHandle()
        };
        req.req = &req;
        _external_memory->submit_request(req);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Pause the task until the next frame is loaded is complete

        // Copy the data from the file frame into the given buffer
        memcpy(ptr + current_byte, file_frames[stream->descriptor], chunk_size);

        stream->offset += chunk_size;
    }

    return count;
}


int VMM::file_close(VirtualFile *stream) {
    if (stream == nullptr) {
        return -1;
    }

    // Write dirty frame if the page is in a write mode
    if (strchr(stream->flags, 'w') || strchr(stream->flags, 'a') || strchr(stream->flags, '+')) {
        MemoryRequest req = {
            .op = MemoryOp::FWRITE,
            .arg1 = stream->offset,                     // File offset to write to
            .arg2 = VIRTUAL_FILE_PAGE_SIZE,             // Total bytes to write
            .arg3 = stream->remote_id,                  // File id on remote server
            .buffer = file_frames[stream->descriptor],  // Frame to write
            .task = NULL                                // The second memory request (FCLOSE) will be where the task is woken up.
        };
        _external_memory->submit_request(req);
    }

    // Tell the server that the device has closed the file
    MemoryRequest req = {
        .op = MemoryOp::FCLOSE,
        .arg3 = stream->remote_id,                  // File id on remote server
        .task = xTaskGetCurrentTaskHandle()
    };
    req.req = &req;
    _external_memory->submit_request(req);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Remove access to the old file frame
    uint16_t region_frame[2];

    mpu_enabled.clear(region_frame[1]);

    queue_try_remove(&mpu_region_frame_fifo, region_frame);

    set_addr_nexec(region_frame[0], (uint32_t)file_frames[stream->descriptor], (uint32_t)file_frames[stream->descriptor] + VIRTUAL_FILE_PAGE_SIZE, false);

    return 0;   // 100% Success Rate :)
}

void VMM::file_access(VirtualFile &file_id, uint32_t file_offset)
{
    // Check if the file is resident.
    uint8_t* file_frame = file_frames[file_id.descriptor];

    // If the address is in bounds then it is already loaded
    if(!(file_id.offset <= file_offset && file_offset <= file_id.offset + PAGE_SIZE)) {
        return;
    } else {
        MemoryRequest req = {
            .op = MemoryOp::FREAD,
            .arg1 = file_offset,
            .arg2 = PAGE_SIZE,
            .arg3 = file_id.remote_id,
            .buffer = file_frame,
            .task = xTaskGetCurrentTaskHandle()
        };
        req.req = &req;
        _external_memory->submit_request(req);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // If the queue is full then there are no available MPU regions. We have to replace one of them.
    uint16_t region_frame[2];

    if(queue_is_full(&mpu_region_frame_fifo)) {
        queue_try_remove(&mpu_region_frame_fifo, region_frame);
    } else {
        // Add a new region to the list
        region_frame[0] = mpu_region_frame_fifo.element_count + 1;  // Starts at 1. This is because 0 is used for the program counter
    }

    mpu_enabled.clear(region_frame[1]);

    // Replace the old region number's frame with the new frame
    queue_try_add(&mpu_region_frame_fifo, region_frame);

    // Enable access to the file
    set_addr_nexec(region_frame[0], (uint32_t)file_frame, (uint32_t)file_frame + VIRTUAL_FILE_PAGE_SIZE, true);
}
