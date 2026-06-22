#ifndef VIRTUAL_FILE_H
#define VIRTUAL_FILE_H

#include "pico/stdlib.h"
#include <FreeRTOS.h>
#include <task.h>
#include <pico/mutex.h>
#include <pico/sem.h>
#include "memory.hpp"

// #include "external_memory.h"    // For sending memory requests
class ExternalMemory;

// #define KOMIHASH_NS_CUSTOM komihash
#include "komihash/komihash.h"


// Define a 16MB window for the bitstream(s)
#define MAX_VIRTUAL_FILES   163 // VirtualFile size: 25 bytes x 163 = 4075. Basically 4kb.
#define MAX_VIRTUAL_FILE_FRAMES 4
#define VIRTUAL_FILE_PAGE_SIZE 4096  // 4kB

/** This struct holds the reference data to a currently active file.
 * descriptor - The id of the file. It directly refers to the frame index the
 * file is stored in.
 * offset - The offset from the start of the file that is currently loaded.
 * The loaded chunk is [offset, offset + PAGE_SIZE], where PAGE_SIZE is defined in
 * internal_memory.h
 * size - IDK why I have this currently
 * flags - Is the file "r" read, "w" write, or something else? Prevents having to write back unmodified data.
 * local - Is the file loaded onto the machine.
 */
typedef struct {
    uint32_t file_hash; // 4 bytes
    int descriptor;     // 4 bytes
    uint32_t offset;    // 4 bytes
    int mode;           // 4 bytes
    // The remote/external file id returned by the host-side file-open command
    int32_t remote_id;  // 4 bytes
    bool local;         // 1 byte
    char *flags;        // 4 bytes
} VirtualFile;          // 25 Bytes


/// @brief The hash function for all files. Used to speed up all subsequent comparisons between file names.
/// @param file The file name to be hashed
/// @return The hash of the file name
inline static uint32_t hash(const char *file) { return komihash(file, strlen(file), 42); };

class VFM {
    static const int O_RDONLY  = 00000000;
    static const int O_WRONLY  = 00000001;
    static const int O_RDWR    = 00000002;
    static const int O_CREAT   = 00000100;
    static const int O_TRUNC   = 00001000;
    static const int O_APPEND  = 00002000;
    
    uint16_t file_lru[MAX_VIRTUAL_FILE_FRAMES];    // Map of each age index to a file frame
    uint8_t file_ages[MAX_VIRTUAL_FILE_FRAMES] = { 0 };    // The ages of each file frame
    int8_t num_occupied_files = 0;

    VirtualFile file_data[MAX_VIRTUAL_FILES] = { 0 };
    uint8_t file_frames[MAX_VIRTUAL_FILE_FRAMES][VIRTUAL_FILE_PAGE_SIZE];      // The physical frame location of a file

    mutex_t vfmMutex;
    semaphore_t core1_wait_sem;

    void clear_frame(uint8_t frame_idx);
    uint8_t get_available_frame(bool* is_frame_dirty, uint32_t* frame_to_write);  // Returns the first available frame's index( Will boot a page is needed. )

    // ==== LRU code ====
    void move_to_back(uint8_t frame_idx);
    void update_lru_access(uint8_t frame_idx); // Moves a frame to the "Most Recently Used" (top) position
    //---------------------

    ExternalMemory *_external_memory;

    public:
        VFM();
    
        // Communication with External Memory
        void notify_completion(MemoryRequest *finished_req);

        VirtualFile *fopen(const char *path, const char *mode_str);

        size_t fwrite(const void* __restrict__ buffer, size_t size, size_t count, VirtualFile* __restrict__ stream);
        size_t fread(void * ptr, size_t size, size_t count, VirtualFile * stream);
        int fclose(VirtualFile * stream);
};


// VirtualFile* _vfopen(const char *file, const char* mode);
// int _vfclose(void* ptr);
// size_t _vfwrite(const void* __restrict__ buffer, size_t size, size_t count, VirtualFile* __restrict__ stream);
// size_t _vfread( void * ptr, size_t size, size_t count, VirtualFile * stream );

#endif  // VIRTUAL_FILE_H