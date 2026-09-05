#include "disk_manager.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

DiskManager::DiskManager(const std::string &db_file) : num_pages_(0) {
    fd_ = ::open(db_file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("CRITICAL: open failed: " + db_file);
    }

    off_t file_size = ::lseek(fd_, 0, SEEK_END);
    if (file_size < 0) {
        ::close(fd_);
        throw std::runtime_error("CRITICAL: lseek failed.");
    }
    num_pages_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        // Enforce an absolute metadata hardware flush before closing the descriptor
        ::fsync(fd_);
        ::close(fd_);
    }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;

    ssize_t bytes_written = ::pwrite(fd_, page_data, PAGE_SIZE, offset);
    if (bytes_written != PAGE_SIZE) {
        throw std::runtime_error("CRITICAL: Linux pwrite failed.");
    }

    if (::fsync(fd_) < 0) {
        throw std::runtime_error("CRITICAL: Linux fsync failed.");
    }
    
    // Explicitly update internal state counters if writing past old block boundaries
    if (page_id >= num_pages_) {
        num_pages_ = page_id + 1;
    }
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;

    off_t current_file_size = ::lseek(fd_, 0, SEEK_END);
    if (offset >= current_file_size) {
        throw std::runtime_error("CRITICAL: Out of bounds read requested.");
    }

    ssize_t bytes_read = ::pread(fd_, page_data, PAGE_SIZE, offset);
    if (bytes_read < 0) {
        throw std::runtime_error("CRITICAL: Linux pread failed.");
    }
    if (bytes_read < PAGE_SIZE) {
        std::fill(page_data + bytes_read, page_data + PAGE_SIZE, 0);
    }
}

page_id_t DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    return num_pages_++;
}
