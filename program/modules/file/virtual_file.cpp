#include "virtual_file.h"

// This replaces the standard fopen
void* emulated_mmap_open(const char* filename) {
    if (g_vfile.is_open) return NULL; // Only supporting one file for simplicity

    MemoryRequest req = {
        .op = MemoryOp::FOPEN,
        .v_page_id = (uint32_t)filename, // Cast pointer to 32-bit integer
        .frame_index = 0,                 // Not used for FOPEN
        .sram_buffer = NULL,             // Not used for FOPEN
        .task = xTaskGetCurrentTaskHandle()
    };
    external_memory.submit_request(req);

    xTaskNotifyGive(NULL);  // Wait until the file has been loaded

    uint32_t file_size = req.frame_index;   // Get the file size
    if (file_size == 0) return NULL;

    g_vfile.start_addr = VIRTUAL_FILE_BASE;
    g_vfile.size = file_size;
    g_vfile.current_pos = 0;
    g_vfile.is_open = true;

    // Return the "pointer" to the start of the virtual range
    return (void*)g_vfile.start_addr;
}

void emulated_mmap_close(void* ptr) {
    if ((uint32_t)ptr == VIRTUAL_FILE_BASE) {
        MemoryRequest req = {
            .op = MemoryOp::FCLOSE,
            .v_page_id = 0, // Cast pointer to 32-bit integer
            .frame_index = 0,                 // Not used for FCLOSE
            .sram_buffer = NULL,             // Not used for FCLOSE
            .task = NULL    // No task is suspended when closing a file.
        };
        external_memory.submit_request(req);
        g_vfile.is_open = false;
    }
}

uint32_t file_mpu_fault(uint32_t fault_addr) {
    // Check if the fault is within our File Mapping Range
    if (fault_addr >= VIRTUAL_FILE_BASE && fault_addr < VIRTUAL_FILE_END) {

        // Calculate the offset into the file
        uint32_t file_offset = fault_addr - VIRTUAL_FILE_BASE;

        // Load the file at the offset into the frame
        vmm.file_access(file_offset);

        return vmm.get_file_frame();    // Return the pointer to the buffer.
    }
    return 0;   // Not a file.
}