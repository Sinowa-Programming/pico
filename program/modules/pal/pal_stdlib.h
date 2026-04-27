// This file is meant to copy stdlib.h source code closely and
// functions are added based on what is required to compile the program
#ifndef PAL_STDLIB_H
#define PAL_STDLIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void vmemset(uint32_t dest_v_addr, int value, size_t count);
extern void vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count);
extern void *vcalloc(size_t num, size_t size);
extern void *vmalloc(size_t size);
extern void vfree(void *ptr);

#ifdef __cplusplus
}
#endif

// Map standard functions to the wrappers
#define malloc(size) vmalloc(size)
#define calloc(nmemb, size) vcalloc(nmemb, size)
#define free(ptr) vfree(ptr)
#define memcpy(dest, src, n) vmemcpy(dest, src, n)
#define printf(format, ...) vprintf(format, ##__VA_ARGS__)

#endif // PAL_STDLIB_H