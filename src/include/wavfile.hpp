#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "ringbuffer.hpp"

class WavFile {
public:
    WavFile(std::shared_ptr<RingBuffer<float>> ring_buffer);
    
    bool load(const std::string& path);
    void play(int sample_rate); // Blocks and feeds ringbuffer at specified sample rate
    void play_fast();           // Feed as fast as ring buffer allows (for benchmarks)

    int get_sample_rate() const { return sample_rate_; }
    int get_num_samples() const { return static_cast<int>(audio_data_.size()); }
    bool is_done() const { return done_.load(); }

private:
    std::shared_ptr<RingBuffer<float>> ring_buffer_;
    std::vector<float> audio_data_;
    int sample_rate_ = 0;
    std::atomic<bool> done_{false};
};
