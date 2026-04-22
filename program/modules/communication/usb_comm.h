#ifndef USB_COMM_H
#define USB_COMM_H

#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"

// Task stack size: Needs to be large enough to handle USB descriptors and callbacks
// 2048 - 4096 bytes is usually safe for CDC/MSC.
#define USBD_STACK_SIZE     (3*configMINIMAL_STACK_SIZE)
#define USBD_PRIORITY       (configMAX_PRIORITIES - 1) // High priority recommended

extern TaskHandle_t usb_device_task_tcb;

void usb_device_task(void *param);

void usb_comm_setup();

// The Task Function
void usb_device_task(void *param);

// === VMM specific. This won't exist for the spi version ===
// These are declared `extern` here and defined in usb_comm.cpp so other
// translation units (like external_memory_usb.cpp) can set them when they
// expect an incoming transfer from the host.
extern uint8_t* page_dest_ptr; // Pointer to where we are writing
extern uint32_t transfer_offset;         // How many bytes we've written
extern bool data_receiving;      // Are we in the middle of a transfer?

// Helper to safely move data
// We use memcpy here because for <512 bytes, the CPU overhead of setting up
// a blocking DMA is often higher than just copying the memory.
void buffer_data_chunk(const uint8_t* src, size_t len);
// ==========================================================

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize);
// ------------------

#endif  // USB_COMM_H