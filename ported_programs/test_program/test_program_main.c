#include "test_program_main.h"
#include "pico/stdlib.h"

const int TST_PAGE_SIZE = 4096;
const int TST_PAGE_CNT = 1000;

int test_program_main(int argc, char *argv[])
{
    printf("Test Begin");
    
    // Allocation Test
    printf("Starting with allocation test.");
    uint8_t *arr = (uint8_t)malloc(sizeof(uint8_t) * TST_PAGE_SIZE * TST_PAGE_CNT);
    
    printf("Pages allocated. Now accessing every byte in the cell.");
    for(int i = 0; i < TST_PAGE_SIZE * TST_PAGE_CNT; i++) {
        arr[i] = 1;
    }

    printf("Allocation Test done.");

    return 0;
}