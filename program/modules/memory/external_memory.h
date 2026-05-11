#ifndef EXTERNAL_MEMORY_H
#define EXTERNAL_MEMORY_H

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "portmacro.h"

#include "internal_memory.h"

#define USB_COMM
// #define SPI_COMM

#include "tusb.h"

class VMM;

class ExternalMemory {
    TaskHandle_t run_task;

    QueueHandle_t mem_requests;

    void *rx_buffer;    // Stores a pointer to any data the communication handler sends to the External Memory
    MemoryRequest *active_req;

    VMM *internal_memory;

    void setup_dma();

    void run();

    static void task_entry(void* params) {
        ExternalMemory *external_memory = static_cast<ExternalMemory*>(params);
        external_memory->run();
    }

public:
    ExternalMemory() {};
    /// @brief This handles the movement of memory in and out of the host's sram page blocks
    /// @param internal_memory This it the object to respond to when a memory request has been completed.
    /// @param queue_size The amount of request that can be sent
    ExternalMemory(VMM *internal_memory, uint32_t queue_size);
    void start();

    // Tell the External Memory to either read or write a specific page
    void submit_request(MemoryRequest &req);

    uint8_t* get_memory_request_sram_buffer();

    // A page has been transfered
    void notify_transfer_completion(void *buffer = nullptr);
};


static void send_chunked(uint8_t* data, uint32_t len);

#endif  // EXTERNAL_MEMORY_H