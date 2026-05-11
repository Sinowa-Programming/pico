// This file is meant to copy stdlib.h source code closely and
// functions are added based on what is required to compile the program
#ifndef PAL_STDLIB_H
#define PAL_STDLIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void _vmemset(uint32_t dest_v_addr, int value, size_t count);
extern void _vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count);
extern void *_vcalloc(size_t num, size_t size);
extern void *_vmalloc(size_t size);
extern void _vfree(void *ptr);
extern int _vprintf(const char * format, ...);
extern void _vsleep(uint32_t time);

#define API_TABLE_ADDRESS 0x2007F000

typedef struct {
    void (*sleep)(uint32_t);
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
#ifdef IS_CLIENT_PROGRAM

    // Map standard functions to the hardcoded jump table pointer
    #define malloc(size)         FW_API->malloc(size)
    #define calloc(nmemb, size)  FW_API->calloc(nmemb, size)
    #define free(ptr)            FW_API->free(ptr)
    #define memcpy(dest, src, n) FW_API->memcpy((uint32_t)dest, (uint32_t)src, n)
    #define memset(dest, val, n) FW_API->memset((uint32_t)dest, val, n)
    #define printf(format, ...)  FW_API->printf(format, ##__VA_ARGS__)
    #define sleep(time)          FW_API->sleep(time)
#endif

#endif // PAL_STDLIB_H