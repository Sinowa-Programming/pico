#ifndef USB_COMM_H
#define USB_COMM_H

#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"

#define USBD_STACK_SIZE     4096
#define USBD_PRIORITY       (configMAX_PRIORITIES - 1) // High priority recommended

extern TaskHandle_t usb_device_task_tcb;

void usb_device_task(void *param);

void usb_comm_setup();

// The Task Function
void usb_device_task(void *param);

// Helper to safely move data
// We use memcpy here because for <512 bytes, the CPU overhead of setting up
// a blocking DMA is often higher than just copying the memory.
void buffer_data_chunk(const uint8_t* src, size_t len);
// ==========================================================

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize);
// ------------------

#endif  // USB_COMM_H