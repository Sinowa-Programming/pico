#include "external_memory.h"
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
            if (active_req->op == MemoryOp::READ) {
                // The page is not in memory. Load it.
                CommunicationHeader header = {
                    MCU_ID,
                    PAGE_TABLE_READ,
                    2,  // The two bytes of the page id to read
                };
                send_chunked((uint8_t*)&header, sizeof(header));
                send_chunked((uint8_t* )&(active_req->v_page_id), 2);
            } else if(active_req->op == MemoryOp::WRITE) {
                CommunicationHeader header = {
                    MCU_ID,
                    PAGE_TABLE_WRITE,
                    PAGE_SIZE + 2,  // The actual data + the two bytes of the page id to write
                };
                send_chunked((uint8_t*)&header, sizeof(header));
                send_chunked((uint8_t* )&(active_req->v_page_id), 2);
                send_chunked(active_req->sram_buffer, 4096);
            } else if(active_req->op == MemoryOp::ALLOC) {
                CommunicationHeader header = {
                    MCU_ID,
                    PAGE_TABLE_ALLOC,
                    0,              // Just telling the page system to give me a page number
                };
                send_chunked((uint8_t*)&header, sizeof(header));
            }


            // Sleep until the task is done
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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
    return active_req->sram_buffer;
}

void ExternalMemory::notify_transfer_completion() {
    // Notify the ExternalMemory Task to process the full page
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyGive(run_task);
}

// Tell the External Memory Manager that a memory address has been allocated to it.
void ExternalMemory::notify_allocation_completion(uint32_t v_mem_addr)
{
    active_req->v_page_id = v_mem_addr / PAGE_SIZE;
    internal_memory->notify_completion(active_req);
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