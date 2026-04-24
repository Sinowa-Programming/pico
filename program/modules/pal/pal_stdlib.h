// This file is meant to copy stdlib.h source code closely and
// functions are added based on what is required to compile the program

#include "pal.h"    // Has the memory functions


// Map standard functions to the wrappers
#define malloc(size) vmalloc(size)
#define calloc(nmemb, size) vcalloc(nmemb, size)
#define free(ptr) vfree(ptr)
#define memcpy(dest, src, n) vmemcpy(dest, src, n)
#define printf(format, ...) vprintf(format, __VA_ARGS__)