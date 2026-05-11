#include "pal_stdlib.h"
#include "pal.h"

#ifdef __cplusplus
extern "C" {
#endif

FirmwareJMPTable api_table __attribute__((section(".api_table_section"))) = {
    .sleep  = _vsleep,
    .printf = _vprintf,
    .memset = _vmemset,
    .memcpy = _vmemcpy,
    .calloc = _vcalloc,
    .malloc = _vmalloc,
    .free   = _vfree
};

#ifdef __cplusplus
}
#endif
