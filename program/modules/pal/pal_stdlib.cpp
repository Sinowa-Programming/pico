#include "pal_stdlib.h"
#include "pal.h"

#ifdef __cplusplus
extern "C" {
#endif

const FirmwareJMPTable api_table = {
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
