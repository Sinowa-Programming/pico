#include <sys/types.h>

#include "virtual_file.h"
#include "pal.h"
#include "memory.hpp"
#include <pico/multicore.h>

void VFM::clear_frame(uint8_t frame_idx)
{
    // Wipe mappings instantly
    memset(file_frames[frame_idx], 0, PAGE_SIZE);
    file_data[frame_idx].local = false;

    move_to_back(frame_idx);
    --num_occupied_files;
}

uint8_t VFM::get_available_frame(bool* is_frame_dirty, uint32_t* frame_to_write)
{
    mutex_enter_blocking(&vfmMutex);
    uint32_t frame = -1;

    if (num_occupied_files < MAX_VIRTUAL_FILE_FRAMES) {
        frame = num_occupied_files;
        num_occupied_files++;
        return frame;
    }
    for (int i=0; i < MAX_VIRTUAL_FILE_FRAMES; ++i) {
        uint8_t frame_id = file_lru[i];
        VirtualFile file = file_data[frame_id];

        // Evict if the page is valid and NOT dirty.
        if (file.remote_id != -1 && file.mode != O_RDONLY) {
            frame = frame_id;
            break;
        }
    }

    if(frame == -1) {
        // RAM is full and all frames are dirty. Evict the LRU frame.
        frame = file_lru[MAX_VIRTUAL_FILE_FRAMES - 1];

        VirtualFile *file = &file_data[frame];
        MemoryRequest write_req = {
            .op = MemoryOp::FWRITE,
            .arg3 = file->remote_id,
            .buffer = file_frames[frame],
            .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
        };
        write_req.req = &write_req;

        mutex_exit(&vfmMutex);
        _external_memory->submit_request(write_req);
        // Suspend the task. When it is woken up, the page will be in local memory
        if (get_core_num() == 1) {
            sem_acquire_blocking(&core1_wait_sem);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        mutex_enter_blocking(&vfmMutex);
    }

    // It's a clean frame, just wipe the metadata safely
    clear_frame(frame);
    mutex_exit(&vfmMutex);

    return frame;
}

void VFM::move_to_back(uint8_t frame_idx)
{
    int pos = -1;
    for (int i = 0; i < num_occupied_files; i++) {
        if (file_lru[i] == frame_idx) {
            pos = i;
            break;
        }
    }

    if (pos != -1) {
        // Shift everything after this frame one position left
        for (int i = pos; i < num_occupied_files - 1; i++) {
            file_lru[i] = file_lru[i + 1];
        }
        // Place the freed frame at the boundary of occupied/unoccupied space
        file_lru[num_occupied_files - 1] = frame_idx;
        --num_occupied_files;
    }
}

void VFM::update_lru_access(uint8_t frame_idx)
{
    int pos = -1;
    for (int i = 0; i < MAX_VIRTUAL_FILE_FRAMES; i++) {
        if (file_lru[i] == frame_idx) {
            pos = i;
            break;
        }
    }
    // Shift everything down and put frame_idx at 0
    if (pos != -1) {
        for (int i = pos; i > 0; i--) {
            file_lru[i] = file_lru[i-1];
        }
        file_lru[0] = frame_idx;
        file_ages[frame_idx] = 0; // Reset age on access
    }
}

VFM::VFM()
{
    mutex_init(&vfmMutex);
    sem_init(&core1_wait_sem, 0, 1);

    for(int i = 0; i < MAX_VIRTUAL_FILE_FRAMES; i++) {
        file_lru[i] = i;
    }
}

void VFM::write_all_data()
{
    mutex_enter_blocking(&vfmMutex);

    for (int frame_num = 0; frame_num < MAX_VIRTUAL_FILE_FRAMES; ++frame_num) {
        VirtualFile *file = &file_data[frame_num];

        if (file->remote_id == -1 || file->mode == O_RDONLY) {
            continue;
        }

        MemoryRequest write_req = {
            .op = MemoryOp::FWRITE,
            .arg3 = (uint32_t)file->remote_id,
            .buffer = file_frames[frame_num],
            .task = NULL // Fire and forget
        };
        write_req.req = &write_req;

        mutex_exit(&vfmMutex);
        _external_memory->submit_request(write_req);
        mutex_enter_blocking(&vfmMutex);
    }

    // Submit a sync request to wait for all queued writes to finish
    bool is_core1 = (get_core_num() == 1);
    MemoryRequest sync_req = {
        .op = MemoryOp::SYNC,
        .arg1 = 1,  // VFM
        .task = is_core1 ? NULL : xTaskGetCurrentTaskHandle()
    };
    sync_req.req = &sync_req;

    mutex_exit(&vfmMutex);
    _external_memory->submit_request(sync_req);

    if (is_core1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

void VFM::notify_completion(MemoryRequest *finished_req)
{
    // FWRITE doesn't need anything
    if (finished_req->from_core1) {
        sem_release(&core1_wait_sem);
    } else if (finished_req->task) {
        xTaskNotifyGive(finished_req->task);
    }
}

VirtualFile *VFM::fopen(const char *path, const char *mode_str)
{
    // String mode to int flags
    int mode = 0;
    if (strchr(mode_str, 'r')) {
        mode = strchr(mode_str, '+') ? O_RDWR : O_RDONLY;
    } else if (strchr(mode_str, 'w')) {
        mode = O_CREAT | O_TRUNC | (strchr(mode_str, '+') ? O_RDWR : O_WRONLY);
    } else if (strchr(mode_str, 'a')) {
        mode = O_CREAT | O_APPEND | (strchr(mode_str, '+') ? O_RDWR : O_WRONLY);
    }

    uint32_t file_hash = hash(path);

    // Check if already resident
    mutex_enter_blocking(&vfmMutex);
    for(int i = 0; i < MAX_VIRTUAL_FILES; i++) {
        if(file_data[i].local && file_data[i].file_hash == file_hash) {
            update_lru_access(i); // Move to Most Recently Used
            mutex_exit(&vfmMutex);
            return &file_data[i];
        }
    }
    mutex_exit(&vfmMutex);

    // Open the file
    MemoryRequest req = {
        .op = MemoryOp::FOPEN,
        .arg1 = (uint32_t)path, // Filename of file to open
        .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
    };
    req.req = &req;
    _external_memory->submit_request(req);

    if (get_core_num() == 1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    uint32_t file_size = req.arg2;
    if (file_size == 0 && mode == O_RDONLY) {
        return nullptr; 
    }

    bool is_frame_dirty;
    uint32_t frame_to_write;
    uint8_t frame_id = get_available_frame(&is_frame_dirty, &frame_to_write);

    // Local VirtualFile entry
    mutex_enter_blocking(&vfmMutex);
    file_data[frame_id].file_hash = file_hash;
    file_data[frame_id].descriptor = frame_id;
    file_data[frame_id].offset = 0;
    file_data[frame_id].mode = mode;
    file_data[frame_id].remote_id = req.arg3; // The remote file id
    file_data[frame_id].local = false;

    update_lru_access(frame_id);
    mutex_exit(&vfmMutex);

    return &file_data[frame_id];
}

size_t VFM::fwrite(const void* __restrict__ buffer, size_t size, size_t count, VirtualFile *__restrict__ stream)
{
    if(size == 0 || count == 0 || stream == nullptr) {
        return 0;
    }

    size_t total_bytes = size * count;
    size_t current_byte = 0;

    // Update LRU since this frame is actively being used
    mutex_enter_blocking(&vfmMutex);
    update_lru_access(stream->descriptor);
    mutex_exit(&vfmMutex);

    for(; current_byte < total_bytes; current_byte += VIRTUAL_FILE_PAGE_SIZE) {

        size_t chunk_size = MIN(total_bytes - current_byte, (size_t)VIRTUAL_FILE_PAGE_SIZE);

        // Copy the data from the user's buffer into the dedicated memory frame
        memcpy(file_frames[stream->descriptor], (uint8_t *)buffer + current_byte, chunk_size);

        MemoryRequest req = {
            .op = MemoryOp::FWRITE,
            .arg1 = stream->offset,                     // File offset to start writing from
            .arg2 = chunk_size,                         // Total bytes to write
            .arg3 = (uint32_t)stream->remote_id,        // File id on the server
            .buffer = file_frames[stream->descriptor],  // Frame to write
            .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
        };
        req.req = &req;
        
        _external_memory->submit_request(req);

        // Pause the task until the external memory transfer is complete
        if (get_core_num() == 1) {
            sem_acquire_blocking(&core1_wait_sem);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        stream->offset += chunk_size;
    }

    return count;
}

size_t VFM::fread(void * ptr, size_t size, size_t count, VirtualFile * stream)
{
    if(size == 0 || count == 0 || stream == nullptr) {
        return 0;
    }

    size_t total_bytes = size * count;
    size_t current_byte = 0;

    // Update LRU since this frame is actively being used
    mutex_enter_blocking(&vfmMutex);
    update_lru_access(stream->descriptor);
    mutex_exit(&vfmMutex);

    for(; current_byte < total_bytes; current_byte += VIRTUAL_FILE_PAGE_SIZE) {

        size_t chunk_size = MIN(total_bytes - current_byte, (size_t)VIRTUAL_FILE_PAGE_SIZE);

        MemoryRequest req = {
            .op = MemoryOp::FREAD,
            .arg1 = stream->offset,                     // File offset to read from
            .arg2 = chunk_size,                         // Total bytes to read
            .arg3 = (uint32_t)stream->remote_id,        // File id on the server
            .buffer = file_frames[stream->descriptor],  // Frame destination
            .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
        };
        req.req = &req;
        
        _external_memory->submit_request(req);

        // Pause the task until the frame is loaded completely
        if (get_core_num() == 1) {
            sem_acquire_blocking(&core1_wait_sem);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        // Copy the data from the file frame into the user's buffer
        memcpy((uint8_t *)ptr + current_byte, file_frames[stream->descriptor], chunk_size);

        stream->offset += chunk_size;
    }

    return count;
}

int VFM::fclose(VirtualFile * stream) 
{
    if (stream == nullptr || stream->remote_id == -1) {
        return -1;
    }

    // Tell the server/host that the device has closed the file
    MemoryRequest req = {
        .op = MemoryOp::FCLOSE,
        .arg3 = (uint32_t)stream->remote_id,            // File id on remote server
        .task = get_core_num() == 0 ? xTaskGetCurrentTaskHandle() : NULL
    };
    req.req = &req;
    
    _external_memory->submit_request(req);

    if (get_core_num() == 1) {
        sem_acquire_blocking(&core1_wait_sem);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // Clean up local resources for this file descriptor
    mutex_enter_blocking(&vfmMutex);
    stream->remote_id = -1;
    stream->local = false;
    clear_frame(stream->descriptor); // Frees the frame up in the LRU system
    mutex_exit(&vfmMutex);
    
    return 0;
}