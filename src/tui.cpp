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
int Tui::detected_freq_ = 0;
float Tui::afc_confidence_ = 0.0f;
std::mutex Tui::tui_mutex_;
Tui::ConfigChangeCallback Tui::config_change_cb_;
double Tui::rig_frequency_ = 0.0;
std::string Tui::rig_mode_ = "";

// =========================================================================
// Band mapper — frequency (Hz) to ham band name
// =========================================================================
static std::string freq_to_band(double freq_hz) {
    double mhz = freq_hz / 1e6;
    if (mhz >= 1.8 && mhz <= 2.0)   return "160m";
    if (mhz >= 3.5 && mhz <= 4.0)   return "80m";
    if (mhz >= 5.3 && mhz <= 5.4)   return "60m";
    if (mhz >= 7.0 && mhz <= 7.3)   return "40m";
    if (mhz >= 10.1 && mhz <= 10.15) return "30m";
    if (mhz >= 14.0 && mhz <= 14.35) return "20m";
    if (mhz >= 18.068 && mhz <= 18.168) return "17m";
    if (mhz >= 21.0 && mhz <= 21.45) return "15m";
    if (mhz >= 24.89 && mhz <= 24.99) return "12m";
    if (mhz >= 28.0 && mhz <= 29.7) return "10m";
    if (mhz >= 50.0 && mhz <= 54.0) return "6m";
    if (mhz >= 144.0 && mhz <= 148.0) return "2m";
    if (mhz >= 420.0 && mhz <= 450.0) return "70cm";
    return "";
}

static std::string format_freq_mhz(double freq_hz) {
    double mhz = freq_hz / 1e6;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", mhz);
    return std::string(buf);
}

Tui::Tui() : screen_(ScreenInteractive::Fullscreen()) {}

void Tui::add_decoded_char(const std::string& s) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    decoded_text_ += s;
    if (decoded_text_.length() > 2000) {
        decoded_text_ = decoded_text_.substr(decoded_text_.length() - 2000);
    }
}

void Tui::update_metrics(float wpm, float snr, int detected_freq, float afc_confidence) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    current_wpm_ = wpm;
    current_snr_ = snr;
    detected_freq_ = detected_freq;
    afc_confidence_ = afc_confidence;
}

