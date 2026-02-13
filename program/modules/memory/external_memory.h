#ifndef EXTERNAL_MEMORY_H
#define EXTERNAL_MEMORY_H

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "portmacro.h"

#include "memory.hpp"
#include "internal_memory.h"
#include "comm_commands.h"

#define USB_COMM

#ifdef USB_COMM
#include "tusb.h"
#endif

class VMM;

#ifdef SPI_COMM
class ExternalMemory {
    TaskHandle_t run_task;

    QueueHandle_t mem_requests;
    spi_inst_t* spi_hw = spi0;
    int dma_tx_chan;
    int dma_rx_chan;

    VMM *internal_memory;

    void __isr dma_handler();
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
};
#endif
#ifdef USB_COMM
class ExternalMemory {
    TaskHandle_t run_task;

    QueueHandle_t mem_requests;
    spi_inst_t* spi_hw = spi0;
    int dma_tx_chan;
    int dma_rx_chan;

    MemoryRequest active_req;

    VMM *internal_memory;

    void receive(uint8_t itf);
    void transmit(uint8_t itf, uint32_t sent_bytes);
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

    void notify_completion();
};
#endif

#endif  // EXTERNAL_MEMORY_H