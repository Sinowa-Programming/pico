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
extern int vprintf(const char * format, ...);

#define API_TABLE_ADDRESS 0x2007F000

typedef struct {
    int (*printf)(const char *, ...);
    void (*memset)(uint32_t, int, size_t);
    void (*memcpy)(uint32_t, uint32_t, size_t);
    void *(*calloc)(size_t, size_t);
    void *(*malloc)(size_t);
    void (*free)(void *);
} FirmwareJMPTable;

#define FW_API ((FirmwareJMPTable*)API_TABLE_ADDRESS)
extern FirmwareJMPTable api_table;

#ifdef __cplusplus
}
#endif

// Map standard functions to the hardcoded jump table pointer
#define malloc(size)         FW_API->malloc(size)
#define calloc(nmemb, size)  FW_API->calloc(nmemb, size)
#define free(ptr)            FW_API->free(ptr)
#define memcpy(dest, src, n) FW_API->memcpy(dest, src, n)
#define memset(dest, val, n) FW_API->memset(dest, val, n)
#define printf(format, ...)  FW_API->printf(format, ##__VA_ARGS__)

#endif // PAL_STDLIB_H