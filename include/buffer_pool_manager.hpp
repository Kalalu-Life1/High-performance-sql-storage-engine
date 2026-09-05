#ifndef BUFFER_POOL_MANAGER_HPP
#define BUFFER_POOL_MANAGER_HPP

#include "disk_manager.hpp"
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>

using frame_id_t = int32_t;

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager);
    ~BufferPoolManager();

    // Core Cache Engine API
    char* FetchPage(page_id_t page_id);
    void UnpinPage(page_id_t page_id, bool is_dirty);
    page_id_t NewPage();

private:
    struct PageFrame {
        page_id_t page_id = INVALID_PAGE_ID;
        int pin_count = 0;
        bool is_dirty = false;
        char data[PAGE_SIZE] = {0};
    };

    size_t pool_size_;
    DiskManager *disk_manager_;
    std::vector<PageFrame> pool_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::list<frame_id_t> lru_list_; // LRU tracking structure
    std::mutex latch_;

    bool FindVictim(frame_id_t *frame_id);
};

#endif
