#ifndef PAL_H
#define PAL_H

#include "internal_memory.h"
#include "external_memory.h"
// ====Global Variables====
extern ExternalMemory external_memory;
extern VMM vmm;    // This will need to be setup in main().
// ------------------------

/* === Memory Functions === */
void vmemset(uint32_t dest_v_addr, int value, size_t count);
void vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count);
void *vcalloc(size_t num, size_t size);
void *vmalloc(size_t size);
void vfree(void *ptr);

/* === Other functions === */
int vprintf(const char * format, ...);

#endif  // PAL_H