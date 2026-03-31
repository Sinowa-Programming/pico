#ifndef VIRTUAL_FILE_H
#define VIRTUAL_FILE_H

#include "pico/stdlib.h"

// Define a 16MB window for the bitstream(s)
#define MAX_VIRTUAL_FILES   2
#define VIRTUAL_FILE_BASE   0x80000000
#define VIRTUAL_FILE_END    0x81000000
#define VIRTUAL_FILE_PAGE_SIZE 4096  // 4kB

/** This struct holds the reference data to a currently active file.
 * descriptor - The id of the file. It directly refers to the frame index the
 * file is stored in.
 * offset - The offset from the start of the file that is currently loaded.
 * The loaded chunk is [offset, offset + PAGE_SIZE], where PAGE_SIZE is defined in
 * internal_memory.h
 * size - IDK why I have this currently
 * flags - Is the file "r" read, "w" write, or something else? Prevents having to write back unmodified data.
 */
typedef struct {
    uint32_t file_hash;
    int descriptor;
    uint32_t offset;
    uint32_t size;
    char* flags;
} VirtualFile;


/// @brief The hash function for all files. Used to speed up all subsequent comparisons between file names.
/// @param file The file name to be hashed
/// @return The hash of the file name
inline static uint32_t hash(const char *file);


VirtualFile* __wrap__fopen(const char *file, int flags, ...);
int __wrap__fclose(void* ptr);
size_t __wrap__fwrite(const void* __restrict__ buffer, size_t size, size_t count, VirtualFile* __restrict__ stream);
size_t __wrap__fread( void * ptr, size_t size, size_t count, VirtualFile * stream );

#endif  // VIRTUAL_FILE_H