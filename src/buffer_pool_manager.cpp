#include "buffer_pool_manager.hpp"
#include <iostream>
#include <algorithm>

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    pool_.resize(pool_size_);
    // Initialize our free pool frames into the LRU tracker
    for (size_t i = 0; i < pool_size_; ++i) {
        lru_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    // Flush all modified (dirty) pages back to physical disk storage upon shutdown
    for (auto &frame : pool_) {
        if (frame.page_id != INVALID_PAGE_ID && frame.is_dirty) {
            disk_manager_->WritePage(frame.page_id, frame.data);
        }
    }
}

bool BufferPoolManager::FindVictim(frame_id_t *frame_id) {
    // Look through the LRU list to find an unpinned frame to evict from RAM
    for (auto it = lru_list_.begin(); it != lru_list_.end(); ++it) {
        if (pool_[*it].pin_count == 0) {
            *frame_id = *it;
            lru_list_.erase(it);
            return true;
        }
    }
    return false; // All pages are locked in RAM; cache cannot evict safely
}

char* BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(latch_);

    // Case 1: Page already exists inside our RAM pool cache
    if (page_table_.find(page_id) != page_table_.end()) {
        frame_id_t frame_id = page_table_[page_id];
        pool_[frame_id].pin_count++;
        // Remove from current position in LRU list to refresh its prominence
        lru_list_.remove(frame_id);
        lru_list_.push_back(frame_id);
        return pool_[frame_id].data;
    }

    // Case 2: Page is on disk, we must evict an old page from RAM to make room
    frame_id_t victim_frame = -1;
    if (!FindVictim(&victim_frame)) {
        return nullptr; // No frames can be safely evicted (Buffer Pool Full Error)
    }

    // If the old page was modified, write it back to disk before erasing it
    PageFrame &frame = pool_[victim_frame];
    if (frame.page_id != INVALID_PAGE_ID) {
        if (frame.is_dirty) {
            disk_manager_->WritePage(frame.page_id, frame.data);
        }
        page_table_.erase(frame.page_id);
    }

    // Read requested page directly from Linux drive into our freshly cleared RAM frame
    disk_manager_->ReadPage(page_id, frame.data);
    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.is_dirty = false;

    page_table_[page_id] = victim_frame;
    lru_list_.push_back(victim_frame);

    return frame.data;
}

void BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> guard(latch_);
    if (page_table_.find(page_id) == page_table_.end()) return;

    frame_id_t frame_id = page_table_[page_id];
    PageFrame &frame = pool_[frame_id];

    if (is_dirty) {
        frame.is_dirty = true;
    }

    if (frame.pin_count > 0) {
        frame.pin_count--;
    }
}

page_id_t BufferPoolManager::NewPage() {
    std::lock_guard<std::mutex> guard(latch_);

    frame_id_t victim_frame = -1;
    if (!FindVictim(&victim_frame)) {
        return INVALID_PAGE_ID;
    }

    PageFrame &frame = pool_[victim_frame];
    if (frame.page_id != INVALID_PAGE_ID) {
        if (frame.is_dirty) {
            disk_manager_->WritePage(frame.page_id, frame.data);
        }
        page_table_.erase(frame.page_id);
    }

    // Allocate a clean virtual page entry
    page_id_t new_page_id = disk_manager_->AllocatePage();
    
    frame.page_id = new_page_id;
    frame.pin_count = 1;
    frame.is_dirty = false;
    std::fill(frame.data, frame.data + PAGE_SIZE, 0);

    page_table_[new_page_id] = victim_frame;
    lru_list_.push_back(victim_frame);

    return new_page_id;
}
