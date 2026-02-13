#ifndef COMM_COMMANDS_H
#define COMM_COMMANDS_H
#include <pico/stdlib.h>

/*
* Byte setup:
* 1 byte: MCU ID
* 1 byte: Command
* 2 bytes: Data length( Different for PAGE_TABLE_SET )
* N bytes: Data (optional and sent separately after the header)
*
* ALT( PAGE_TABLE_SET ):
* - instead of Data length, the two bytes are:
* - - 1 byte: Frame idx to write to.
* - - 1 byte: Unused
*/

/*
* Each page will be 4098 bytes in transmission:
* 2 bytes - Page Id
* 4096 bytes - Page data
*/
const uint8_t MCU_ID = 0;

struct __attribute__((packed)) CommunicationHeader {
    uint8_t mcu_id;
    uint8_t cmd;
    uint16_t data_length;
};

enum {
    // Client commands( Commands for the program running though the PAL ).
    START_CLIENT = 0x1,
    HALT_CLIENT = 0X2,
    RESUME_CLIENT = 0X3,

    PAGE_TABLE_SET = 0xE,  // Write a page of data to a specific frame. For starting execution by writing to frame 0.
    PAGE_TABLE_TRANSMISSION = 0xF   // The device has received a page.
};



#endif  // COMM_COMMANDS_H