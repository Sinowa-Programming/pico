#include <sys/types.h>

// #define KOMIHASH_NS_CUSTOM komihash
#include "komihash/komihash.h"

#include "virtual_file.h"
#include "pal.h"
#include "memory.hpp"

// Return a pointer to the Virtual file on success.
VirtualFile* __wrap__fopen(const char *file, int flags, ...) {
    return vmm.file_open(file, flags);
}

int __wrap__fclose(void* ptr) {
    return vmm.file_close((VirtualFile *)ptr);
}

size_t __wrap__fwrite(const void* __restrict__ buffer, size_t size, size_t count, VirtualFile* __restrict__ stream) {
    return vmm.file_write(buffer, size, count, stream);
}

size_t __wrap__fread( void * ptr, size_t size, size_t count, VirtualFile * stream ) {
    return vmm.file_read(ptr, size, count, stream);
}

uint32_t file_mpu_fault(uint32_t fault_addr) {
    // Check if the fault is within our File Mapping Range
    if (fault_addr >= VIRTUAL_FILE_BASE && fault_addr < VIRTUAL_FILE_END) {

        // Calculate the offset into the file
        uint32_t file_offset = fault_addr - VIRTUAL_FILE_BASE;

        // Load the file at the offset into the frame
        vmm.file_access(file_offset);

        return (uint32_t)vmm.get_file_frame();    // Return the pointer to the buffer.
    }
    return 0;   // Not a file.
}

inline static uint32_t hash(const char *file)
{
    return komihash(file, strlen(file), 42);
}
