#ifndef WAL_MANAGER_HPP
#define WAL_MANAGER_HPP

#include <string>
#include <mutex>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>

class WALManager {
public:
    explicit WALManager(const std::string &wal_file) {
        // Open the WAL log in append-only mode, create it if missing
        fd_ = ::open(wal_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("CRITICAL: Failed to open WAL file descriptor.");
        }
    }

    ~WALManager() {
        if (fd_ >= 0) {
            ::fsync(fd_);
            ::close(fd_);
        }
    }

    // Appends a transactional modification log entry safely to the hard drive
    void AppendRecord(const std::string &key, const std::string &value) {
        std::lock_guard<std::mutex> guard(latch_);
        
        // Protocol Serialization Format: "PUT key value\n"
        std::string log_entry = "PUT " + key + " " + value + "\n";
        
        // Native atomic append via the Linux kernel
        ssize_t bytes_written = ::write(fd_, log_entry.c_str(), log_entry.length());
        if (bytes_written != static_cast<ssize_t>(log_entry.length())) {
            throw std::runtime_error("CRITICAL: Partial or failed WAL block write.");
        }

        // Force hardware synchronization checkpoint to guarantee absolute ACID compliance
        if (::fsync(fd_) < 0) {
            throw std::runtime_error("CRITICAL: WAL hardware sync flush failed.");
        }
    }

private:
    int fd_;
    std::mutex latch_;
};

#endif
