#include "tui.hpp"
#include "config.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component_options.hpp>

using namespace ftxui;

std::string Tui::decoded_text_ = "";
float Tui::current_wpm_ = 0.0f;
float Tui::current_snr_ = 0.0f;
std::mutex Tui::tui_mutex_;

Tui::Tui() : screen_(ScreenInteractive::Fullscreen()) {}

void Tui::add_decoded_char(char c) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    decoded_text_ += c;
    if (decoded_text_.length() > 500) {
        decoded_text_ = decoded_text_.substr(decoded_text_.length() - 500);
    }
}

void Tui::update_metrics(float wpm, float snr) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    current_wpm_ = wpm;
    current_snr_ = snr;
}

void Tui::run() {
    auto config = ConfigManager::instance().get_config();

    std::string tone_freq = std::to_string(config.tone_frequency);
    std::string api_port = std::to_string(config.api_port);

    auto freq_input = Input(&tone_freq, "Tone Freq");
    auto port_input = Input(&api_port, "API Port");

    auto save_btn = Button("Save Config", [&] {
        try {
            config.tone_frequency = std::stoi(tone_freq);
            config.api_port = std::stoi(api_port);
            ConfigManager::instance().update_config(config);
        } catch (...) {}
    });

    auto config_editor = Container::Vertical({
        freq_input,
        port_input,
        save_btn
    });

    auto renderer = Renderer(config_editor, [&] {
        std::lock_guard<std::mutex> lock(tui_mutex_);

        auto text_panel = window(text(" Decoded Text "), paragraph(decoded_text_)) | flex;
        
        auto meter = gauge(current_snr_ / 100.0f) | color(Color::Green);
        auto snr_panel = window(text(" Signal SNR "), meter | size(HEIGHT, EQUAL, 1));
        
        auto wpm_txt = text(std::to_string(static_cast<int>(current_wpm_)) + " WPM") | bold;
        auto wpm_panel = window(text(" Speed "), wpm_txt | center);

        auto editor_panel = window(text(" Configuration "), config_editor->Render());

        auto right_col = vbox({
            snr_panel,
            wpm_panel,
            editor_panel
        }) | size(WIDTH, EQUAL, 30);

        return hbox({
            text_panel,
            right_col
        }) | border | clear_under;
    });

    // Run blockingly
    std::thread ui_thread([&] {
        while(true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            screen_.PostEvent(Event::Custom);
        }
    });

    screen_.Loop(renderer);
    ui_thread.detach();
}
