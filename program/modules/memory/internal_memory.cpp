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

    pending_mpu_cmd.region = mpu_region;
    pending_mpu_cmd.base_addr = base_addr;
    pending_mpu_cmd.limit_addr = limit_addr;
    pending_mpu_cmd.access = true;
    pending_mpu_cmd.execute = (slot == MpuRegionSlot::SLOT_EXEC);
    pending_mpu_cmd.clear = false;

    mpu_ack_flag = false;
    __DMB();
    
    if (get_core_num() == 0) {
        multicore_fifo_push_blocking((uint32_t)&pending_mpu_cmd);

        // Wait for ACK from core1
        while (!mpu_ack_flag) {
            tight_loop_contents();
        }
    } else {
        set_addr(pending_mpu_cmd.region, pending_mpu_cmd.base_addr, pending_mpu_cmd.limit_addr, pending_mpu_cmd.access, pending_mpu_cmd.execute);
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
    pending_mpu_cmd.region = mpu_region;
    pending_mpu_cmd.base_addr = 0;
    pending_mpu_cmd.limit_addr = 0;
    pending_mpu_cmd.access = false;
    pending_mpu_cmd.execute = false;
    pending_mpu_cmd.clear = true;

    mpu_ack_flag = false;
    __DMB();
    
    if (get_core_num() == 0) {
        multicore_fifo_push_blocking((uint32_t)&pending_mpu_cmd);

        // Wait for ACK from core1
        while (!mpu_ack_flag) {
            tight_loop_contents();
        }
    } else {
        mpu_clear_region(mpu_region);
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

    file_lru_fifo = xQueueCreate(MAX_VIRTUAL_FILES, sizeof(uint16_t));
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
    }
    else if (finished_req->op == MemoryOp::FREE) {
        // Mark the page and it's attached frame clear for future use if it is present
        uint32_t page_id = (finished_req->arg1 - VIRTUAL_MEMORY_BASE) / PAGE_SIZE;
        if(is_resident.get(page_id)) {
            uint32_t frame = page_to_frame[page_id];
            is_resident.clear(page_id);
            is_dirty.clear(page_id);
            page_to_frame[page_id] = -1;
            frame_to_page[frame] = 0xFFFFFFFF;
        }
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

VirtualFile *VMM::file_open(const char *file, char *mode)
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
        .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
    };
    req.req = &req;
    _external_memory->submit_request(req);

    // Wait until the file has been loaded
    if (get_core_num() == 1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    uint32_t file_size = req.arg2;
    if (file_size == 0) return NULL;

    // File LRU
    uint16_t file_id;
    if(uxQueueSpacesAvailable(file_lru_fifo) == 0) {
        // Remove the old file from the loaded page
        xQueueReceive(file_lru_fifo, &file_id, 0);

        VirtualFile file_to_remove = file_data[file_id];

        // Flush the file data if it was open as write.
        if(file_to_remove.flags != "r") {
            MemoryRequest write_req = {
                .op = MemoryOp::FWRITE,
                .arg1 = file_to_remove.offset,      // File offset
                .arg2 = PAGE_SIZE,                  // Size of the buffer
                .arg3 = file_to_remove.remote_id,   // File id on the server
                .buffer = file_frames[file_id],     // Pointer to the buffer
                .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
            };
            req.req = &req;
            _external_memory->submit_request(write_req);

            // Wait until the file has been written
            if (get_core_num() == 1) {
                sem_acquire_blocking(&core1_wait_sem);
            } else {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
        }
    } else {
        file_id = uxQueueMessagesWaiting(file_lru_fifo);
    }

    xQueueSend(file_lru_fifo, &file_id, 0);

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
        memcpy(file_frames[stream->descriptor], (uint8_t *)buffer + current_byte, chunk_size);

        MemoryRequest req = {
            .op = MemoryOp::FWRITE,
            .arg1 = stream->offset,                     // File offset to start writing from
            .arg2 = chunk_size,                         // Total bytes to write
            .arg3 = stream->remote_id,                  // File id on the server
            .buffer = file_frames[stream->descriptor],  // Frame to write
        };
        _external_memory->submit_request(req);
    }

    memcpy(file_frames[stream->descriptor], (uint8_t *)buffer + current_byte, total_bytes - current_byte);
    MemoryRequest req = {
        .op = MemoryOp::FWRITE,
        .arg1 = stream->offset,                     // File offset to start writing from
        .arg2 = total_bytes - current_byte,         // Total bytes to write
        .arg3 = stream->remote_id,
        .buffer = file_frames[stream->descriptor],  // Frame to write
        .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
    };
    req.req = &req;
    _external_memory->submit_request(req);

    // Pause the task until all the writing is complete
    if (get_core_num() == 1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

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
            .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
        };
        req.req = &req;
        _external_memory->submit_request(req);

        // Pause the task until the next frame is loaded is complete
        if (get_core_num() == 1) {
            sem_acquire_blocking(&core1_wait_sem);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        // Copy the data from the file frame into the given buffer
        memcpy((uint8_t *)ptr + current_byte, file_frames[stream->descriptor], chunk_size);

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
        .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
    };
    req.req = &req;
    _external_memory->submit_request(req);
    if (get_core_num() == 1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    // TODO: FINISH THIS
    
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
            .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
        };
        req.req = &req;
        _external_memory->submit_request(req);
        if (get_core_num() == 1) {
            sem_acquire_blocking(&core1_wait_sem);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
    // TODO: FINISH THIS
}
