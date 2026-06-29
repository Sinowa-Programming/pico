#ifndef CLIENT_MAP_H
#define CLIENT_MAP_H

#include <cstdint>
#include "mpu_config.h"
#include "internal_memory.h"
#include "virtual_file.h"

extern VFM vfm;
extern ExternalMemory external_memory;

namespace CLIENT {
    uint8_t process_id;

    typedef void (*main_func_t)(void);
    extern volatile bool task_enabled;

    void setup_client_task();

    void start_client_task();   // Starts the loaded program.
    void load_frame(uintptr_t physical_addr);

    void client_task();    // The task that is running on the PAL

    // === Client program pausing ===
    extern volatile bool client_paused;
    const uint16_t PAUSE_IRQ_NUM    = 48;
    void pause_client_isr();

    // === Client program storing and loading ===
    extern volatile uintptr_t load_data_buffer;
    extern volatile bool pause_on_client_store;   // Wait in the isr after everything is store( halting the program )
    extern volatile bool external_mem_notify_completion;
    const uint16_t STORE_IRQ_NUM    = 46;
    const uint16_t LOAD_IRQ_NUM     = 47;

    /// @brief There are two sections to a client's PCB(process control block). The static section,
    /// which is the variables with a fixed size, and the dynamic section, which holds all of the large arrays that
    /// have variable sizes.
    ///
    /// ISRs used(fired by writing their IRQ # to NVIC_ICPR1):
    ///     Storing: SPAREIRQ_IRQ_0 - 46
    ///     Loading: SPAREIRQ_IRQ_1 - 47
    ///     Pausing: SPAREIRQ_IRQ_2 - 48
    ///
    /// The static section lists the sizes of the dynamic sections.
    ///     fpu_active    - If active, the SoftwareFpuFrame and CpuFpuHardwareFrame are also transmitted
    ///     addr_map_size - The amount of VMM::address_map entries that are transmitted.
    ///     open_file_cnt - The amount of VFM::file_data entries that are transmitted.
    typedef struct {
        uint8_t process_id;

        // CPU registers
        CpuSoftwareFrame cpu_soft_regs;
        CpuHardwareFrame cpu_hard_regs;

        uint16_t active_pages[3];   // Look at mpu_enabled in VMM

        // --- Dynamic section ---
        bool fpu_active;            // If the FPU was being used when the store was requested
        uint32_t addr_map_size;     // The amount of mapped addresses
        uint32_t open_file_cnt;     // The amount of open files
    } ClientPCBStatic;  // 8+32+32+6+4+4=86 bytes

    typedef struct {
        SoftwareFpuFrame fpu_soft_regs;
        CpuFpuHardwareFrame fpu_hard_regs;
    } ClientPCBFPU;

    extern volatile ClientPCBStatic client_pcb_snapshot;

    // Dynamic snapshot objects(May not be used during PCB creation or have variable sizes.)
    extern volatile ClientPCBFPU client_pcb_fpu_snapshot;
    extern volatile StaticAddressMap<VMM::ADJUSTED_ADDRESS_LIMIT>::AddressMap client_address_map_snapshot[VMM::ADJUSTED_ADDRESS_LIMIT];
    extern volatile VirtualFile client_virtual_file_snapshot[MAX_VIRTUAL_FILES];

    /// @brief Setups the ISRs for the load and store client.
    void client_isr_setup();

    // /// @brief Launches the store_client_isr.
    // /// @param pause If pause is set, the isr will spinlock and _wfe() until pause is unset
    // void store_client(bool pause = false);

    /// @brief To get the register state, an isr is needed to push all of the registers to the stack.
    void store_client_isr();

    /// @brief Loads the given software and hardware frames in the stack. Starts executing immediately once
    /// registers are loaded.
    void load_client_isr();
}
#endif // CLIENT_MAP_H