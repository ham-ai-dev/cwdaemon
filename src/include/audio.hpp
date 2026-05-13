#pragma once

#include <portaudio.h>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include "ringbuffer.hpp"

class AudioCapture {
public:
    AudioCapture(std::shared_ptr<RingBuffer<float>> ring_buffer);
    ~AudioCapture();

    bool init(const std::string& device_name, int sample_rate, int buffer_size);
    bool start();
    bool stop();

    // Hot-swap: switch to a new audio device without restarting the application
    // Returns true if the switch succeeded, false if it failed (stays on old device)
    bool switch_device(const std::string& device_name);

    // Get current device name and status
    std::string get_device_name() const;
    bool is_running() const { return is_running_.load(); }

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
    std::atomic<bool> is_running_;
    mutable std::mutex device_mutex_;  // Protects device switching
};
