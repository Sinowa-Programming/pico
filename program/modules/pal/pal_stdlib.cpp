#include "pal_stdlib.h"


#ifdef __cplusplus
extern "C" {
#endif

FirmwareJMPTable api_table __attribute__((section(".api_table_section"))) = {
    .printf = vprintf,
    .memset = vmemset,
    .memcpy = vmemcpy,
    .calloc = vcalloc,
    .malloc = vmalloc,
    .free   = vfree
};

#ifdef __cplusplus
}
#endif
