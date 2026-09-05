#ifndef KV_ENGINE_HPP
#define KV_ENGINE_HPP

#include "disk_manager.hpp"
#include "buffer_pool_manager.hpp"
#include "table_page.hpp"
#include "wal_manager.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>

class KVEngine {
public:
    KVEngine(const std::string &db_file, const std::string &wal_file, size_t buffer_pool_size) 
        : disk_manager_(db_file), bpm_(buffer_pool_size, &disk_manager_), 
          wal_manager_(wal_file), wal_file_path_(wal_file), active_page_id_(INVALID_PAGE_ID) {
        BootstrapIndex();
        ReplayWAL(); 
    }

    bool Put(const std::string &key, const std::string &value, bool log_to_wal = true) {
        std::lock_guard<std::recursive_mutex> guard(engine_latch_);

        if (log_to_wal) {
            wal_manager_.AppendRecord(key, value);
        }

        if (active_page_id_ == INVALID_PAGE_ID) {
            active_page_id_ = bpm_.NewPage();
            char *page_data = bpm_.FetchPage(active_page_id_);
            TablePage::InitializePage(page_data);
            bpm_.UnpinPage(active_page_id_, true);
        }

        char *page_data = bpm_.FetchPage(active_page_id_);
        uint32_t assigned_slot = 0;

        bool success = TablePage::InsertRecord(page_data, key.c_str(), value.c_str(), assigned_slot);
        
        if (!success) {
            bpm_.UnpinPage(active_page_id_, false);
            active_page_id_ = bpm_.NewPage();
            page_data = bpm_.FetchPage(active_page_id_);
            TablePage::InitializePage(page_data);
            success = TablePage::InsertRecord(page_data, key.c_str(), value.c_str(), assigned_slot);
        }

        if (success) {
            index_map_[key] = {active_page_id_, assigned_slot};
            bpm_.UnpinPage(active_page_id_, true);
            return true;
        }

        bpm_.UnpinPage(active_page_id_, false);
        return false;
    }

    bool Get(const std::string &key, std::string &out_value) {
        std::lock_guard<std::recursive_mutex> guard(engine_latch_);

        if (index_map_.find(key) == index_map_.end()) {
            return false;
        }

        auto location = index_map_[key];
        page_id_t pid = location.first;
        uint32_t slot_id = location.second;

        char *page_data = bpm_.FetchPage(pid);
        char key_buf[PAGE_SIZE] = {0};
        char val_buf[PAGE_SIZE] = {0};

        bool found = TablePage::GetRecord(page_data, slot_id, key_buf, val_buf);
        bpm_.UnpinPage(pid, false);

        if (found) {
            out_value = std::string(val_buf);
            return true;
        }
        return false;
    }

private:
    DiskManager disk_manager_;
    BufferPoolManager bpm_;
    WALManager wal_manager_;
    std::string wal_file_path_;
    page_id_t active_page_id_;
    std::recursive_mutex engine_latch_; 
    std::unordered_map<std::string, std::pair<page_id_t, uint32_t>> index_map_;

    void BootstrapIndex() {
        std::lock_guard<std::recursive_mutex> guard(engine_latch_);
        page_id_t total_allocated_pages = disk_manager_.GetNumPages();
        page_id_t total_pages = 0;
        
        while (total_pages < total_allocated_pages) {
            char *page_data = bpm_.FetchPage(total_pages);
            if (page_data == nullptr) break;

            uint32_t slot_count;
            std::memcpy(&slot_count, page_data, sizeof(uint32_t));

            for (uint32_t i = 0; i < slot_count; ++i) {
                char k_buf[PAGE_SIZE] = {0};
                char v_buf[PAGE_SIZE] = {0};
                if (TablePage::GetRecord(page_data, i, k_buf, v_buf)) {
                    index_map_[std::string(k_buf)] = {total_pages, i};
                }
            }

            bpm_.UnpinPage(total_pages, false);
            active_page_id_ = total_pages;
            total_pages++;
        }
    }

    void ReplayWAL() {
        std::lock_guard<std::recursive_mutex> guard(engine_latch_);
        std::ifstream infile(wal_file_path_);
        if (!infile.good()) return; 

        std::string line;
        while (std::getline(infile, line)) {
            // Trim whitespace and raw network control signals (\r and \n) safely
            while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string cmd, key, value;
            ss >> cmd >> key;
            std::getline(ss, value);
            
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            while(!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }

            if (cmd == "PUT") {
                this->Put(key, value, false); 
            }
        }
    }
};

#endif
