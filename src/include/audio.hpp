#pragma once

#include <portaudio.h>
#include <string>
#include <memory>
#include <vector>
#include "ringbuffer.hpp"

class AudioCapture {
public:
    AudioCapture(std::shared_ptr<RingBuffer<float>> ring_buffer);
    ~AudioCapture();

    bool init(const std::string& device_name, int sample_rate, int buffer_size);
    bool start();
    bool stop();
    
    static std::vector<std::string> get_devices();

private:
    static int pa_callback(const void* input, void* output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData);

    std::shared_ptr<RingBuffer<float>> ring_buffer_;
    PaStream* stream_;
    std::string device_name_;
    int sample_rate_;
    int buffer_size_;
    int channels_;
    bool is_running_;
};
