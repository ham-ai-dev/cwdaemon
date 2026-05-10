#pragma once

#include <string>
#include <atomic>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

class Tui {
public:
    Tui();
    void run();

    static void add_decoded_char(char c);
    static void update_metrics(float wpm, float snr);

private:
    ftxui::ScreenInteractive screen_;
    
    // Global state for rendering
    static std::string decoded_text_;
    static float current_wpm_;
    static float current_snr_;
    static std::mutex tui_mutex_;
};
