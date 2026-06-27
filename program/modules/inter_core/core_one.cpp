#include "inter_core.h"

#include "mpu_config.h"
#include "debug_led.h"
#include "pal.h"
#include "client_map.h"

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/exception.h"
#include "hardware/structs/scb.h"

volatile MpuRegionConfig pending_mpu_region_config;
volatile InterCoreCommand pending_core_cmd;
volatile bool mpu_ack_flag = false;


void core1_setup()
{
    exception_set_exclusive_handler(MEMMANAGE_EXCEPTION, isr_memfault);
    irq_set_priority(MEMMANAGE_EXCEPTION, 0);

    // Setup the mpu
    configure_core1_static_regions();
    configure_rp2350_core1_mpu();

    // Setup core 1's fifo isr
    uint irq_num = SIO_FIFO_IRQ_NUM(1);

    // Remove what stole my IRQ handler.
    irq_handler_t current_handler = irq_get_vtable_handler(irq_num);
    if (current_handler != __unhandled_user_irq) {
        irq_remove_handler(irq_num, current_handler);
    }

    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1), core1_fifo_isr);
    irq_set_priority(irq_num, 0x80);
    irq_set_enabled(SIO_FIFO_IRQ_NUM(1), true);
}

void core1_fifo_isr() {
    while (multicore_fifo_rvalid()) {
        uint32_t cmd_ptr = multicore_fifo_pop_blocking();
        InterCoreCommand* cmd = (InterCoreCommand*)cmd_ptr;
        if (cmd == nullptr) {
            return;
        }

        if (cmd->type == InterCoreCommandType::MpuSetRegion) {
            if (pending_mpu_region_config.region >= 4) {
                return;
            }
            set_addr(pending_mpu_region_config.region, pending_mpu_region_config.base_addr, pending_mpu_region_config.limit_addr, pending_mpu_region_config.access, pending_mpu_region_config.execute);
        } else if (cmd->type == InterCoreCommandType::MpuClearRegion) {
            if (pending_mpu_region_config.region >= 4) {
                return;
            }
            mpu_clear_region(pending_mpu_region_config.region);
        } else if (cmd->type == InterCoreCommandType::ClientStore) {
            // launch the store client isr
            scb_hw->icsr = (1 << CLIENT::STORE_IRQ_NUM);
        } else {
            return;
        }

        // Make sure the MPU updates have been completed
        __DSB();
        __ISB();

        if(cmd->type == InterCoreCommandType::MpuClearRegion || cmd->type == InterCoreCommandType::MpuSetRegion) {
            mpu_ack_flag = true;
        }

        // Make sure the volatile bool has been updated.
        __DMB();
    }
    multicore_fifo_clear_irq();
}