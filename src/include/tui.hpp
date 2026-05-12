#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

class Tui {
public:
    Tui();
    void run();

    static void add_decoded_char(char c);
    static void update_metrics(float wpm, float snr,
                               int detected_freq = 0, float afc_confidence = 0.0f);

    // Callback for live parameter changes (tone freq, device, etc.)
    using ConfigChangeCallback = std::function<void(const std::string& key, const std::string& value)>;
    static void set_config_change_callback(ConfigChangeCallback cb);

private:
    ftxui::ScreenInteractive screen_;
    
    // Global state for rendering
    static std::string decoded_text_;
    static float current_wpm_;
    static float current_snr_;
    static int detected_freq_;
    static float afc_confidence_;
    static std::mutex tui_mutex_;
    static ConfigChangeCallback config_change_cb_;
};
