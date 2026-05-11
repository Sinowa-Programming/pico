#include "test_program_main.h"
#include "pico/stdlib.h"

// Interceptor macro
#include "../../program/modules/pal/pal_stdlib.h"

const int TST_PAGE_SIZE = 4096;
const int TST_PAGE_CNT = 1000;

void test_program_main(int argc, char *argv[])
{
    while(1) {
        printf("Hello from test_program_main!");
        sleep(1000);
    }
}