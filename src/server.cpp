#include "kv_engine.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sstream>
#include <vector>
#include <thread>
#include <queue>
#include <condition_variable>
#include <algorithm>
#include <functional>

constexpr int PORT = 8080;
constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 1024;
constexpr int THREAD_POOL_SIZE = 4; 

std::string ToUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

// Enterprise Concurrent Worker Thread Pool Subsystem
class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]() { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task(); 
                }
            });
        }
    }

    void Enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(task);
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

int SetNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void ProcessClientSQL(int client_socket, std::string request, KVEngine &engine) {
    while(!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
        request.pop_back();
    }
    if (request.empty()) return;

    std::string response;
    std::string request_upper = ToUpper(request);

    if (request_upper == "EXIT" || request_upper == "QUIT") {
        response = "BYE\n";
        ::send(client_socket, response.c_str(), response.length(), 0);
        ::close(client_socket);
        return;
    }
    else if (request_upper.find("INSERT INTO") == 0) {
        size_t val_pos = request.find("VALUES");
        if (val_pos == std::string::npos) {
            response = "SQL_ERROR: Missing VALUES clause\n";
        } else {
            size_t open_paren = request.find('(', val_pos);
            size_t close_paren = request.find(')', open_paren);
            if (open_paren == std::string::npos || close_paren == std::string::npos) {
                response = "SQL_ERROR: Invalid syntax near VALUES\n";
            } else {
                std::string args = request.substr(open_paren + 1, close_paren - open_paren - 1);
                std::stringstream arg_ss(args);
                std::string key, value;
                std::getline(arg_ss, key, ',');
                std::getline(arg_ss, value);
                
                // Enhanced robust sanitization utility to strip out SQL syntax tokens
                key.erase(std::remove(key.begin(), key.end(), '\''), key.end());
                key.erase(std::remove(key.begin(), key.end(), '\"'), key.end());
                key.erase(std::remove(key.begin(), key.end(), ')'), key.end());
                
                value.erase(std::remove(value.begin(), value.end(), '\''), value.end());
                value.erase(std::remove(value.begin(), value.end(), '\"'), value.end());
                value.erase(std::remove(value.begin(), value.end(), ')'), value.end());
                
                // Trim leading/trailing blank whitespace spaces securely
                key.erase(0, key.find_first_not_of(" \t"));
                if(key.find_last_not_of(" \t") != std::string::npos) {
                    key.erase(key.find_last_not_of(" \t") + 1);
                }
                
                value.erase(0, value.find_first_not_of(" \t"));
                if(value.find_last_not_of(" \t") != std::string::npos) {
                    value.erase(value.find_last_not_of(" \t") + 1);
                }

                if (engine.Put(key, value)) {
                    response = "SQL_EXECUTE: 1 row inserted successfully.\n";
                } else {
                    response = "SQL_ERROR: Insertion storage failure.\n";
                }
            }
        }
    } 
    else if (request_upper.find("SELECT") == 0) {
        size_t where_pos = request_upper.find("WHERE");
        size_t eq_pos = request.find('=', where_pos);
        if (where_pos == std::string::npos || eq_pos == std::string::npos) {
            response = "SQL_ERROR: Use SELECT value FROM table WHERE key = 'target'\n";
        } else {
            std::string target_key = request.substr(eq_pos + 1);
            
            target_key.erase(std::remove(target_key.begin(), target_key.end(), '\''), target_key.end());
            target_key.erase(std::remove(target_key.begin(), target_key.end(), '\"'), target_key.end());
            target_key.erase(std::remove(target_key.begin(), target_key.end(), ';'), target_key.end());
            
            target_key.erase(0, target_key.find_first_not_of(" \t"));
            if(target_key.find_last_not_of(" \t") != std::string::npos) {
                target_key.erase(target_key.find_last_not_of(" \t") + 1);
            }

            std::string out_val;
            if (engine.Get(target_key, out_val)) {
                response = "+---------------------------------------+\n| VALUE\t\t\t\t\t|\n+---------------------------------------+\n| " + out_val + "\t\t|\n+---------------------------------------+\n";
            } else {
                response = "SQL_EMPTY: 0 rows returned (Key Not Found)\n";
            }
        }
    } else {
        response = "SQL_ERROR: Unknown statement. Use INSERT INTO or SELECT.\n";
    }

    ::send(client_socket, response.c_str(), response.length(), 0);
}

int main() {
    std::string db_file = "production_network.db";
    std::string wal_file = "production_wal.log";
    
    std::cout << "🚀 Starting High-Concurrency epoll Network Server Framework..." << std::endl;
    KVEngine engine(db_file, wal_file, 10);
    ThreadPool pool(THREAD_POOL_SIZE);

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (::bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) return 1;
    if (::listen(server_fd, 128) < 0) return 1;
    SetNonBlocking(server_fd);

    int epoll_fd = ::epoll_create1(0);
    if (epoll_fd < 0) return 1;

    epoll_event ev{}, events[MAX_EVENTS];
    ev.events = EPOLLIN | EPOLLET; 
    ev.data.fd = server_fd;
    
    if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) return 1;

    std::cout << "📡 Asynchronous epoll Matrix handling traffic on port -> " << PORT << std::endl;

    while (true) {
        int nfds = ::epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                while (true) {
                    sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_socket = ::accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if (client_socket < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
                        break;
                    }
                    SetNonBlocking(client_socket);
                    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; 
                    ev.data.fd = client_socket;
                    ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev);
                }
            } else {
                int client_fd = events[i].data.fd;
                char buffer[BUFFER_SIZE];
                std::memset(buffer, 0, BUFFER_SIZE);
                
                ssize_t bytes_read = ::recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes_read <= 0) {
                    ::close(client_fd);
                } else {
                    std::string req(buffer);
                    pool.Enqueue([client_fd, req, &engine, epoll_fd]() {
                        ProcessClientSQL(client_fd, req, engine);
                        
                        epoll_event ev_rearm{};
                        ev_rearm.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                        ev_rearm.data.fd = client_fd;
                        ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_rearm);
                    });
                }
            }
        }
    }

    ::close(server_fd);
    return 0;
}
