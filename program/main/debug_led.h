#ifndef DEBUG_LED_H
#define DEBUG_LED_H
#include <pico/stdlib.h>
#include "hardware/sync.h"
#include "hardware/structs/sio.h" // Required for direct register access

#define LED_PIN 16
#define PIN_MASK (1ul << LED_PIN) // Pre-calculate the bitmask for Pin 16

void ws2812_send_bit(bool bit);

void ws2812_send_pixel(uint8_t r, uint8_t g, uint8_t b);

void blink_binary(uint8_t number);

#endif