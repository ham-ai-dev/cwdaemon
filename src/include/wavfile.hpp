#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ringbuffer.hpp"

class WavFile {
public:
    WavFile(std::shared_ptr<RingBuffer<float>> ring_buffer);
    
    bool load(const std::string& path);
    void play(int sample_rate); // Blocks and feeds ringbuffer at specified sample rate

private:
    std::shared_ptr<RingBuffer<float>> ring_buffer_;
    std::vector<float> audio_data_;
};
