#ifndef PAL_H
#define PAL_H

#include "internal_memory.h"
#include "external_memory.h"

#include <cstdarg>  // For varadic functions( the ... in vprintf )

// ====Global Variables====
extern ExternalMemory external_memory;
extern VMM vmm;    // This will need to be setup in main().
// ------------------------

#ifdef __cplusplus
extern "C" {
#endif

/* === Memory Functions === */
void _vmemset(uint32_t dest_v_addr, int value, size_t count);
void _vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count);
void *_vcalloc(size_t num, size_t size);
void *_vmalloc(size_t size);
void _vfree(void *ptr);

/* === Other functions === */
static char formatted_text[256];  // Buffer to store the text for the log command
int _vprintf(const char * format, ...);
void _vsleep(uint32_t time);


#ifdef __cplusplus
}
#endif

#endif  // PAL_H