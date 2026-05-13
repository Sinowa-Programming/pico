#include "debug_led.h"
#include "hardware/timer.h"

void ws2812_send_bit(bool bit) {
    if (bit) {
        // WS2812 '1' : High for ~0.8us, Low for ~0.45us
        sio_hw->gpio_set = PIN_MASK;         // Instant hardware pin HIGH
        for(volatile int i=0; i<30; i++);    // High delay
        sio_hw->gpio_clr = PIN_MASK;         // Instant hardware pin LOW
        for(volatile int i=0; i<10; i++);    // Low delay
    } else {
        // WS2812 '0' : High for ~0.4us, Low for ~0.85us
        sio_hw->gpio_set = PIN_MASK;         // Instant hardware pin HIGH
        for(volatile int i=0; i<3; i++);     // CRITICAL: Extremely short High delay
        sio_hw->gpio_clr = PIN_MASK;         // Instant hardware pin LOW
        for(volatile int i=0; i<30; i++);    // Low delay
    }
}

void ws2812_send_pixel(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    
    // Interrupts MUST remain disabled during this, or the timing breaks again
    uint32_t mask = save_and_disable_interrupts();

    for (int i = 23; i >= 0; i--) {
        ws2812_send_bit((color >> i) & 1);
    }

    restore_interrupts(mask);
    
    // Latch delay to tell the LED the frame is done
    busy_wait_us_32(60);
}

void blink_binary(uint8_t number) {
    // We will blink 8 bits, starting from the highest bit (Most Significant Bit)
    for (int i = 7; i >= 0; i--) {
        bool is_one = (number >> i) & 1;
        
        if (is_one) {
            // Bit is 1: Green
            ws2812_send_pixel(0, 255, 0);
        } else {
            // Bit is 0: Red
            ws2812_send_pixel(255, 0, 0);
        }
        
        // Hold the color so you can see it
        sleep_ms(700);
        
        // Turn off briefly between bits to distinguish them
        ws2812_send_pixel(0, 0, 0);
        sleep_ms(300);
    }

    ws2812_send_pixel(255, 69, 0);  // Orange
    sleep_ms(700);
}


void blink_binary_32(uint32_t number) {
    // We will blink 8 bits, starting from the highest bit (Most Significant Bit)
    for (int i = 31; i >= 0; i--) {
        bool is_one = (number >> i) & 1;
        
        if (is_one) {
            // Bit is 1: Green
            ws2812_send_pixel(0, 255, 0);
        } else {
            // Bit is 0: Red
            ws2812_send_pixel(255, 0, 0);
        }
        
        // Hold the color so you can see it
        sleep_ms(700);
        
        // Turn off briefly between bits to distinguish them
        ws2812_send_pixel(0, 0, 0);
        sleep_ms(300);
    }

    ws2812_send_pixel(255, 69, 0);  // Orange
    sleep_ms(700);
}