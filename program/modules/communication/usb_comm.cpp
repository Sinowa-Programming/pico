#include "usb_comm.h"

#include "comm_commands.h"
#include "pal.h"
#include "memory.hpp"   // For the page size
#include "client_map.h"
#include "virtual_file.h"

TaskHandle_t usb_device_task_tcb;

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
}

// The Task Function
void usb_device_task(void *param) {
    // This loop handles all USB events (setup packets, data transfer, etc.)
    while (1) {
        // tud_task() is now blocking because of CFG_TUSB_OS
        tud_task();
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
    // Ensure we have at least the header (MCU_ID + CMD)
    if (bufsize < 2) return;

    uint8_t mcu_id = buffer[0];
    if (mcu_id != MCU_ID) return;

    // 1. HANDLE ONGOING TRANSMISSION
    // If we are already receiving a page, treat this entire packet as data
    if (data_receiving) {
        buffer_data_chunk(buffer, bufsize);

        if (transfer_offset >= PAGE_SIZE) {
            // Transfer Complete!
            data_receiving = false;

            external_memory.notify_transfer_completion();
        }
        return;
    }

    uint8_t command = buffer[1];

    switch (command)
    {
        case PAGE_TABLE_WRITE:
            page_dest_ptr = vmm.sram_frames[buffer[2]];
            transfer_offset = 0;
            data_receiving = true;

            // Handle payload included in THIS packet (after the 2 header bytes)
            if (bufsize > 2) {
                buffer_data_chunk(buffer + 2, bufsize - 2);
            }
            break;

        case PAGE_TABLE_ALLOC: {
            uint32_t vaddr = buffer[2] << 16 || buffer[3];
            external_memory.notify_transfer_completion(&vaddr);
            break;
        }
        case START_CLIENT: { // This also resets the client if it is actively running
            uint32_t vaddr = buffer[2] << 16 || buffer[3];
            vmm.access(vaddr, false);  // Make the new address resident
            CLIENT::load_frame(vmm.get_physical_ptr(vaddr));    // Set the client to start executing at the address
            break;
        }

        case HALT_CLIENT:
            vTaskSuspend(CLIENT::client_task_tcb);
            break;

        case RESUME_CLIENT:
            vTaskResume(CLIENT::client_task_tcb);
            break;

        case FILE_OPEN: {
            uint32_t remote_file_id = buffer[2] << 16 || buffer[3];
            external_memory.notify_transfer_completion(&remote_file_id);
            break;
        }

        case FILE_READ:
            page_dest_ptr = (uint8_t *)VIRTUAL_FILE_BASE;
            transfer_offset = 0;
            data_receiving = true;

            // Handle payload included in THIS packet (after the 2 header bytes)
            if (bufsize > 2) {
                buffer_data_chunk(buffer + 2, bufsize - 2);
            }
            break;
        default:
            // An invalid command was sent
            break;
    }
}
