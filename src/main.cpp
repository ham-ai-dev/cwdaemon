#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sstream>

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

// =========================================================================
// Benchmark accuracy comparison
// =========================================================================
static void print_benchmark_results(const std::string& decoded, const std::string& expected_file,
                                    float final_wpm, float final_snr) {
    // Read expected text from file
    std::ifstream ef(expected_file);
    if (!ef.is_open()) {
        std::cerr << "ERROR: Cannot open expected file: " << expected_file << std::endl;
        return;
    }

    std::string expected_line;
    std::string expected;
    while (std::getline(ef, expected_line)) {
        // Look for EXPECTED: prefix, otherwise use the whole file
        if (expected_line.substr(0, 9) == "EXPECTED:") {
            expected = expected_line.substr(9);
            // Trim leading space
            if (!expected.empty() && expected[0] == ' ') expected = expected.substr(1);
            break;
        }
    }
    if (expected.empty()) {
        // No EXPECTED: prefix found — use entire file content
        ef.clear();
        ef.seekg(0);
        std::ostringstream ss;
        ss << ef.rdbuf();
        expected = ss.str();
        // Trim trailing whitespace
        while (!expected.empty() && (expected.back() == '\n' || expected.back() == '\r'))
            expected.pop_back();
    }

    // Compute Levenshtein distance for character error rate
    int n = decoded.size();
    int m = expected.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = 0; i <= n; i++) dp[i][0] = i;
    for (int j = 0; j <= m; j++) dp[0][j] = j;
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            int cost = (decoded[i-1] == expected[j-1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
        }
    }
    int edit_distance = dp[n][m];
    int max_len = std::max(n, m);
    float accuracy = (max_len > 0) ? 100.0f * (1.0f - static_cast<float>(edit_distance) / max_len) : 100.0f;

    // Word-level comparison
    auto split_words = [](const std::string& s) {
        std::vector<std::string> words;
        std::istringstream iss(s);
        std::string w;
        while (iss >> w) words.push_back(w);
        return words;
    };
    auto dec_words = split_words(decoded);
    auto exp_words = split_words(expected);

    int correct_words = 0;
    int total_words = exp_words.size();
    for (size_t i = 0; i < std::min(dec_words.size(), exp_words.size()); i++) {
        if (dec_words[i] == exp_words[i]) correct_words++;
    }
    float word_accuracy = (total_words > 0) ? 100.0f * correct_words / total_words : 0.0f;

    // Print results
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║              CWDAEMON BENCHMARK RESULTS                     ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << "║ DECODED:  " << decoded.substr(0, 50)
              << std::string(std::max(0, 50 - (int)decoded.size()), ' ') << "║" << std::endl;
    if (decoded.size() > 50)
        std::cout << "║          " << decoded.substr(50, 50)
                  << std::string(std::max(0, 50 - (int)decoded.substr(50).size()), ' ') << "║" << std::endl;
    std::cout << "║ EXPECTED: " << expected.substr(0, 50)
              << std::string(std::max(0, 50 - (int)expected.size()), ' ') << "║" << std::endl;
    if (expected.size() > 50)
        std::cout << "║          " << expected.substr(50, 50)
                  << std::string(std::max(0, 50 - (int)expected.substr(50).size()), ' ') << "║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;

    // Accuracy color: green if > 80%, yellow if > 60%, red otherwise
    char acc_buf[64];
    snprintf(acc_buf, sizeof(acc_buf), "%.1f%%", accuracy);
    char wacc_buf[64];
    snprintf(wacc_buf, sizeof(wacc_buf), "%.1f%%", word_accuracy);

    std::cout << "║ Character Accuracy:  " << acc_buf
              << " (" << (max_len - edit_distance) << "/" << max_len << " correct)"
              << std::string(std::max(0, 25 - (int)std::string(acc_buf).size()), ' ') << "║" << std::endl;
    std::cout << "║ Word Accuracy:       " << wacc_buf
              << " (" << correct_words << "/" << total_words << " words)"
              << std::string(std::max(0, 27 - (int)std::string(wacc_buf).size()), ' ') << "║" << std::endl;
    std::cout << "║ Edit Distance:       " << edit_distance
              << std::string(std::max(0, 38 - (int)std::to_string(edit_distance).size()), ' ') << "║" << std::endl;
    std::cout << "║ Final WPM:           " << static_cast<int>(final_wpm)
              << std::string(std::max(0, 38 - (int)std::to_string((int)final_wpm).size()), ' ') << "║" << std::endl;
    std::cout << "║ SNR Metric:          " << static_cast<int>(final_snr)
              << std::string(std::max(0, 38 - (int)std::to_string((int)final_snr).size()), ' ') << "║" << std::endl;
    std::cout << "║ Decoded Chars:       " << n
              << std::string(std::max(0, 38 - (int)std::to_string(n).size()), ' ') << "║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]" << std::endl;
    std::cerr << "  --tui                    Launch terminal UI" << std::endl;
    std::cerr << "  --wav <file.wav>         Decode from WAV file instead of live audio" << std::endl;
    std::cerr << "  --expected <file.txt>    Compare decoded output against expected text" << std::endl;
    std::cerr << "                           (requires --wav, enables benchmark mode)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Benchmark example:" << std::endl;
    std::cerr << "  " << prog << " --wav baseline.wav --expected baseline_decode.txt" << std::endl;
}

