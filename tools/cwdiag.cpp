// Diagnostic tool: feed WAV through CWDsp pipeline and print events + decoded chars
// No Drogon, no TUI, no PortAudio — just pure DSP + timing

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

#include "goertzel.hpp"
#include "timing.hpp"

struct WavHeader {
    char riff_header[4];
    int wav_size;
    char wave_header[4];
    char fmt_header[4];
    int fmt_chunk_size;
    short audio_format;
    short num_channels;
    int sample_rate;
    int byte_rate;
    short sample_alignment;
    short bit_depth;
    char data_header[4];
    int data_bytes;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.wav> [tone_freq] [wpm]" << std::endl;
        return 1;
    }

    double tone_freq = 600.0;
    int wpm = 18;
    if (argc >= 3) tone_freq = std::stod(argv[2]);
    if (argc >= 4) wpm = std::stoi(argv[3]);

    // Load WAV
    std::ifstream file(argv[1], std::ios::binary);
    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    if (std::string(header.riff_header, 4) != "RIFF") {
        std::cerr << "Not a WAV file" << std::endl;
        return 1;
    }

    std::cerr << "WAV: " << header.sample_rate << " Hz, "
              << header.num_channels << " ch, "
              << header.bit_depth << " bit, "
              << header.data_bytes / (header.sample_rate * header.num_channels * header.bit_depth / 8)
              << " sec" << std::endl;

    int num_samples = header.data_bytes / (header.bit_depth / 8);
    std::vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), header.data_bytes);

    std::vector<double> audio(num_samples);
    for (int i = 0; i < num_samples; i++)
        audio[i] = static_cast<double>(raw[i]) / 32768.0;

    std::cerr << "Loaded " << num_samples << " samples" << std::endl;
    std::cerr << "Tone freq: " << tone_freq << " Hz, initial WPM: " << wpm << std::endl;

    // Setup DSP
    double bandwidth = 5.0 * wpm / 1.2;
    CWDsp dsp(header.sample_rate, tone_freq, bandwidth, wpm);

    int char_count = 0;
    TimingDecoder decoder(wpm, [&char_count](char c) {
        std::cout << c << std::flush;
        char_count++;
    });

    // Share state
    dsp.set_rx_state_ptr(decoder.get_rx_state_ptr());

    struct State { TimingDecoder* dec; CWDsp* dsp; };
    State state{&decoder, &dsp};

    dsp.set_event_callback([](int event, unsigned int smpl_ctr, void* user) {
        auto* s = static_cast<State*>(user);
        std::string decoded;
        s->dec->handle_event(event, smpl_ctr, decoded);
    }, &state);

    // Process all samples
    for (int i = 0; i < num_samples; i++) {
        dsp.process_sample(audio[i]);
    }

    std::cout << std::endl;
    std::cerr << "Decoded " << char_count << " characters" << std::endl;
    std::cerr << "Final WPM: " << decoder.get_wpm() << std::endl;
    std::cerr << "Final metric: " << dsp.get_metric() << std::endl;
    std::cerr << "AGC peak: " << dsp.get_agc_peak() << std::endl;
    std::cerr << "Noise floor: " << dsp.get_noise_floor() << std::endl;

    return 0;
}
