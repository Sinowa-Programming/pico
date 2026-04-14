#include "external_memory.h"

#include <cstring>
#include "memory.hpp"
#include "internal_memory.h"
#include "comm_commands.h"

#ifdef USB_COMM

static void transmit_page(uint8_t* page_data, uint32_t page_index);
static void send_chunked(uint8_t* data, uint32_t len);

void ExternalMemory::setup_dma() {
    // Not used in this version
}

void ExternalMemory::run() {
    active_req;
    while (true) {
        // Wait for a request from the VMM
        if (xQueueReceive(mem_requests, &active_req, portMAX_DELAY)) {
            switch(active_req->op) {
                case MemoryOp::READ: {
                    // The page is not in memory. Load it.
                    CommunicationHeader header = {
                        MCU_ID,
                        PAGE_TABLE_READ,
                        2,  // The two bytes of the page id to read
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req->arg1), 4);
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    // Notify internal memory that the page has been provided via rx_buffer
                    internal_memory->notify_completion(active_req);
                    break;
                }

                case MemoryOp::WRITE: {
                    CommunicationHeader header = {
                        MCU_ID,
                        PAGE_TABLE_WRITE,
                        PAGE_SIZE + 4,  // The actual data + the 4 bytes of the page id to write
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req->arg1), 4);
                    send_chunked(active_req->buffer, PAGE_SIZE);
                    break;
                }

                case MemoryOp::ALLOC: {
                    CommunicationHeader header = {
                        MCU_ID,
                        PAGE_TABLE_ALLOC,
                        sizeof(uint32_t),    // Tell the swap system the size of the block we are requesting
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req->arg2), sizeof(uint32_t));

                    // Sleep until the request is done
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                    // Get the remote file id
                    active_req->arg2 = (uint32_t)rx_buffer / PAGE_SIZE;
                    internal_memory->notify_completion(active_req);
                    break;
                }

                /* FILE */
                case MemoryOp::FOPEN: {
                    char* filename = (char *)(active_req->arg1);
                    CommunicationHeader header = {
                        MCU_ID,
                        FILE_OPEN,
                        strlen(filename)
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)filename, strlen(filename));

                    // Sleep until the request is done
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    break;
                }
                case MemoryOp::FCLOSE: {
                    uint32_t remote_file_id = active_req->arg3;
                    CommunicationHeader header = {
                        MCU_ID,
                        FILE_CLOSE,
                        4
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&remote_file_id, sizeof(remote_file_id));
                    break;
                }
                case MemoryOp::FREAD: {
                    struct __attribute__((__packed__)) {
                        uint32_t file_offset;
                        uint32_t data_length;
                        uint32_t remote_file_id;
                    } file_read_header;
                    file_read_header.file_offset = active_req->arg1;
                    file_read_header.data_length = active_req->arg2;
                    file_read_header.remote_file_id = active_req->arg3;

                    CommunicationHeader header = {
                        MCU_ID,
                        FILE_READ,
                        sizeof(file_read_header)
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&file_read_header, sizeof(file_read_header));

                    // Sleep until the request is done
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    break;
                }
                case MemoryOp::FWRITE: {
                    // Data to be sent over
                    struct __attribute__((__packed__)) {
                        uint32_t file_offset;
                        uint32_t data_length;
                        uint32_t remote_file_id;
                    } file_write_header;
                    file_write_header.file_offset = active_req->arg1;
                    file_write_header.data_length = active_req->arg2;
                    file_write_header.remote_file_id = active_req->arg3;

                    CommunicationHeader header = {
                        MCU_ID,
                        FILE_WRITE,
                        sizeof(file_write_header) + VIRTUAL_FILE_PAGE_SIZE,  // The file header + the actual data
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&file_write_header, sizeof(file_write_header));
                    send_chunked(active_req->buffer, VIRTUAL_FILE_PAGE_SIZE);
                    break;
                }
            }

            if(active_req->task != NULL) {
                xTaskNotifyGive(active_req->task);
            }
        }
    }
}

ExternalMemory::ExternalMemory(VMM *internal_memory, uint32_t queue_size) {
    mem_requests = xQueueCreate(queue_size, sizeof(MemoryRequest));
    setup_dma();
}

void ExternalMemory::start() {
    xTaskCreate(task_entry, "USB_DMA_VMM", 2048, this, configMAX_PRIORITIES - 1, &run_task);
    vTaskCoreAffinitySet(run_task, SYSTEM_CORE_AFFINITY);
}

void ExternalMemory::submit_request(MemoryRequest &req) {
    xQueueSend(mem_requests, &req, portMAX_DELAY);
}

uint8_t* ExternalMemory::get_memory_request_sram_buffer() {
    return active_req->buffer;
}

void ExternalMemory::notify_transfer_completion(void *buffer) {
    rx_buffer = buffer;

    // Notify the ExternalMemory Task to process the full page
    xTaskNotifyGive(run_task);
}

// === Helpers for transferring over full speed usb ===
// Helper: Writes data to USB FIFO, yielding if FIFO is full
static void send_chunked(uint8_t* data, uint32_t len) {
    uint32_t sent = 0;

    while (sent < len) {
        // Check available space in the TinyUSB Ring Buffer
        uint32_t avail = tud_vendor_write_available();

        if (avail > 0) {
            uint32_t chunk = (len - sent < avail) ? (len - sent) : avail;

            // Push to FIFO
            tud_vendor_write(data + sent, chunk);
            sent += chunk;
        } else {
            // FIFO Full: Yield to FreeRTOS to let USB ISR drain buffer
            vTaskDelay(1);
        }
    }
}

#endif