#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class RigCtl {
public:
    RigCtl();
    ~RigCtl();

    void start();
    void stop();

    double get_frequency() const;
    std::string get_mode() const;

private:
    void thread_func();
    bool connect_to_rigctld();
    std::string send_command(const std::string& cmd);

    std::thread thread_;
    std::atomic<bool> is_running_;
    int socket_fd_;

    mutable std::mutex data_mutex_;
    double frequency_;
    std::string mode_;
};
