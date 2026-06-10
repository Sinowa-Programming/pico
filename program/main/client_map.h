#ifndef CLIENT_MAP_H
#define CLIENT_MAP_H

#include <cstdint>

namespace CLIENT {
    typedef void (*main_func_t)(void);
    extern volatile bool task_enabled;

    void setup_client_task();

    void start_client_task();   // Starts the loaded program.
    void load_frame(uintptr_t physical_addr);

    void client_task();    // The task that is running on the PAL
}
#endif // CLIENT_MAP_H