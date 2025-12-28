#include "external_memory.h"

void __isr ExternalMemory::dma_handler() {
    dma_irqn_acknowledge_channel(0, dma_rx_chan);   // The 0 stands for core 0( the 0th hardware status register )

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Notify the task that DMA is done
    vTaskNotifyGiveFromISR(run_task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void ExternalMemory::setup_dma() {
    dma_tx_chan = dma_claim_unused_channel(true);
    dma_rx_chan = dma_claim_unused_channel(true);

    // Configure TX channel (SRAM -> SPI)
    dma_channel_config tx_c = dma_channel_get_default_config(dma_tx_chan);
    channel_config_set_transfer_data_size(&tx_c, DMA_SIZE_8);
    channel_config_set_dreq(&tx_c, spi_get_dreq(spi_hw, true));
    dma_channel_set_config(dma_tx_chan, &tx_c, false);

    // Configure RX channel (SPI -> SRAM)
    dma_channel_config rx_c = dma_channel_get_default_config(dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_c, DMA_SIZE_8);
    channel_config_set_write_increment(&rx_c, true);
    channel_config_set_dreq(&rx_c, spi_get_dreq(spi_hw, false));
    dma_channel_set_config(dma_rx_chan, &rx_c, false);
}

void ExternalMemory::run() {
    MemoryRequest req;
    while (true) {
        // Wait for a request from the VMM
        if (xQueueReceive(mem_requests, &req, portMAX_DELAY)) {
            if (req.op == MemoryOp::READ) {
                // Trigger DMA RX
                dma_channel_transfer_to_buffer_now(dma_rx_chan, req.sram_buffer, PAGE_SIZE);
                // Must also send "dummy" bytes to clock the SPI
                static uint8_t dummy = 0xFF;
                dma_channel_transfer_from_buffer_now(dma_tx_chan, &dummy, PAGE_SIZE);
            } else {
                // Trigger DMA TX
                dma_channel_transfer_from_buffer_now(dma_tx_chan, req.sram_buffer, PAGE_SIZE);
            }

            // Wait for ISR to reawaken task
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            // 3. Notify the VMM or requester task that the data is ready
            internal_memory->notify_completion(req);
        }
    }
}

ExternalMemory::ExternalMemory(VMM *internal_memory, uint32_t queue_size = 10) {
    mem_requests = xQueueCreate(queue_size, sizeof(MemoryRequest));
    setup_dma();
}

void ExternalMemory::start() {
    xTaskCreate(task_entry, "SPI_DMA_VMM", 2048, this, configMAX_PRIORITIES - 1, &run_task);
}

void ExternalMemory::submit_request(MemoryRequest &req) {
    xQueueSend(mem_requests, req, portMAX_DELAY);
}