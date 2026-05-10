#include "rigctl.hpp"
#include "config.hpp"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

RigCtl::RigCtl() : is_running_(false), socket_fd_(-1), frequency_(0.0), mode_("UNKNOWN") {}

RigCtl::~RigCtl() {
    stop();
}

void RigCtl::start() {
    if (is_running_) return;
    is_running_ = true;
    thread_ = std::thread(&RigCtl::thread_func, this);
}

void RigCtl::stop() {
    if (!is_running_) return;
    is_running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

double RigCtl::get_frequency() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return frequency_;
}

std::string RigCtl::get_mode() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return mode_;
}

bool RigCtl::connect_to_rigctld() {
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }

    auto config = ConfigManager::instance().get_config();

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == -1) return false;

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config.rigctl_port);
    if (inet_pton(AF_INET, config.rigctl_host.c_str(), &server_addr.sin_addr) <= 0) {
        return false;
    }

    if (connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    return true;
}

std::string RigCtl::send_command(const std::string& cmd) {
    if (socket_fd_ == -1) return "";

    std::string full_cmd = cmd + "\n";
    if (send(socket_fd_, full_cmd.c_str(), full_cmd.length(), 0) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return "";
    }

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return "";
    }

    std::string resp(buffer);
    if (!resp.empty() && resp.back() == '\n') resp.pop_back();
    return resp;
}

void RigCtl::thread_func() {
    while (is_running_) {
        auto config = ConfigManager::instance().get_config();
        
        if (config.rigctl_enabled) {
            if (socket_fd_ == -1) {
                if (!connect_to_rigctld()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                    continue;
                }
            }

            std::string freq_str = send_command("f");
            std::string mode_str = send_command("m");

            if (!freq_str.empty()) {
                std::lock_guard<std::mutex> lock(data_mutex_);
                try {
                    frequency_ = std::stod(freq_str);
                } catch (...) { }
                
                if (!mode_str.empty()) {
                    size_t pos = mode_str.find('\n');
                    if (pos != std::string::npos) {
                        mode_ = mode_str.substr(0, pos);
                    } else {
                        mode_ = mode_str;
                    }
                }
            }
        } else {
            if (socket_fd_ != -1) {
                close(socket_fd_);
                socket_fd_ = -1;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config.rigctl_poll_interval_ms));
    }
}
