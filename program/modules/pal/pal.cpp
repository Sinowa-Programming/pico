#include "pal.h"

// Custom memset for Virtual Addresses
void vmemset(uint32_t dest_v_addr, int value, size_t count) {
    while (count > 0) {
        // 1. Ensure the page is resident and marked as dirty
        // Calling access() handles the Page Fault logic and LRU updates
        vmm.access(dest_v_addr, true);

        uint32_t offset = dest_v_addr % PAGE_SIZE;

        // 2. Calculate the remaining space in the current physical page frame
        size_t bytes_to_end_of_page = PAGE_SIZE - offset;
        size_t chunk = std::min(count, bytes_to_end_of_page);

        // 3. Get the direct physical pointer to the SRAM frame
        uint8_t* p_dest = vmm.get_physical_ptr(dest_v_addr);

        // 4. Use optimized CPU memset
        // On RP2350, this will use word-aligned stores if possible
        memset(p_dest, value, chunk);

        // 5. Advance pointers
        dest_v_addr += chunk;
        count -= chunk;
    }
}

// Custom memcpy for Virtual Addresses
void vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count) {
    while (count > 0) {
        // 1. Force the pages into RAM via access() (just for the first byte of each page)
        // This triggers the page fault logic if necessary.
        vmm.access(dest_v_addr);
        vmm.access(src_v_addr);

        uint32_t dest_page = dest_v_addr / PAGE_SIZE;
        uint32_t src_page = src_v_addr / PAGE_SIZE;
        uint32_t dest_off = dest_v_addr % PAGE_SIZE;
        uint32_t src_off = src_v_addr % PAGE_SIZE;

        // 2. Calculate how much we can copy within the current page boundaries
        size_t dest_rem = PAGE_SIZE - dest_off;
        size_t src_rem = PAGE_SIZE - src_off;
        size_t chunk = std::min({count, dest_rem, src_rem});

        // 3. Get physical pointers and use optimized CPU memcpy
        uint8_t* p_dest = vmm.get_physical_ptr(dest_v_addr);
        uint8_t* p_src = vmm.get_physical_ptr(src_v_addr);

        // ARM Cortex-M33 hardware-optimized memcpy (handles word alignment)
        memcpy(p_dest, p_src, chunk);

        dest_v_addr += chunk;
        src_v_addr += chunk;
        count -= chunk;
    }
}

void *vcalloc(size_t num, size_t size)
{
    size_t alloc_pages_needed = size / PAGE_SIZE;
    uint8_t frame_idx;
    
    for(size_t pages_alloced = 0; pages_alloced < alloc_pages_needed; ++pages_alloced){
        frame_idx = vmm.alloc();
        
        

    
    }
    return nullptr;
}
