#include "usb_comm.h"

#include "comm_commands.h"
#include "pal.h"
#include "memory.hpp"   // For the page size
#include "client_map.h"
#include "virtual_file.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <pico/multicore.h>

#include "debug_led.h"

TaskHandle_t usb_device_task_tcb;
// Queue and task for handling commands that must not block the USB task
typedef struct {
    uint8_t cmd;
    uint32_t vaddr;
    void *buffer;
} UsbCommand_t;
static QueueHandle_t usb_command_queue = NULL;
TaskHandle_t usb_command_task_tcb = NULL;

// === Multi-Packet RX State Machine ===
enum class RxState {
    IDLE,           // No transfers occurring
    EXT_MEM,        // Receiving a Page Table or File page
    CLIENT_LOAD     // Receiving Client Snapshot data
};

static RxState current_rx_state = RxState::IDLE;    // The current state machine state
static uint16_t expected_data_length = PAGE_SIZE;   // The size of the data being recieved
static uint8_t* page_dest_ptr = nullptr;            // Pointer to where we are writing
static uint32_t transfer_offset = 0;                // How many bytes we've written

// A static buffer to hold all incoming pieces of the Client Snapshot
static uint8_t client_load_buffer[1024];            // 1kb

// =======================

// Command handler task: runs potentially blocking operations (vmm.access, load_frame)
void usb_command_task(void *param) {
    UsbCommand_t cmd;
    while (1) {
        if (xQueueReceive(usb_command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.cmd) {
                case START_CLIENT: {
                    // Make the new address resident (may block waiting for external memory)
                    vmm.access(cmd.vaddr, VMM::MpuRegionSlot::SLOT_EXEC);
                    _vprintf("After vmm access. Offset: 0x%x", cmd.vaddr - VIRTUAL_MEMORY_BASE);
                    CLIENT::load_frame(vmm.get_physical_ptr(cmd.vaddr));
                    CLIENT::start_client_task();
                    break;
                }
                case LOAD_CLIENT: {
                    irq_set_pending(CLIENT::LOAD_IRQ_NUM);
                    break;
                }
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

    // Create queue and command task to avoid blocking the USB task on memory access
    usb_command_queue = xQueueCreate(10, sizeof(UsbCommand_t));
    xTaskCreate(
        usb_command_task,
        "usb_cmd",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        &usb_command_task_tcb
    );

#if(configNUMBER_OF_CORES == 2)
    vTaskCoreAffinitySet(usb_device_task_tcb, SYSTEM_CORE_AFFINITY);
    vTaskCoreAffinitySet(usb_command_task_tcb, SYSTEM_CORE_AFFINITY);
#endif
}

// The Task Function
void usb_device_task(void *param) {
    // This loop handles all USB events (setup packets, data transfer, etc.)
    while (1) {
        // tud_task() is now blocking because of CFG_TUSB_OS
        tud_task();
        // ws2812_send_pixel(100,100,100);
    }
}

uint16_t expected_data_length;

// === VMM specific. This won't exist for the spi version ===
// Helper to safely move data
// We use memcpy here because for <64 bytes, the CPU overhead of setting up
// a blocking DMA is often higher than just copying the memory.
void buffer_data_chunk(const uint8_t* src, size_t len) {
    if (transfer_offset + len > PAGE_SIZE) {
        len = PAGE_SIZE - transfer_offset; // Prevent overflow
    }

    // Copy from USB buffer to VMM memory
    memcpy(page_dest_ptr + transfer_offset, src, len);
    transfer_offset += len;
}
// ==========================================================

// === Client Specific ===
void unpack_client_buffer() {
    uint32_t offset = 0;

    memcpy((void*)&CLIENT::client_pcb_snapshot, client_load_buffer + offset, sizeof(CLIENT::ClientPCBStatic));
    offset += sizeof(CLIENT::ClientPCBStatic);

    if (CLIENT::client_pcb_snapshot.fpu_active) {
        memcpy((void*)&CLIENT::client_pcb_fpu_snapshot, client_load_buffer + offset, sizeof(CLIENT::ClientPCBFPU));
        offset += sizeof(CLIENT::ClientPCBFPU);
    }

    uint32_t addr_map_bytes = sizeof(StaticAddressMap<VMM::ADJUSTED_ADDRESS_LIMIT>::AddressMap) * CLIENT::client_pcb_snapshot.addr_map_size;
    memcpy((void*)CLIENT::client_address_map_snapshot, client_load_buffer + offset, addr_map_bytes);
    offset += addr_map_bytes;

    uint32_t v_file_bytes = sizeof(VirtualFile) * CLIENT::client_pcb_snapshot.open_file_cnt;
    memcpy((void*)CLIENT::client_virtual_file_snapshot, client_load_buffer + offset, v_file_bytes);

    if (usb_command_queue != NULL) {
        UsbCommand_t cmd = { .cmd = LOAD_CLIENT };
        xQueueSend(usb_command_queue, &cmd, 0);
    }
}
// =========================

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    if(bufsize == 0)  {
        return;
    }

    // If we are already receiving a page, treat this entire packet as data
    if (current_rx_state != RxState::IDLE) {
        buffer_data_chunk(buffer, bufsize);

        if (transfer_offset >= expected_data_length) {
            // Transfer Complete!
            if (current_rx_state == RxState::EXT_MEM) {
                external_memory.notify_transfer_completion();
            } else if (current_rx_state == RxState::CLIENT_LOAD) {
                unpack_client_buffer();
            }
            current_rx_state = RxState::IDLE;
        }
        tud_vendor_read_flush();
        return;
    }

    // Ensure we have at least the header (MCU_ID + CMD + DATA_LENGTH)
    if (bufsize < sizeof(CommunicationHeader)) {
        return;
    }

    CommunicationHeader* header = (CommunicationHeader*)buffer;
    if (header->mcu_id != MCU_ID) {
        return;
    }

    uint8_t command = header->cmd;
    uint16_t payload_len = header->data_length;
    uint16_t header_size = sizeof(CommunicationHeader);
    
    switch (command)
    {
        case PAGE_TABLE_WRITE:
        case FILE_READ:
            page_dest_ptr = external_memory.get_memory_request_sram_buffer();
            transfer_offset = 0;
            expected_data_length = payload_len;
            current_rx_state = RxState::EXT_MEM;

            // Handle payload included in THIS packet (after the 4 header bytes)
            if (bufsize > header_size) {
                buffer_data_chunk(buffer + header_size, bufsize - header_size);
            }

            // Check if the entire payload somehow arrived in the first packet
            if (transfer_offset >= expected_data_length) {
                current_rx_state = RxState::IDLE;
                external_memory.notify_transfer_completion();
            }
            break;

        case FILE_OPEN: {
            int32_t remote_file_id;
            memcpy(&remote_file_id, buffer + header_size, sizeof(int32_t));
            external_memory.notify_transfer_completion((void *)remote_file_id);
            break;
        }

        case PAGE_TABLE_ALLOC: {
            uint32_t vaddr;
            memcpy(&vaddr, buffer + header_size, sizeof(uint32_t));
            external_memory.notify_transfer_completion((void *)vaddr);
            break;
        }

        case START_CLIENT: { // This also resets the client if it is actively running
            uint32_t vaddr;
            memcpy(&vaddr, buffer + header_size, sizeof(uint32_t));
            // Enqueue the request for the command task to process so USB task doesn't block
            if (usb_command_queue != NULL) {
                UsbCommand_t cmd = { .cmd = START_CLIENT, .vaddr = vaddr };
                BaseType_t ok = xQueueSend(usb_command_queue, &cmd, 0);
            }
            break;
        }

        case LOAD_CLIENT: {
            expected_data_length = payload_len;

            page_dest_ptr = client_load_buffer;
            transfer_offset = 0;
            current_rx_state = RxState::CLIENT_LOAD;

            // Handle payload included in THIS packet (after the 4 header bytes)
            if (bufsize > header_size) {
                buffer_data_chunk(buffer + header_size, bufsize - header_size);
            }

            // Check if the entire payload somehow arrived in the first packet
            if (transfer_offset >= expected_data_length) {
                current_rx_state = RxState::IDLE;
                unpack_client_buffer();
            }
            break;
        }
        default:
            // An invalid command was sent
            break;
    }
    tud_vendor_read_flush();
}


// void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes) {
//     tud_vendor_write_flush();   // Flush any excess data
// }
