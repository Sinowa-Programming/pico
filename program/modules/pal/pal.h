#include "internal_memory.h"

// ====Global Variables====
VMM vmm;    // This will need to be setup in main().
// ------------------------

template <typename T>
class VPtr {
private:
    uint32_t _v_addr; // The raw virtual address (0 to 40MB)

public:
    VPtr(uint32_t addr = 0) : _vmm(vmm), _v_addr(addr) {}

    // Pointer Arithmetic
    VPtr operator+(uint32_t offset) const { return VPtr(_vmm, _v_addr + (offset * sizeof(T))); }
    VPtr& operator++() { _v_addr += sizeof(T); return *this; }

    // De-referencing for Reading
    operator T() const {
        T value;
        uint8_t* dest = reinterpret_cast<uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            dest[i] = _vmm.access(_v_addr + i, false);
        }
        return value;
    }

    // Assignment for Writing (VPtr<int> p; p = 5;)
    VPtr& operator=(const T& value) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            _vmm.access(_v_addr + i, true) = src[i];
        }
        return *this;
    }

    // Array-style access
    VPtr operator[](uint32_t index) const {
        return *this + index;
    }

    // Access raw virtual address
    uint32_t addr() const { return _v_addr; }
};


void vmalloc() {
    
}

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
        vmm.access(dest_v_addr, true);
        vmm.access(src_v_addr, false);

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