#include "test_program_main.h"
#include "pico/stdlib.h"

// Interceptor macro
#include "../../program/modules/pal/pal_stdlib.h"

const int TST_PAGE_SIZE = 4096;
const int TST_PAGE_CNT = 1000;

void test_program_main(int argc, char *argv[])
{
    // === PRINTF + SLEEP ===
    printf("Hello from test_program_main!");
    sleep(1000);
    // === END PRINTF + SLEEP ===

    // === MALLOC ===
    printf("Small malloc test (50 ints).");
    int *malloc_arr1 = (int *)malloc(50 * sizeof(int));
    if(malloc_arr1 == NULL) {
        while(1) {
            printf("Malloc failed. malloc_arr1 == NULL.");
            sleep(5000);
        }
    }
    for(int i = 0; i < 50; ++i) {
        malloc_arr1[i] = i;
        printf("i: %d.", i);
    }
    sleep(1000);
    free(malloc_arr1);

    printf("Multi-Page malloc Test.");
    int *malloc_arr2 = (int *)malloc(2 * TST_PAGE_SIZE * sizeof(int));
    if(malloc_arr2 == NULL) {
        while(1) {
            printf("Multi-Page malloc failed. malloc_arr2 == NULL.");
            sleep(5000);
        }
    }
    for(int i = 0; i < 2 * TST_PAGE_SIZE; ++i) {
        malloc_arr2[i] = i;
    }
    sleep(1000);
    free(malloc_arr2);
    // === END MALLOC ===

    // CALLOC
    printf("Small calloc test (50 ints).");
    int *calloc_arr1 = (int *)calloc(50, sizeof(int));
    if(calloc_arr1 == NULL) {
        while(1) {
            printf("Calloc failed. calloc_arr1 == NULL.");
            sleep(5000);
        }
    }
    for(int i = 0; i < 50; ++i) {
        if(calloc_arr1[i] != 0) {
            while(1) {
                printf("Calloc failed. calloc_arr1[i] != 0. i = %d", i);
                sleep(5000);
            }
        };
    }
    sleep(1000);
    free(calloc_arr1);

    printf("Multi-Page calloc Test.");
    int *calloc_arr2 = (int *)calloc(2 * TST_PAGE_SIZE, sizeof(int));
    if(calloc_arr2 == NULL) {
        while(1) {
            printf("Multi-Page calloc failed. calloc_arr2 == NULL.");
            sleep(5000);
        }
    }
    for(int i = 0; i < 2 * TST_PAGE_SIZE; ++i) {
        if(calloc_arr2[i] != 0) {
            while(1) {
                printf("Calloc failed. calloc_arr2[i] != 0. i = %d", i);
                sleep(5000);
            }
        };
    }
    sleep(1000);
    free(calloc_arr2);
    // === END CALLOC ===

    // === MEMCPY ===
    printf("Small memcpy test (50 ints).");
    int *memcpy_src1 = (int *)malloc(50 * sizeof(int));
    int *memcpy_dst1 = (int *)malloc(50 * sizeof(int));
    if(memcpy_src1 == NULL || memcpy_dst1 == NULL) {
        while(1) {
            printf("Memcpy malloc failed.");
            sleep(5000);
        }
    }
    for(int i = 0; i < 50; ++i) {
        memcpy_src1[i] = i;
    }
    memcpy(memcpy_dst1, memcpy_src1, 50 * sizeof(int));
    for(int i = 0; i < 50; ++i) {
        if(memcpy_dst1[i] != memcpy_src1[i]) {
            while(1) {
                printf("Memcpy failed. memcpy_dst1[i] != memcpy_src1[i]. i = %d", i);
                sleep(5000);
            }
        }
    }
    sleep(1000);
    free(memcpy_src1);
    free(memcpy_dst1);

    printf("Multi-Page memcpy test.");
    int *memcpy_src2 = (int *)malloc(2 * TST_PAGE_SIZE * sizeof(int));
    int *memcpy_dst2 = (int *)malloc(2 * TST_PAGE_SIZE * sizeof(int));
    if(memcpy_src2 == NULL || memcpy_dst2 == NULL) {
        while(1) {
            printf("Multi-Page memcpy malloc failed.");
            sleep(5000);
        }
    }
    for(int i = 0; i < 2 * TST_PAGE_SIZE; ++i) {
        memcpy_src2[i] = i;
    }
    memcpy(memcpy_dst2, memcpy_src2, 2 * TST_PAGE_SIZE * sizeof(int));
    for(int i = 0; i < 2 * TST_PAGE_SIZE; ++i) {
        if(memcpy_dst2[i] != memcpy_src2[i]) {
            while(1) {
                printf("Multi-Page memcpy failed. memcpy_dst2[i] != memcpy_src2[i]. i = %d", i);
                sleep(5000);
            }
        }
    }
    sleep(1000);
    free(memcpy_src2);
    free(memcpy_dst2);
    // === END MEMCPY ===

    // === MEMSET ===
    printf("Small memset test (50 ints).");
    int *memset_arr1 = (int *)malloc(50 * sizeof(int));
    if(memset_arr1 == NULL) {
        while(1) {
            printf("Memset malloc failed.");
            sleep(5000);
        }
    }
    memset(memset_arr1, 0xAA, 50 * sizeof(int));
    unsigned char *memset_bytes1 = (unsigned char *)memset_arr1;
    for(int i = 0; i < 50 * sizeof(int); ++i) {
        if(memset_bytes1[i] != 0xAA) {
            while(1) {
                printf("Memset failed. memset_bytes1[i] != 0xAA. i = %d", i);
                sleep(5000);
            }
        }
    }
    sleep(1000);
    free(memset_arr1);

    printf("Multi-Page memset test.");
    int *memset_arr2 = (int *)malloc(2 * TST_PAGE_SIZE * sizeof(int));
    if(memset_arr2 == NULL) {
        while(1) {
            printf("Multi-Page memset malloc failed.");
            sleep(5000);
        }
    }
    memset(memset_arr2, 0xAA, 2 * TST_PAGE_SIZE * sizeof(int));
    unsigned char *memset_bytes2 = (unsigned char *)memset_arr2;
    for(int i = 0; i < 2 * TST_PAGE_SIZE * sizeof(int); ++i) {
        if(memset_bytes2[i] != 0xAA) {
            while(1) {
                printf("Multi-Page memset failed. memset_bytes2[i] != 0xAA. i = %d", i);
                sleep(5000);
            }
        }
    }
    sleep(1000);
    free(memset_arr2);
    // === END MEMSET ===

    while(1) {
        printf("Tests Completed!");
        sleep(5000);
    }
}