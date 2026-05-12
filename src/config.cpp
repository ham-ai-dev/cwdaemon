#include "config.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdio>

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_path_ = path;

    if (!std::filesystem::exists(path)) {
        save(); // create default
        return;
    }

    try {
        auto tbl = toml::parse_file(path);
        config_.audio_device = tbl["audio"]["device"].value_or("default");
        config_.sample_rate = tbl["audio"]["sample_rate"].value_or(8000);
        config_.buffer_size = tbl["audio"]["buffer_size"].value_or(512);

        config_.tone_frequency = tbl["dsp"]["tone_frequency"].value_or(600);
        config_.agc_decay = tbl["dsp"]["agc_decay"].value_or(10);
        config_.wpm_min = tbl["dsp"]["wpm_min"].value_or(5);
        config_.wpm_max = tbl["dsp"]["wpm_max"].value_or(50);
        config_.afc_enabled = tbl["dsp"]["afc_enabled"].value_or(true);

        config_.api_port = tbl["api"]["port"].value_or(8080);
        config_.log_level = tbl["api"]["log_level"].value_or("info");

        config_.rigctl_enabled = tbl["rigctl"]["enabled"].value_or(false);
        config_.rigctl_host = tbl["rigctl"]["host"].value_or("127.0.0.1");
        config_.rigctl_port = tbl["rigctl"]["port"].value_or(4532);
        config_.rigctl_poll_interval_ms = tbl["rigctl"]["poll_interval_ms"].value_or(1000);
    } catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed: " << err << "\n";
    }
}

void ConfigManager::save() {
    auto tbl = toml::table{
        { "audio", toml::table{
            { "device", config_.audio_device },
            { "sample_rate", config_.sample_rate },
            { "buffer_size", config_.buffer_size }
        }},
        { "dsp", toml::table{
            { "tone_frequency", config_.tone_frequency },
            { "agc_decay", config_.agc_decay },
            { "wpm_min", config_.wpm_min },
            { "wpm_max", config_.wpm_max },
            { "afc_enabled", config_.afc_enabled }
        }},
        { "api", toml::table{
            { "port", config_.api_port },
            { "log_level", config_.log_level }
        }},
        { "rigctl", toml::table{
            { "enabled", config_.rigctl_enabled },
            { "host", config_.rigctl_host },
            { "port", config_.rigctl_port },
            { "poll_interval_ms", config_.rigctl_poll_interval_ms }
        }}
    };

    std::string tmp_path = config_path_ + ".tmp";
    {
        std::ofstream out(tmp_path);
        out << tbl;
    }
    std::rename(tmp_path.c_str(), config_path_.c_str());
}

Config ConfigManager::get_config() {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void ConfigManager::update_config(const Config& new_config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = new_config;
    save();
}
