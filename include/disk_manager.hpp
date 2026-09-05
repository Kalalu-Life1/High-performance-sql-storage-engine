#ifndef DISK_MANAGER_HPP
#define DISK_MANAGER_HPP

#include <string>
#include <mutex>
#include <cstdint>

constexpr uint32_t PAGE_SIZE = 4096;
using page_id_t = int32_t;
constexpr page_id_t INVALID_PAGE_ID = -1;

class DiskManager {
public:
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    void WritePage(page_id_t page_id, const char *page_data);
    void ReadPage(page_id_t page_id, char *page_data);
    page_id_t AllocatePage();
    
    // Added to securely query the existing file block count
    page_id_t GetNumPages() { 
        std::lock_guard<std::mutex> guard(db_io_latch_);
        return num_pages_; 
    }

private:
    int fd_;
    page_id_t num_pages_;
    std::mutex db_io_latch_;
};

#endif