int main(int argc, char** argv) {
    bool tui_mode = false;
    std::string wav_file = "";
    std::string expected_file = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tui") {
            tui_mode = true;
        } else if (arg == "--wav" && i + 1 < argc) {
            wav_file = argv[++i];
        } else if (arg == "--expected" && i + 1 < argc) {
            expected_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    bool benchmark_mode = !wav_file.empty() && !expected_file.empty();

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
        if (!benchmark_mode) {
            std::cerr << "WAV: " << wav->get_sample_rate() << " Hz, "
                      << wav->get_num_samples() << " samples ("
                      << wav->get_num_samples() / wav->get_sample_rate() << "s)" << std::endl;
        }
    }

    // =====================================================================
    // In benchmark mode: skip API/rigctl, no TUI — pure DSP test
    // =====================================================================
    std::shared_ptr<RigCtl> rigctl;
    std::shared_ptr<ApiServer> api;
    if (!benchmark_mode) {
        rigctl = std::make_shared<RigCtl>();
        rigctl->start();
        api = std::make_shared<ApiServer>(audio, rigctl);
        api->start(config.api_port);
    }

    // =====================================================================
    // Determine actual input sample rate for DSP
    // =====================================================================
    int dsp_sample_rate = config.sample_rate;
    if (!wav_file.empty()) {
        dsp_sample_rate = wav->get_sample_rate();
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

    // Collect decoded output for benchmark comparison
    std::string decoded_output;
    std::mutex decoded_mutex;

    TimingDecoder decoder(initial_wpm, [&](char c) {
        if (benchmark_mode) {
            std::lock_guard<std::mutex> lk(decoded_mutex);
            decoded_output += c;
        } else {
            ApiServer::broadcast_decoded_char(c);
            Tui::add_decoded_char(c);
            std::cout << c << std::flush;
        }
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
    // Start WAV playback thread
    // In benchmark mode: play_fast() for immediate results
    // In normal --wav mode: play() at real-time rate
    // =====================================================================
    std::thread wav_thread;
    if (wav) {
        if (benchmark_mode) {
            wav_thread = std::thread([wav]() { wav->play_fast(); });
        } else {
            wav_thread = std::thread([wav]() { wav->play(wav->get_sample_rate()); });
        }
    }

    // =====================================================================
    // Decoder thread — consumes audio from ring buffer
    // fldigi: rx_process → rx_FFTprocess → decode_stream
    // Also runs ToneDetector for AFC (auto frequency control)
    // =====================================================================
    std::thread decoder_thread([&]() {
        int metric_ctr = 0;

        // AFC: auto tone detection
        ToneDetector tone_detector(dsp_sample_rate, config.tone_frequency);
        int current_tone = config.tone_frequency;

        while (global_running) {
            auto sample_opt = ring_buffer->pop();
            if (sample_opt) {
                float sample = *sample_opt;
                dsp.process_sample(static_cast<double>(sample));

                // Feed AFC tone detector
                if (config.afc_enabled && tone_detector.feed(sample)) {
                    int detected = tone_detector.detected_frequency();
                    float conf = tone_detector.confidence();

                    // Only retune if confidence is high and frequency shifted
                    // by more than 15 Hz (avoid jitter on stable signals)
                    if (conf > 2.5f && std::abs(detected - current_tone) > 15) {
                        current_tone = detected;
                        dsp.set_frequency(static_cast<double>(detected));
                    }
                }

                // Update metrics periodically (~0.5s)
                if (++metric_ctr > dsp_sample_rate / 2) {
                    float wpm = decoder.get_wpm();
                    float snr = static_cast<float>(dsp.get_metric());

                    if (!benchmark_mode) {
                        ApiServer::current_wpm = wpm;
                        ApiServer::current_snr = snr;
                        ApiServer::current_freq = current_tone;
                        
                        ApiServer::broadcast_metrics(snr, wpm, current_tone,
                                                     static_cast<float>(dsp.get_agc_peak()));
                        Tui::update_metrics(wpm, snr, current_tone,
                                           tone_detector.confidence());
                    }
                    metric_ctr = 0;
                }
            } else {
                // In WAV mode: if the WAV is done and buffer is empty, exit
                if (wav && wav->is_done()) {
                    // Drain: give the decoder a moment to finish any pending character
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    if (benchmark_mode) {
        // Wait for decoder to finish processing the WAV
        decoder_thread.join();
        if (wav_thread.joinable()) wav_thread.join();

        // Print benchmark results
        std::string final_decoded;
        {
            std::lock_guard<std::mutex> lk(decoded_mutex);
            final_decoded = decoded_output;
        }

        // Trim trailing whitespace
        while (!final_decoded.empty() && final_decoded.back() == ' ')
            final_decoded.pop_back();

        print_benchmark_results(final_decoded, expected_file,
                                decoder.get_wpm(),
                                static_cast<float>(dsp.get_metric()));
    } else if (tui_mode) {
        Tui tui;
        tui.run();

        global_running = false;
        decoder_thread.join();
        if (wav_thread.joinable()) wav_thread.join();
    } else {
        while (global_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        decoder_thread.join();
        if (wav_thread.joinable()) wav_thread.join();
    }

    if (audio) audio->stop();
    if (rigctl) rigctl->stop();
    if (api) api->stop();
    if (!benchmark_mode) ConfigManager::instance().save();

    return 0;
}
