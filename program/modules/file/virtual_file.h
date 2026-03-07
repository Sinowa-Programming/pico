#ifndef VIRTUAL_FILE_H
#define VIRTUAL_FILE_H

#include "pico/stdlib.h"

// Define a 16MB window for the bitstream (adjust based on your needs)
#define VIRTUAL_FILE_BASE  0x80000000
#define VIRTUAL_FILE_END   0x81000000

typedef struct {
    uint32_t start_addr;
    uint32_t current_pos;
    uint32_t size;
    bool is_open;
} VirtualFile;

static VirtualFile g_vfile = {0};

#endif  // VIRTUAL_FILE_H