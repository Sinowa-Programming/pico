#ifndef PAL_H
#define PAL_H

#include "internal_memory.h"
#include "external_memory.h"
// ====Global Variables====
extern ExternalMemory external_memory;
extern VMM vmm;    // This will need to be setup in main().
// ------------------------

template <typename T>
class VPtr {
private:
    uint32_t _v_addr; // The raw virtual address (0 to 40MB)

public:
    VPtr(uint32_t addr = 0) : _v_addr(addr) {}

    // Pointer Arithmetic
    VPtr operator+(uint32_t offset) const { return VPtr(vmm, _v_addr + (offset * sizeof(T))); }
    VPtr& operator++() { _v_addr += sizeof(T); return *this; }

    // De-referencing for Reading
    operator T() const {
        T value;
        uint8_t* dest = reinterpret_cast<uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            dest[i] = vmm.access(_v_addr + i, false);
        }
        return value;
    }

    // Assignment for Writing (VPtr<int> p; p = 5;)
    VPtr& operator=(const T& value) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            vmm.access(_v_addr + i, true) = src[i];
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


// Custom memset for Virtual Addresses
void vmemset(uint32_t dest_v_addr, int value, size_t count);

// Custom memcpy for Virtual Addresses
void vmemcpy(uint32_t dest_v_addr, uint32_t src_v_addr, size_t count);

#endif  // PAL_H