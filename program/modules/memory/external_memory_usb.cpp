#include "external_memory.h"

#include "memory.hpp"
#include "internal_memory.h"
#include "comm_commands.h"

#include <cstring>
#include "pico/multicore.h"
#include "hardware/irq.h"


#include "debug_led.h"

#ifdef USB_COMM

static void send_chunked(uint8_t* data, uint32_t len);

ExternalMemory* ext_mem_instance = nullptr;
volatile bool request_copied = false;

void ExternalMemory::core0_fifo_isr() {
    BaseType_t ret;
    while(true) {
        if (multicore_fifo_rvalid()) {
            MemoryRequest* req = (MemoryRequest*)multicore_fifo_pop_blocking();
            if(req != nullptr) {
                do {
                    ret = xQueueSend(mem_requests, req, 0);

                    if(ret == errQUEUE_FULL) {
                        vTaskDelay(pdMS_TO_TICKS(1));   // Give time for the fifo to drain
                    }
                } while(ret == errQUEUE_FULL);

                // ACK back to Core 1 that the memory has been safely copied
                request_copied = true;
                __DMB();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}


void ExternalMemory::setup_dma() {
    // Not used in this version
}

void ExternalMemory::run() {
    while (true) {
        MemoryRequest active_req;  // Local variable - each request gets its own storage
        
        // Wait for a request from the VMM
        if (xQueueReceive(mem_requests, &active_req, portMAX_DELAY) == pdTRUE) {
            // Store the buffer pointer for external access
            current_request_buffer = active_req.buffer;
            
            switch(active_req.op) {
                case MemoryOp::READ: {
                    // The page is not in memory. Load it.
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::PAGE_TABLE_READ,
                        sizeof(uint32_t),  // The 4 bytes of the page id to read
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req.arg1), sizeof(uint32_t));
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    break;
                }

                case MemoryOp::WRITE: {
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::PAGE_TABLE_WRITE,
                        PAGE_SIZE + sizeof(uint32_t),  // The actual data + the 4 bytes of the page id to write
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req.arg1), sizeof(uint32_t));
                    send_chunked(active_req.buffer, PAGE_SIZE);
                    break;
                }

                case MemoryOp::ALLOC: {
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::PAGE_TABLE_ALLOC,
                        sizeof(uint32_t),    // Tell the swap system the size of the block we are requesting
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req.arg2), sizeof(uint32_t));

                    // Sleep until the request is done
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                    // Get the virtual address
                    active_req.arg1 = (uint32_t)rx_buffer;
                    break;
                }

                case MemoryOp::FREE: {
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::PAGE_TABLE_FREE,
                        sizeof(uint32_t),       // The address to free
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&(active_req.arg1), sizeof(uint32_t));
                    break;
                }

                /* MISC */
                case MemoryOp::LOG: {
                    uint32_t log_size = strlen((char *)active_req.buffer);
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::PRINTF_LOG,
                        log_size,
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked(active_req.buffer, log_size);
                    break;
                }

                /* FILE */
                case MemoryOp::FOPEN: {
                    char* filename = (char *)(active_req.arg1);
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::FILE_OPEN,
                        strlen(filename)
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)filename, strlen(filename));

                    // Sleep until the request is done
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    break;
                }

                case MemoryOp::FCLOSE: {
                    uint32_t remote_file_id = active_req.arg3;
                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::FILE_CLOSE,
                        sizeof(uint32_t)
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&remote_file_id, sizeof(uint32_t));
                    break;
                }

                case MemoryOp::FREAD: {
                    struct __attribute__((__packed__)) {
                        uint32_t file_offset;
                        uint32_t data_length;
                        uint32_t remote_file_id;
                    } file_read_header;
                    file_read_header.file_offset = active_req.arg1;
                    file_read_header.data_length = active_req.arg2;
                    file_read_header.remote_file_id = active_req.arg3;

                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::FILE_READ,
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
                    file_write_header.file_offset = active_req.arg1;
                    file_write_header.data_length = active_req.arg2;
                    file_write_header.remote_file_id = active_req.arg3;

                    CommunicationHeader header = {
                        MCU_ID,
                        CommCommand::FILE_WRITE,
                        sizeof(file_write_header) + VIRTUAL_FILE_PAGE_SIZE,  // The file header + the actual data
                    };
                    send_chunked((uint8_t*)&header, sizeof(header));
                    send_chunked((uint8_t*)&file_write_header, sizeof(file_write_header));
                    send_chunked(active_req.buffer, VIRTUAL_FILE_PAGE_SIZE);
                    break;
                }
            }

            if (active_req.req != nullptr) {
                active_req.req->arg1 = active_req.arg1;
                active_req.req->arg2 = active_req.arg2;
                active_req.req->arg3 = active_req.arg3;
            }

            if(active_req.task != NULL || active_req.from_core1) {
                // xTaskNotifyGive(active_req.task);
                // Notify internal memory that the page has been provided via rx_buffer
                internal_memory->notify_completion(&active_req);
            }
        }
    }
}

ExternalMemory::ExternalMemory(VMM *internal_memory, uint32_t queue_size) {
    mem_requests = xQueueCreate(queue_size, sizeof(MemoryRequest));
    setup_dma();
    this->internal_memory = internal_memory;
    current_request_buffer = nullptr;
    ext_mem_instance = this;
}

void ExternalMemory::start() {
    // irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(0), core0_fifo_isr);
    // irq_set_priority(SIO_FIFO_IRQ_NUM(0), 0xFF);
    // irq_set_enabled(SIO_FIFO_IRQ_NUM(0), true);

    xTaskCreate(run_task_entry, "USB_DMA_VMM", 256, this, configMAX_PRIORITIES - 1, &run_task);
    xTaskCreate(core0_fifo_task_entry, "C0_FIFO_LSTNR", 128, this, configMAX_PRIORITIES - 2, &core0_fifo_task);
#if(configNUMBER_OF_CORES == 2)
    vTaskCoreAffinitySet(run_task, SYSTEM_CORE_AFFINITY);
    vTaskCoreAffinitySet(core0_fifo_task, SYSTEM_CORE_AFFINITY);
#endif
}

void ExternalMemory::submit_request(MemoryRequest &req) {
    req.from_core1 = (get_core_num() == 1);

    if (req.from_core1) {
        request_copied = false;
        __DMB();
        
        multicore_fifo_push_blocking((uintptr_t)&req);

        // Wait for ACK from core1
        while (!request_copied) {
            tight_loop_contents();
        }
    } else {
        xQueueSend(mem_requests, &req, portMAX_DELAY);
    }
}

uint8_t* ExternalMemory::get_memory_request_sram_buffer() {
    return current_request_buffer;
}

void ExternalMemory::notify_transfer_completion(void *buffer) {
    rx_buffer = buffer;

    // Notify the ExternalMemory Task to process the full page
    // xTaskNotifyGive(run_task);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(run_task, &xHigherPriorityTaskWoken);
    
    // Yield if waking the run_task caused it to have a higher priority 
    // than the currently interrupted task
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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
            tud_vendor_write_flush();
            sent += chunk;
        } else {
            // FIFO Full: Yield to FreeRTOS to let USB ISR drain buffer
            vTaskDelay(10);
        }
    }
}

#endif