void Tui::update_rig_info(double freq_hz, const std::string& mode) {
    std::lock_guard<std::mutex> lock(tui_mutex_);
    rig_frequency_ = freq_hz;
    rig_mode_ = mode;
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
    std::string active_device = config.audio_device;

    // =====================================================================
    // Tab selection
    // =====================================================================
    int tab_selected = 0;
    std::vector<std::string> tab_labels = {" Audio ", " DSP ", " API ", " Rigctl "};

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
    bool afc_enabled = config.afc_enabled;
    auto afc_toggle = Checkbox("AFC (Auto Freq)", &afc_enabled);

    auto dsp_container = Container::Vertical({
        tone_input,
        wpm_min_input,
        wpm_max_input,
        agc_input,
        afc_toggle,
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

    auto save_btn = Button("Save", [&] {
        try {
            if (selected_device >= 0 && selected_device < (int)device_names.size())
                config.audio_device = device_names[selected_device];
            config.tone_frequency = std::stoi(tone_freq_str);
            config.sample_rate = std::stoi(sample_rate_str);
            config.buffer_size = std::stoi(buffer_size_str);
            config.wpm_min = std::stoi(wpm_min_str);
            config.wpm_max = std::stoi(wpm_max_str);
            config.agc_decay = std::stoi(agc_decay_str);
            config.afc_enabled = afc_enabled;
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

    auto apply_tone_btn = Button("Tone", [&] {
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

    auto clear_btn = Button("Clear", [&] {
        std::lock_guard<std::mutex> lock(tui_mutex_);
        decoded_text_.clear();
    });

    auto switch_dev_btn = Button("⚡Switch", [&] {
        if (selected_device >= 0 && selected_device < (int)device_names.size()) {
            std::string new_dev = device_names[selected_device];
            if (config_change_cb_) {
                config_change_cb_("audio_device", new_dev);
                active_device = new_dev;
                config.audio_device = new_dev;
                ConfigManager::instance().update_config(config);
                status_msg = "Switched to: " + new_dev.substr(0, 25);
            } else {
                status_msg = "No callback — restart required";
            }
        }
    });

    auto refresh_dev_btn = Button("↻Scan", [&] {
        device_names = AudioCapture::get_devices();
        device_labels.clear();
        selected_device = 0;
        for (size_t i = 0; i < device_names.size(); i++) {
            device_labels.push_back(device_names[i]);
            if (device_names[i] == active_device) {
                selected_device = static_cast<int>(i);
            }
        }
        if (device_labels.empty()) {
            device_labels.push_back("(no devices found)");
        }
        status_msg = "Devices refreshed (" + std::to_string(device_names.size()) + " found)";
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
        switch_dev_btn,
        refresh_dev_btn,
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

        // AFC status string
        std::string afc_str;
        if (afc_enabled && detected_freq_ > 0) {
            afc_str = " AFC:" + std::to_string(detected_freq_) + "Hz";
        } else if (afc_enabled) {
            afc_str = " AFC:scan";
        } else {
            afc_str = " AFC:off";
        }

        auto metrics_bar = hbox({
            text(" WPM: ") | bold,
            text(std::to_string(static_cast<int>(current_wpm_))) | bold | color(Color::Cyan),
            separator(),
            text(" SNR: ") | bold,
            gauge(snr_pct) | color(snr_color) | size(WIDTH, EQUAL, 15),
            text(" " + std::to_string(static_cast<int>(current_snr_)) + " "),
            separator(),
            text(afc_str) | bold | color(afc_enabled ? Color::Green : Color::GrayDark),
            separator(),
            // Rig frequency and band
            (rig_frequency_ > 0
                ? hbox({
                    text(" " + format_freq_mhz(rig_frequency_) + " ") | bold | color(Color::Magenta),
                    text(freq_to_band(rig_frequency_)) | bold | color(Color::Yellow),
                    text(" " + rig_mode_ + " ") | dim,
                  })
                : text(" No Rig ") | dim),
            filler(),
            text(status_msg) | dim,
        }) | border;

        // ----- Config panel (right side) -----
        Element tab_body;
        switch (tab_selected) {
            case 0: { // Audio
                auto dev_list = device_menu->Render() | vscroll_indicator | frame
                    | size(HEIGHT, LESS_THAN, 8);

                // Active device indicator
                std::string active_short = active_device;
                if (active_short.length() > 28) active_short = active_short.substr(0, 28) + "…";
                bool is_selected_active = (selected_device >= 0 && selected_device < (int)device_names.size()
                    && device_names[selected_device] == active_device);

                tab_body = vbox({
                    hbox({text("● Active: ") | bold | color(Color::Green),
                          text(active_short) | color(Color::Green)}),
                    separator(),
                    text("Select Input Device:") | bold,
                    dev_list,
                    separator(),
                    hbox({
                        switch_dev_btn->Render()
                            | (is_selected_active ? dim : nothing),
                        text(" "),
                        refresh_dev_btn->Render(),
                    }),
                    separator(),
                    hbox({text("Rate: "), text(sample_rate_str + " Hz") | dim,
                          text("  Buf: "), text(buffer_size_str) | dim}),
                });
                break;
            }
            case 1: { // DSP
                tab_body = vbox({
                    hbox({text("Tone Freq (Hz):  ") | bold, tone_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("WPM Min:         ") | bold, wpm_min_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("WPM Max:         ") | bold, wpm_max_input->Render() | size(WIDTH, EQUAL, 8)}),
                    hbox({text("AGC Decay:       ") | bold, agc_input->Render() | size(WIDTH, EQUAL, 8)}),
                    separator(),
                    afc_toggle->Render(),
                    (detected_freq_ > 0 && afc_enabled)
                        ? hbox({text("Detected: ") | dim, text(std::to_string(detected_freq_) + " Hz") | bold | color(Color::Green)})
                        : text("Detecting...") | dim,
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
