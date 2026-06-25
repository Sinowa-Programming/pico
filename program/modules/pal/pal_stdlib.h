// This file is meant to copy stdlib.h source code closely and
// functions are added based on what is required to compile the program
#ifndef PAL_STDLIB_H
#define PAL_STDLIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef VIRTUAL_FILE_STRUCT_DEFINED
#define VIRTUAL_FILE_STRUCT_DEFINED
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
#endif

extern void _vmemset(void *ptr, int value, size_t count);
extern void _vmemcpy(void *dest, void *src, size_t count);
extern void *_vcalloc(size_t num, size_t size);
extern void *_vmalloc(size_t size);
extern void _vfree(void *ptr);
extern int _vprintf(const char * format, ...);
extern void _vsleep(uint32_t time);
extern VirtualFile *_vfopen(const char * filename, const char * mode);
extern int _vfclose(VirtualFile * stream);
extern size_t _vfread(void * ptr, size_t size, size_t count, VirtualFile * stream);
extern size_t _vfwrite(const void * ptr, size_t size, size_t count, VirtualFile * stream);

#define API_TABLE_ADDRESS 0x2007F000

typedef struct {
    void (*sleep)(uint32_t);
    int (*printf)(const char *, ...);
    void (*memset)(void *, int, size_t);
    void (*memcpy)(void *, void *, size_t);
    void *(*calloc)(size_t, size_t);
    void *(*malloc)(size_t);
    void (*free)(void *);
    VirtualFile *(*fopen)(const char *, const char *);
    int (*fclose)(VirtualFile *);
    size_t (*fread)(void *, size_t, size_t, VirtualFile *);
    size_t (*fwrite)(const void *, size_t, size_t, VirtualFile *);
} FirmwareJMPTable;

#define FW_API ((FirmwareJMPTable*)API_TABLE_ADDRESS)
extern const FirmwareJMPTable api_table;    // This is in standard ram

#ifdef __cplusplus
}
#endif
#ifdef IS_CLIENT_PROGRAM

    // Map standard functions to the hardcoded jump table pointer
    #define malloc(size)                        FW_API->malloc(size)
    #define calloc(nmemb, size)                 FW_API->calloc(nmemb, size)
    #define free(ptr)                           FW_API->free(ptr)
    #define memcpy(dest, src, n)                FW_API->memcpy(dest, src, n)
    #define memset(dest, val, n)                FW_API->memset(dest, val, n)
    #define printf(format, ...)                 FW_API->printf(format, ##__VA_ARGS__)
    #define sleep(time)                         FW_API->sleep(time)
    #define fopen(filename, mode)               FW_API->fopen(filename, mode)
    #define fclose(stream)                      FW_API->fclose(stream)
    #define fread(ptr, size, count, stream)     FW_API->fread(ptr, size, count, stream)
    #define fwrite(ptr, size, count, stream)    FW_API->fwrite(ptr, size, count, stream)
#endif

#endif // PAL_STDLIB_H