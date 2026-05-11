#include "tui.hpp"
#include "config.hpp"
#include "audio.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component_options.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

using namespace ftxui;

std::string Tui::decoded_text_ = "";
float Tui::current_wpm_ = 0.0f;
float Tui::current_snr_ = 0.0f;
std::mutex Tui::tui_mutex_;
Tui::ConfigChangeCallback Tui::config_change_cb_;

Tui::Tui() : screen_(ScreenInteractive::Fullscreen()) {}

void Tui::add_decoded_char(char c) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    decoded_text_ += c;
    if (decoded_text_.length() > 2000) {
        decoded_text_ = decoded_text_.substr(decoded_text_.length() - 2000);
    }
}

void Tui::update_metrics(float wpm, float snr) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    current_wpm_ = wpm;
    current_snr_ = snr;
}

void Tui::set_config_change_callback(ConfigChangeCallback cb) {
    config_change_cb_ = cb;
}

void Tui::run() {
    auto config = ConfigManager::instance().get_config();

    // =====================================================================
    // Enumerate audio devices via PortAudio
    // =====================================================================
    std::vector<std::string> device_names = AudioCapture::get_devices();
    std::vector<std::string> device_labels;
    int selected_device = 0;

    for (size_t i = 0; i < device_names.size(); i++) {
        device_labels.push_back(device_names[i]);
        if (device_names[i] == config.audio_device) {
            selected_device = static_cast<int>(i);
        }
    }
    if (device_labels.empty()) {
        device_labels.push_back("(no devices found)");
    }

    // =====================================================================
    // DSP Configuration fields
    // =====================================================================
    std::string tone_freq_str = std::to_string(config.tone_frequency);
    std::string sample_rate_str = std::to_string(config.sample_rate);
    std::string buffer_size_str = std::to_string(config.buffer_size);
    std::string wpm_min_str = std::to_string(config.wpm_min);
    std::string wpm_max_str = std::to_string(config.wpm_max);
    std::string agc_decay_str = std::to_string(config.agc_decay);

    // =====================================================================
    // API Configuration fields
    // =====================================================================
    std::string api_port_str = std::to_string(config.api_port);
    std::string log_level_str = config.log_level;

    // =====================================================================
    // Rigctl Configuration fields
    // =====================================================================
    bool rigctl_enabled = config.rigctl_enabled;
    std::string rigctl_host_str = config.rigctl_host;
    std::string rigctl_port_str = std::to_string(config.rigctl_port);
    std::string rigctl_poll_str = std::to_string(config.rigctl_poll_interval_ms);

    // =====================================================================
    // Status message
    // =====================================================================
    std::string status_msg = "Ready";

    // =====================================================================
    // Tab selection
    // =====================================================================
    int tab_selected = 0;
    std::vector<std::string> tab_labels = {"Audio", "DSP", "API", "Rigctl"};

    // =====================================================================
    // Build components — Audio tab
    // =====================================================================
    auto device_menu = Menu(&device_labels, &selected_device);

    // =====================================================================
    // Build components — DSP tab
    // =====================================================================
    auto tone_input = Input(&tone_freq_str, "600");
    auto wpm_min_input = Input(&wpm_min_str, "5");
    auto wpm_max_input = Input(&wpm_max_str, "50");
    auto agc_input = Input(&agc_decay_str, "10");

    auto dsp_container = Container::Vertical({
        tone_input,
        wpm_min_input,
        wpm_max_input,
        agc_input,
    });

    // =====================================================================
    // Build components — API tab
    // =====================================================================
    auto port_input = Input(&api_port_str, "8080");
    auto loglevel_input = Input(&log_level_str, "info");

    auto api_container = Container::Vertical({
        port_input,
        loglevel_input,
    });

    // =====================================================================
    // Build components — Rigctl tab
    // =====================================================================
    auto rigctl_toggle = Checkbox("Enabled", &rigctl_enabled);
    auto rigctl_host_input = Input(&rigctl_host_str, "127.0.0.1");
    auto rigctl_port_input = Input(&rigctl_port_str, "4532");
    auto rigctl_poll_input = Input(&rigctl_poll_str, "1000");

    auto rigctl_container = Container::Vertical({
        rigctl_toggle,
        rigctl_host_input,
        rigctl_port_input,
        rigctl_poll_input,
    });

    // =====================================================================
    // Tab switching + Save/Apply buttons
    // =====================================================================
    auto tab_toggle = Toggle(&tab_labels, &tab_selected);

    auto tab_content = Container::Tab({
        device_menu,
        dsp_container,
        api_container,
        rigctl_container,
    }, &tab_selected);

    auto save_btn = Button(" Save Config ", [&] {
        try {
            if (selected_device >= 0 && selected_device < (int)device_names.size())
                config.audio_device = device_names[selected_device];
            config.tone_frequency = std::stoi(tone_freq_str);
            config.sample_rate = std::stoi(sample_rate_str);
            config.buffer_size = std::stoi(buffer_size_str);
            config.wpm_min = std::stoi(wpm_min_str);
            config.wpm_max = std::stoi(wpm_max_str);
            config.agc_decay = std::stoi(agc_decay_str);
            config.api_port = std::stoi(api_port_str);
            config.log_level = log_level_str;
            config.rigctl_enabled = rigctl_enabled;
            config.rigctl_host = rigctl_host_str;
            config.rigctl_port = std::stoi(rigctl_port_str);
            config.rigctl_poll_interval_ms = std::stoi(rigctl_poll_str);

            ConfigManager::instance().update_config(config);
            ConfigManager::instance().save();
            status_msg = "Config saved!";

            // Notify live parameter change
            if (config_change_cb_) {
                config_change_cb_("tone_frequency", tone_freq_str);
                config_change_cb_("audio_device", config.audio_device);
            }
        } catch (const std::exception& e) {
            status_msg = std::string("Error: ") + e.what();
        }
    });

    auto apply_tone_btn = Button(" Apply Tone ", [&] {
        try {
            config.tone_frequency = std::stoi(tone_freq_str);
            ConfigManager::instance().update_config(config);
            if (config_change_cb_)
                config_change_cb_("tone_frequency", tone_freq_str);
            status_msg = "Tone frequency updated to " + tone_freq_str + " Hz";
        } catch (...) {
            status_msg = "Invalid tone frequency";
        }
    });

    auto clear_btn = Button(" Clear Text ", [&] {
        std::lock_guard<std::mutex> lock(tui_mutex_);
        decoded_text_.clear();
    });

    auto buttons = Container::Horizontal({
        save_btn,
        apply_tone_btn,
        clear_btn,
    });

    // =====================================================================
    // Main layout container
    // =====================================================================
    auto main_container = Container::Vertical({
        tab_toggle,
        tab_content,
        buttons,
    });

    // =====================================================================
    // Renderer
    // =====================================================================
    auto renderer = Renderer(main_container, [&] {
        std::lock_guard<std::mutex> lock(tui_mutex_);

        // ----- Decoded text panel (main area) -----
        auto text_panel = window(
            text(" ▶ Decoded CW Text ") | bold,
            paragraph(decoded_text_) | flex
        ) | flex;

        // ----- Metrics bar -----
        float snr_pct = std::clamp(current_snr_ / 100.0f, 0.0f, 1.0f);
        Color snr_color = (snr_pct > 0.5f) ? Color::Green
                        : (snr_pct > 0.2f) ? Color::Yellow
                        : Color::Red;

        auto metrics_bar = hbox({
            text(" WPM: ") | bold,
            text(std::to_string(static_cast<int>(current_wpm_))) | bold | color(Color::Cyan),
            separator(),
            text(" SNR: ") | bold,
            gauge(snr_pct) | color(snr_color) | size(WIDTH, EQUAL, 20),
            text(" " + std::to_string(static_cast<int>(current_snr_)) + " "),
            separator(),
            text(" Tone: ") | bold,
            text(tone_freq_str + " Hz") | color(Color::Yellow),
            separator(),
            text(" Dev: ") | bold,
            text(config.audio_device.substr(0, 25)) | color(Color::Blue) | dim,
            filler(),
            text(status_msg) | dim,
        }) | border;

        // ----- Config panel (right side) -----
        Element tab_body;
        switch (tab_selected) {
            case 0: { // Audio
                auto dev_list = device_menu->Render() | vscroll_indicator | frame
                    | size(HEIGHT, LESS_THAN, 12);
                tab_body = vbox({
                    text("Select Input Device:") | bold,
                    separator(),
                    dev_list,
                    separator(),
                    hbox({text("Sample Rate: "), text(sample_rate_str + " Hz") | dim}),
                    hbox({text("Buffer Size: "), text(buffer_size_str) | dim}),
                });
                break;
            }
            case 1: { // DSP
                tab_body = vbox({
                    hbox({text("Tone Freq (Hz):  ") | bold, tone_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("WPM Min:         ") | bold, wpm_min_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("WPM Max:         ") | bold, wpm_max_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("AGC Decay:       ") | bold, agc_input->Render() | size(WIDTH, EQUAL, 8)}),
                });
                break;
            }
            case 2: { // API
                tab_body = vbox({
                    hbox({text("API Port:   ") | bold, port_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("Log Level:  ") | bold, loglevel_input->Render() | size(WIDTH, EQUAL, 8)}),
                });
                break;
            }
            case 3: { // Rigctl
                tab_body = vbox({
                    rigctl_toggle->Render(),
                    hbox({text("Host:     ") | bold, rigctl_host_input->Render() | size(WIDTH, EQUAL, 16)}),
                    hbox({text("Port:     ") | bold, rigctl_port_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("Poll (ms):") | bold, rigctl_poll_input->Render() | size(WIDTH, EQUAL, 8)}),
                });
                break;
            }
        }

        auto config_panel = window(
            text(" ⚙ Configuration ") | bold,
            vbox({
                tab_toggle->Render() | center,
                separator(),
                tab_body | flex,
                separator(),
                buttons->Render() | center,
            })
        ) | size(WIDTH, EQUAL, 38);

        // ----- Assemble -----
        return vbox({
            hbox({
                text_panel,
                config_panel,
            }) | flex,
            metrics_bar,
        });
    });

    // =====================================================================
    // Periodic refresh thread (100ms)
    // =====================================================================
    std::atomic<bool> ui_running{true};
    std::thread ui_thread([&] {
        while (ui_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            screen_.PostEvent(Event::Custom);
        }
    });

    screen_.Loop(renderer);

    ui_running = false;
    if (ui_thread.joinable()) ui_thread.join();
}
