#ifndef PAL_H
#define PAL_H

#include "internal_memory.h"
#include "external_memory.h"
// ====Global Variables====
extern ExternalMemory external_memory;
extern VMM vmm;    // This will need to be setup in main().
// ------------------------

// Custom memset for Virtual Addresses
void vmemset(uint32_t dest_v_addr, int value, size_t count);

// Custom memcpy for Virtual Addresses
void vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count);

// Custom calloc for Virtual Addresses
void *vcalloc(size_t num, size_t size);


void *vmalloc(size_t num, size_t size);
#endif  // PAL_H