#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <filesystem>

#include "config.hpp"
#include "audio.hpp"
#include "wavfile.hpp"
#include "goertzel.hpp"
#include "timing.hpp"
#include "rigctl.hpp"
#include "api.hpp"
#include "tui.hpp"

std::atomic<bool> global_running{true};

void signal_handler(int signum) {
    global_running = false;
}

// =========================================================================
// DSP event callback — bridges CWDsp events into TimingDecoder
// This is the glue matching fldigi's decode_stream() → handle_event() path
// =========================================================================
struct DecoderState {
    TimingDecoder* decoder;
    CWDsp* dsp;
};

static void dsp_event_callback(int event, unsigned int smpl_ctr, void* user) {
    auto* state = static_cast<DecoderState*>(user);
    std::string decoded;

    // The DSP layer updates the rx_state when it fires KEYDOWN/KEYUP
    // We mirror this into the TimingDecoder before dispatching the event
    bool result = state->decoder->handle_event(event, smpl_ctr, decoded);

    // After handle_event processes KEYDOWN, the TimingDecoder sets its own
    // internal state to RS_IN_TONE. The DSP layer needs to know this for
    // hysteresis (only fire KEYUP if currently RS_IN_TONE).
    // We sync the state back.
    // This is how fldigi works: cw_receive_state is a member of the cw class,
    // shared between decode_stream and handle_event.
}

int main(int argc, char** argv) {
    bool tui_mode = false;
    std::string wav_file = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tui") {
            tui_mode = true;
        } else if (arg == "--wav" && i + 1 < argc) {
            wav_file = argv[++i];
        }
    }

    // Signals for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize Config
    std::string home_dir = getenv("HOME");
    std::string config_dir = home_dir + "/.config/cwdaemon";
    std::filesystem::create_directories(config_dir);
    ConfigManager::instance().load(config_dir + "/config.toml");
    auto config = ConfigManager::instance().get_config();

    auto ring_buffer = std::make_shared<RingBuffer<float>>(65536);

    std::shared_ptr<AudioCapture> audio;
    std::shared_ptr<WavFile> wav;
    
    if (wav_file.empty()) {
        audio = std::make_shared<AudioCapture>(ring_buffer);
        audio->init(config.audio_device, config.sample_rate, config.buffer_size);
        audio->start();
    } else {
        wav = std::make_shared<WavFile>(ring_buffer);
        if (!wav->load(wav_file)) {
            return 1;
        }
        std::thread([wav]() { wav->play(8000); }).detach();
    }

    auto rigctl = std::make_shared<RigCtl>();
    rigctl->start();

    auto api = std::make_shared<ApiServer>(audio, rigctl);
    api->start(config.api_port);

    // =====================================================================
    // Determine actual input sample rate for DSP
    // WAV files may be 8000 Hz; live audio is config.sample_rate (e.g. 48000)
    // =====================================================================
    int dsp_sample_rate = config.sample_rate;
    // For WAV mode, use WAV file's native rate (gentone produces 8000 Hz)
    // The WavFile::play() feeds at the WAV rate regardless of config
    if (!wav_file.empty()) {
        dsp_sample_rate = 8000; // gentone default
    }

    // =====================================================================
    // Create DSP and Timing Decoder — matching fldigi architecture
    //
    // fldigi: bandwidth for matched filter = 5.0 * speed / 1.2
    // Default speed = 18 WPM → bandwidth = 75 Hz
    // =====================================================================
    int initial_wpm = 18;
    double bandwidth = 5.0 * initial_wpm / 1.2; // ~75 Hz matched filter

    CWDsp dsp(dsp_sample_rate, config.tone_frequency, bandwidth, initial_wpm);

    TimingDecoder decoder(initial_wpm, [](char c) {
        ApiServer::broadcast_decoded_char(c);
        Tui::add_decoded_char(c);
        std::cout << c << std::flush;
    });

    // =====================================================================
    // CRITICAL: Share cw_receive_state between DSP and TimingDecoder
    // In fldigi, both decode_stream() and handle_event() read/write the
    // same cw_receive_state member of the cw class. We replicate this by
    // giving the DSP a pointer to TimingDecoder's state variable.
    // =====================================================================
    dsp.set_rx_state_ptr(decoder.get_rx_state_ptr());

    // Wire DSP events into the TimingDecoder
    DecoderState dec_state;
    dec_state.decoder = &decoder;
    dec_state.dsp = &dsp;

    dsp.set_event_callback([](int event, unsigned int smpl_ctr, void* user) {
        auto* state = static_cast<DecoderState*>(user);
        std::string decoded;
        state->decoder->handle_event(event, smpl_ctr, decoded);
    }, &dec_state);

    // =====================================================================
    // Decoder thread — consumes audio from ring buffer
    // fldigi: rx_process → rx_FFTprocess → decode_stream
    // =====================================================================
    std::thread decoder_thread([&]() {
        int metric_ctr = 0;
        while (global_running) {
            auto sample_opt = ring_buffer->pop();
            if (sample_opt) {
                dsp.process_sample(static_cast<double>(*sample_opt));

                // Update metrics periodically (~0.5s)
                if (++metric_ctr > dsp_sample_rate / 2) {
                    float wpm = decoder.get_wpm();
                    float snr = static_cast<float>(dsp.get_metric());

                    ApiServer::current_wpm = wpm;
                    ApiServer::current_snr = snr;
                    ApiServer::current_freq = config.tone_frequency;
                    
                    ApiServer::broadcast_metrics(snr, wpm, config.tone_frequency,
                                                 static_cast<float>(dsp.get_agc_peak()));
                    Tui::update_metrics(wpm, snr);
                    metric_ctr = 0;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    if (tui_mode) {
        Tui tui;
        tui.run();
    } else {
        while (global_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    global_running = false;
    decoder_thread.join();
    
    if (audio) audio->stop();
    rigctl->stop();
    api->stop();
    ConfigManager::instance().save();

    return 0;
}
