#ifndef STATIC_LINKED_LIST_H
#define STATIC_LINKED_LIST_H

#include <pico/stdio.h>

// NOTE: The implementation is in the cpp file and will be copied for each different count init.
// Binary Tree that stores the addresses of objects
template <uint32_t max_size>
class StaticAddressMap {
    struct AddressMap {
        uint32_t original_address;
        uint32_t adjusted_address;
        AddressMap *left;
        AddressMap *right;
        AddressMap *parent;
        uint32_t idx = -1;               // Position of the node in static_container_arr
    };

    uint32_t head;

    AddressMap static_container_arr[max_size];
    uint32_t element_cnt;
    public:
        StaticAddressMap() {
            element_cnt = 0;
        }

        /// @brief Adds the given entry to the AddressMap
        /// @param original_address The original address
        /// @param adjusted_address The adjusted address
        /// @return A flag if the addition was successful
        bool add_entry(uint32_t original_address, uint32_t adjusted_address);

        /// @brief Searches the linked list and returns the index of the object with the given address.
        /// @param adjusted_address The adjusted address to search for
        /// @return The index of the node that has the adjusted address
        uint32_t get_index_from_adjusted_address(uint32_t adjusted_address);

        /// @brief Returns the original address given the index
        /// @param idx The idx received from get_index_from_adjusted_address.
        /// @return The original address associated with the idx
        uint32_t get_original_address_from_index(uint32_t idx);

        /// @brief Deletes the node using the given index.
        /// @param index The index of the node to remove
        void remove_by_index(uint32_t idx);
};

template <uint32_t max_size>
bool StaticAddressMap<max_size>::add_entry(uint32_t original_address, uint32_t adjusted_address)
{
    if(element_cnt == 0) {
        static_container_arr[0] = {
            .original_address = original_address,
            .adjusted_address = adjusted_address,
            .left = nullptr,
            .right = nullptr,
            .idx = 0
        };
        head = 0;
        
        ++element_cnt;
        return true;
    } else if (element_cnt == max_size) {
        return false;
    }
    
    AddressMap *cur_node = &static_container_arr[head];
    AddressMap *parent;
    while(cur_node) {
        parent = cur_node;
        if(adjusted_address < cur_node->adjusted_address) {
            cur_node = cur_node->left;
        } else if(adjusted_address > cur_node->adjusted_address) {
            cur_node = cur_node->right;
        } else {
            cur_node->adjusted_address = adjusted_address;
        }
    }

    // Find an open index to store the new entry at.
    for(uint32_t i=0; i < max_size; ++i) {
        if(static_container_arr[i].idx == -1) {
            // Create the new entry
            static_container_arr[i] = {
                .original_address = original_address,
                .adjusted_address = adjusted_address,
                .left = nullptr,
                .right = nullptr,
                .parent = parent,
                .idx = i
            };

            // Assign the node
            if(original_address < parent->adjusted_address) {
                parent->left = &static_container_arr[i];
            } else {
                parent->right = &static_container_arr[i];
            }
            break;
        }
    }

    ++element_cnt;
    return true;
}

template <uint32_t max_size>
uint32_t StaticAddressMap<max_size>::get_index_from_adjusted_address(uint32_t adjusted_address)
{
    AddressMap *cur_node = &static_container_arr[head];
    while(cur_node) {
        if(cur_node->adjusted_address == adjusted_address) {
            return cur_node->idx;
        }

        if(adjusted_address < cur_node->adjusted_address) {
            cur_node = cur_node->left;
        } else {
            cur_node = cur_node->right;
        }
    }
    return 0xFFFFFFFF;
}

template <uint32_t max_size>
uint32_t StaticAddressMap<max_size>::get_original_address_from_index(uint32_t idx)
{
    return static_container_arr[idx].original_address;
}

template <uint32_t max_size>
void StaticAddressMap<max_size>::remove_by_index(uint32_t idx)
{
    AddressMap *cur_node = &static_container_arr[idx];
    if(cur_node->left && cur_node->right) {
        // Smallest value of the right subtree
        AddressMap *successor = cur_node->right;
        while(successor->left) {
            successor = successor->left;
        }

        cur_node->adjusted_address = successor->adjusted_address;
        cur_node->original_address = successor->original_address;

        cur_node = successor;
    }
    
    // Chose a child that exist(left has priority) and assign it's parent to cur_node
    AddressMap *child = cur_node->left ? cur_node->left : cur_node->right;
    if(child) {
        child->parent = cur_node->parent;
    }

    if(!(cur_node->parent)) {   // deleting the root node
        if(child) {
            head = child->idx;
        }
    } else if(cur_node == cur_node->parent->left) {
        cur_node->parent->left = child;
    } else {
        cur_node->parent->right = child;
    }

    cur_node->idx = -1; // Mark the node deleted
    --element_cnt;
}


#endif // STATIC_LINKED_LIST_H