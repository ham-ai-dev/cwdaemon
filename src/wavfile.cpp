#include "wavfile.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

struct WavHeader {
    char riff_header[4]; // "RIFF"
    int wav_size;
    char wave_header[4]; // "WAVE"
    char fmt_header[4];  // "fmt "
    int fmt_chunk_size;
    short audio_format;
    short num_channels;
    int sample_rate;
    int byte_rate;
    short sample_alignment;
    short bit_depth;
    char data_header[4]; // "data"
    int data_bytes;
};

WavFile::WavFile(std::shared_ptr<RingBuffer<float>> ring_buffer)
    : ring_buffer_(ring_buffer) {}

bool WavFile::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open WAV file: " << path << std::endl;
        return false;
    }

    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    if (std::string(header.riff_header, 4) != "RIFF" || std::string(header.wave_header, 4) != "WAVE") {
        std::cerr << "Invalid WAV file format" << std::endl;
        return false;
    }

    if (header.num_channels != 1) {
        std::cerr << "Only mono WAV files are supported" << std::endl;
        return false;
    }

    if (header.bit_depth != 16) {
        std::cerr << "Only 16-bit WAV files are supported" << std::endl;
        return false;
    }

    sample_rate_ = header.sample_rate;

    int num_samples = header.data_bytes / 2;
    std::vector<int16_t> int_data(num_samples);
    file.read(reinterpret_cast<char*>(int_data.data()), header.data_bytes);

    audio_data_.resize(num_samples);
    for (int i = 0; i < num_samples; ++i) {
        audio_data_[i] = static_cast<float>(int_data[i]) / 32768.0f;
    }

    return true;
}

void WavFile::play(int sample_rate) {
    auto sample_duration = std::chrono::microseconds(1000000 / sample_rate);
    for (float sample : audio_data_) {
        while (!ring_buffer_->push(sample)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Throttle to real-time (approximate for testing)
        std::this_thread::sleep_for(sample_duration);
    }
    done_ = true;
}

void WavFile::play_fast() {
    // Feed samples as fast as the ring buffer allows — no real-time throttle
    // Used for benchmarks where we want results immediately
    for (float sample : audio_data_) {
        while (!ring_buffer_->push(sample)) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    done_ = true;
}
