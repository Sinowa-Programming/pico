#include "usb_comm.h"

// Temportaily for printf
#include <stdio.h>
#include <pico/stdlib.h>

#include "comm_commands.h"
#include "pal.h"
#include "memory.hpp"   // For the page size
#include "client_map.h"
#include "virtual_file.h"
#include "FreeRTOS.h"
#include "queue.h"

#include "debug_led.h"

TaskHandle_t usb_device_task_tcb;
// Queue and task for handling commands that must not block the USB task
typedef struct {
    uint8_t cmd;
    uint32_t vaddr;
} UsbCommand_t;
static QueueHandle_t usb_command_queue = NULL;
TaskHandle_t usb_command_task_tcb = NULL;

// Definitions for variables declared extern in usb_comm.h
uint8_t* page_dest_ptr = nullptr;
uint32_t transfer_offset = 0;
bool data_receiving = false;

// Command handler task: runs potentially blocking operations (vmm.access, load_frame)
void usb_command_task(void *param) {
    UsbCommand_t cmd;
    while (1) {
        if (xQueueReceive(usb_command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.cmd) {
                case START_CLIENT:
                    // Make the new address resident (may block waiting for external memory)
                    vmm.access(cmd.vaddr, false);
                    CLIENT::load_frame(vmm.get_physical_ptr(cmd.vaddr));
                    ws2812_send_pixel(0, 255, 0); // Green
                    break;
                default:
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void usb_comm_setup() {
    tusb_init();

    xTaskCreate(
        usb_device_task,
        "usbd",
        USBD_STACK_SIZE,
        NULL,
        USBD_PRIORITY,
        &usb_device_task_tcb
    );
    vTaskCoreAffinitySet(usb_device_task_tcb, SYSTEM_CORE_AFFINITY);

    // Create queue and command task to avoid blocking the USB task on memory access
    usb_command_queue = xQueueCreate(4, sizeof(UsbCommand_t));
    xTaskCreate(
        usb_command_task,
        "usb_cmd",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        &usb_command_task_tcb
    );
    vTaskCoreAffinitySet(usb_command_task_tcb, SYSTEM_CORE_AFFINITY);
}

// The Task Function
void usb_device_task(void *param) {
    // This loop handles all USB events (setup packets, data transfer, etc.)
    while (1) {
        // tud_task() is now blocking because of CFG_TUSB_OS
        tud_task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// === VMM specific. This won't exist for the spi version ===
// Helper to safely move data
// We use memcpy here because for <512 bytes, the CPU overhead of setting up
// a blocking DMA is often higher than just copying the memory.
void buffer_data_chunk(const uint8_t* src, size_t len) {
    if (transfer_offset + len > PAGE_SIZE) {
        len = PAGE_SIZE - transfer_offset; // Prevent overflow
    }

    // Copy from ephemeral USB buffer to safe VMM memory
    memcpy(page_dest_ptr + transfer_offset, src, len);
    transfer_offset += len;
}
// ==========================================================

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    // 2. PARSE NEW COMMANDS
    // Ensure we have at least the header (MCU_ID + CMD + data_length)
    if (bufsize == 0) return;

    // If we are already receiving a raw data transfer (no header expected),
    // treat this entire packet as data.
    if (data_receiving) {
        buffer_data_chunk(buffer, bufsize);

        if (transfer_offset >= PAGE_SIZE) {
            data_receiving = false;
            external_memory.notify_transfer_completion();
        }
        return;
    }

    // Need at least 4 bytes for the communication header
    if (bufsize < 4) return;

    uint8_t mcu_id = buffer[0];
    if (mcu_id != MCU_ID) return;

    uint8_t command = buffer[1];
    uint16_t data_length = (uint16_t)buffer[2] | ((uint16_t)buffer[3] << 8);

    // payload starts after the 4-byte header
    const uint8_t *payload = buffer + 4;
    size_t payload_size = (bufsize > 4) ? (bufsize - 4) : 0;

    blink_binary(command);

    switch (command)
    {
        case PAGE_TABLE_WRITE: {
            if (payload_size < sizeof(uint32_t)) break;
            uint32_t page_id_raw;
            memcpy(&page_id_raw, payload, sizeof(uint32_t));

            uint32_t page_index;
            if (page_id_raw >= VIRTUAL_MEMORY_BASE) {
                page_index = (page_id_raw - VIRTUAL_MEMORY_BASE) / PAGE_SIZE;
            } else {
                page_index = page_id_raw;
            }
            if (page_index >= NUM_PAGES) break;

            page_dest_ptr = vmm.sram_frames[page_index];
            transfer_offset = 0;
            data_receiving = true;

            // Handle any data included in this packet after the page id
            if (payload_size > sizeof(uint32_t)) {
                buffer_data_chunk(payload + sizeof(uint32_t), payload_size - sizeof(uint32_t));
            }
            break;
        }

        case PAGE_TABLE_ALLOC: {
            if (payload_size < sizeof(uint32_t)) break;
            uint32_t vaddr;
            memcpy(&vaddr, payload, sizeof(uint32_t));
            external_memory.notify_transfer_completion(&vaddr);
            break;
        }

        case START_CLIENT: {
            if (payload_size < sizeof(uint32_t)) break;
            uint32_t vaddr;
            memcpy(&vaddr, payload, sizeof(uint32_t));
            printf("Got address: 0x%08X\n", vaddr);
            if (usb_command_queue != NULL) {
                UsbCommand_t cmd = { .cmd = START_CLIENT, .vaddr = vaddr };
                BaseType_t ok = xQueueSend(usb_command_queue, &cmd, 0);
                if (ok != pdTRUE) {
                    printf("usb_command_queue full, dropping START_CLIENT\n");
                }
            }
            break;
        }

        case HALT_CLIENT:
            vTaskSuspend(CLIENT::client_task_tcb);
            break;

        case RESUME_CLIENT:
            vTaskResume(CLIENT::client_task_tcb);
            break;

        case FILE_OPEN: {
            if (payload_size < sizeof(uint32_t)) break;
            uint32_t remote_file_id;
            memcpy(&remote_file_id, payload, sizeof(uint32_t));
            external_memory.notify_transfer_completion(&remote_file_id);
            break;
        }

        case FILE_READ:
            page_dest_ptr = (uint8_t *)VIRTUAL_FILE_BASE;
            transfer_offset = 0;
            data_receiving = true;

            if (payload_size > 0) {
                buffer_data_chunk(payload, payload_size);
            }
            break;

        default:
            // An invalid or unhandled command was sent
            break;
    }
}
