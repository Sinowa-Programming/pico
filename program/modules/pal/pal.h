#ifndef PAL_H
#define PAL_H

#include "virtual_file.h"
#include "internal_memory.h"
#include "external_memory.h"

#include <cstdarg>  // For varadic functions( the ... in vprintf )

// ====Global Variables====
extern ExternalMemory external_memory;
extern VMM vmm;    // This will need to be setup in main().
extern VFM vfm;
// ------------------------

#ifdef __cplusplus
extern "C" {
#endif

/* === Memory Functions === */
void _vmemset(void *ptr, int value, size_t count);
void _vmemcpy(void *dest, void *src, size_t count);
void *_vcalloc(size_t num, size_t size);
void *_vmalloc(size_t size);
void _vfree(void *ptr);

/* === File Functions === */
VirtualFile *_vfopen(const char * filename, const char * mode);
int _vfclose(VirtualFile * stream);
size_t _vfread(void * ptr, size_t size, size_t count, VirtualFile * stream);
size_t _vfwrite(const void * ptr, size_t size, size_t count, VirtualFile * stream);

/* === Other functions === */
static char formatted_text[256];  // Buffer to store the text for the log command
int _vprintf(const char * format, ...);
void _vsleep(uint32_t time);


#ifdef __cplusplus
}
#endif

#endif  // PAL_H