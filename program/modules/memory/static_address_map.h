#ifndef STATIC_LINKED_LIST_H
#define STATIC_LINKED_LIST_H

#include <pico/stdio.h>

// NOTE: The implementation is in the cpp file and will be copied for each different count init.
// Binary Tree that stores the addresses of objects
template <uint32_t max_size>
class StaticAddressMap {
    AddressMap static_container_arr[max_size];
    uint32_t element_cnt = 0;
    public:
        struct AddressMap {
            uint32_t original_address;
            uint32_t adjusted_address;
        };

        StaticAddressMap() {}

        AddressMap *get_address_map_container() { return static_container_arr; };
        uint32_t get_element_count() { return element_cnt; };

        /// @brief Adds the given entry to the AddressMap
        /// @param original_address The original address
        /// @param adjusted_address The adjusted address
        /// @return A flag if the addition was successful
        bool add_entry(uint32_t original_address, uint32_t adjusted_address);

        /// @brief Returns the index of the entry in the static_container_arr holding the adjusted address
        /// @param adjusted_address The address to search for
        /// @return index of the entry in the static_container_arr holding the adjusted address. Returns 0xFFFFFFFF if no entry.
        uint32_t get_index_from_adjusted_address(uint32_t adjusted_address);


        /// @brief Array access for static_container_arr
        /// @param idx Index to access
        /// @return Original address if valid. Else, it returns 0.
        uint32_t get_original_address_from_index(uint32_t idx);

        /// @brief Remove the address mapping given the index
        /// @param idx_to_rem The index to remove. Given by get_idx_from_adjusted
        void remove_by_idx(uint32_t idx_to_rem);

        /// @brief Remove the address mapping given the original address
        /// @param original_address 
        void remove_by_original_address(uint32_t original_address);

        /// @brief Remove the address mapping given the adjusted address
        /// @param adjusted_address 
        void remove_by_adjusted_address(uint32_t adjusted_address);
};

template <uint32_t max_size>
bool StaticAddressMap<max_size>::add_entry(uint32_t original_address, uint32_t adjusted_address)
{
    if (element_cnt == max_size) {
        return false;
    }
    
    for(uint32_t i=0; i < element_cnt; ++i) {
        if(static_container_arr[i].original_address == original_address) {
            static_container_arr[i].adjusted_address = adjusted_address;
            return true;
        }
    }

    static_container_arr[element_cnt].adjusted_address = adjusted_address;
    static_container_arr[element_cnt].original_address = original_address;

    ++element_cnt;
    return true;
}

template <uint32_t max_size>
uint32_t StaticAddressMap<max_size>::get_index_from_adjusted_address(uint32_t adjusted_address) {
    for(uint32_t i=0; i < element_cnt; ++i) {
        if(static_container_arr[i].adjusted_address == adjusted_address) {
            return i;
        }
    }

    return 0xFFFFFFFF;
}

template <uint32_t max_size>
uint32_t StaticAddressMap<max_size>::get_original_address_from_index(uint32_t idx) {
    if(idx < element_cnt) {
        return static_container_arr[idx].original_address;
    }
    return 0;
}

template <uint32_t max_size>
void StaticAddressMap<max_size>::remove_by_idx(uint32_t idx_to_rem) {
    static_container_arr[idx_to_rem] = static_container_arr[element_cnt - 1];
    --element_cnt;
}

template <uint32_t max_size>
void StaticAddressMap<max_size>::remove_by_original_address(uint32_t original_address) {
    for(uint32_t i=0; i < element_cnt; ++i) {
        if(static_container_arr[i].original_address == original_address) {
            remove_by_idx(i);
        }
    }
}

template <uint32_t max_size>
void StaticAddressMap<max_size>::remove_by_adjusted_address(uint32_t adjusted_address) {
    for(uint32_t i=0; i < element_cnt; ++i) {
        if(static_container_arr[i].adjusted_address == adjusted_address) {
            remove_by_idx(i);
        }
    }
}


#endif // STATIC_LINKED_LIST_H