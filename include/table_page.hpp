#ifndef TABLE_PAGE_HPP
#define TABLE_PAGE_HPP

#include "disk_manager.hpp"
#include <cstdint>
#include <cstring>

class TablePage {
public:
    static void InitializePage(char *page_data) {
        uint32_t zero = 0;
        uint32_t initial_free_space = PAGE_SIZE;
        std::memcpy(page_data, &zero, sizeof(uint32_t)); 
        std::memcpy(page_data + sizeof(uint32_t), &initial_free_space, sizeof(uint32_t)); 
    }

    static bool InsertRecord(char *page_data, const char *key, const char *value, uint32_t &out_slot_id) {
        uint32_t slot_count;
        uint32_t free_space_ptr;
        std::memcpy(&slot_count, page_data, sizeof(uint32_t));
        std::memcpy(&free_space_ptr, page_data + sizeof(uint32_t), sizeof(uint32_t));

        uint32_t key_len = std::strlen(key) + 1;
        uint32_t val_len = std::strlen(value) + 1;
        uint32_t total_record_size = key_len + val_len;

        uint32_t required_space = total_record_size + sizeof(uint32_t) * 2;
        uint32_t current_directory_end = sizeof(uint32_t) * 2 + slot_count * (sizeof(uint32_t) * 2);

        if (free_space_ptr < current_directory_end || (free_space_ptr - current_directory_end) < required_space) {
            return false; 
        }

        free_space_ptr -= total_record_size;

        std::memcpy(page_data + free_space_ptr, key, key_len);
        std::memcpy(page_data + free_space_ptr + key_len, value, val_len);

        uint32_t slot_offset_pos = sizeof(uint32_t) * 2 + slot_count * (sizeof(uint32_t) * 2);
        std::memcpy(page_data + slot_offset_pos, &free_space_ptr, sizeof(uint32_t));
        std::memcpy(page_data + slot_offset_pos + sizeof(uint32_t), &total_record_size, sizeof(uint32_t));

        out_slot_id = slot_count;
        slot_count++;
        std::memcpy(page_data, &slot_count, sizeof(uint32_t));
        std::memcpy(page_data + sizeof(uint32_t), &free_space_ptr, sizeof(uint32_t));

        return true;
    }

    static bool GetRecord(const char *page_data, uint32_t slot_id, char *out_key, char *out_value) {
        uint32_t slot_count;
        std::memcpy(&slot_count, page_data, sizeof(uint32_t));
        if (slot_id >= slot_count) return false;

        uint32_t slot_offset_pos = sizeof(uint32_t) * 2 + slot_id * (sizeof(uint32_t) * 2);
        uint32_t record_offset;
        uint32_t record_len;
        std::memcpy(&record_offset, page_data + slot_offset_pos, sizeof(uint32_t));
        std::memcpy(&record_len, page_data + slot_offset_pos + sizeof(uint32_t), sizeof(uint32_t));

        const char* key_ptr = page_data + record_offset;
        uint32_t key_len = std::strlen(key_ptr) + 1;
        std::strcpy(out_key, key_ptr);
        std::strcpy(out_value, key_ptr + key_len);

        return true;
    }
};

#endif
