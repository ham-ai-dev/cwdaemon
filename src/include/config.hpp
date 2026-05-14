#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <toml++/toml.hpp>

struct Config {
    std::string audio_device = "default";
    int sample_rate = 8000;
    int buffer_size = 512;
    int tone_frequency = 700;
    int agc_decay = 10;
    int wpm_min = 2;
    int wpm_max = 50;
    bool afc_enabled = true;
    int api_port = 8080;
    std::string log_level = "info";

    // rigctl
    bool rigctl_enabled = false;
    std::string rigctl_host = "127.0.0.1";
    int rigctl_port = 4532;
    int rigctl_poll_interval_ms = 1000;
};

class ConfigManager {
public:
    static ConfigManager& instance();

    void load(const std::string& path);
    void save();
    
    Config get_config();
    void update_config(const Config& new_config);

private:
    ConfigManager() = default;
    Config config_;
    std::string config_path_;
    std::mutex mutex_;
};
