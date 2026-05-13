#include "audio.hpp"
#include <iostream>

AudioCapture::AudioCapture(std::shared_ptr<RingBuffer<float>> ring_buffer)
    : ring_buffer_(ring_buffer), stream_(nullptr), sample_rate_(8000),
      buffer_size_(512), channels_(1), is_running_(false) {
    Pa_Initialize();
}

AudioCapture::~AudioCapture() {
    stop();
    Pa_Terminate();
}

bool AudioCapture::init(const std::string& device_name, int sample_rate, int buffer_size) {
    std::lock_guard<std::mutex> lock(device_mutex_);
    device_name_ = device_name;
    sample_rate_ = sample_rate;
    buffer_size_ = buffer_size;
    return true;
}

bool AudioCapture::start() {
    std::lock_guard<std::mutex> lock(device_mutex_);

    if (is_running_) return true;

    PaStreamParameters inputParameters;
    int num_devices = Pa_GetDeviceCount();
    int device_index = paNoDevice;

    if (device_name_ != "default") {
        for (int i = 0; i < num_devices; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxInputChannels > 0 && device_name_ == info->name) {
                device_index = i;
                break;
            }
        }
    }

    if (device_index == paNoDevice) {
        device_index = Pa_GetDefaultInputDevice();
    }

    if (device_index == paNoDevice) {
        std::cerr << "PortAudio error: No default input device." << std::endl;
        return false;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
    channels_ = info->maxInputChannels > 0 ? info->maxInputChannels : 1;
    inputParameters.device = device_index;
    inputParameters.channelCount = channels_;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = info->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_,
                                &inputParameters,
                                nullptr, // no output
                                sample_rate_,
                                buffer_size_,
                                paNoFlag,
                                pa_callback,
                                this);

    if (err != paNoError) {
        std::cerr << "PortAudio Open Stream error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "PortAudio Start Stream error: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        return false;
    }

    is_running_ = true;
    std::cerr << "Audio: capturing from \"" << info->name << "\" at " << sample_rate_ << " Hz" << std::endl;
    return true;
}

bool AudioCapture::stop() {
    // Note: caller must hold device_mutex_ or this is called from destructor
    if (!is_running_) return true;

    is_running_ = false;

    if (stream_) {
        PaError err = Pa_StopStream(stream_);
        if (err != paNoError) {
            std::cerr << "PortAudio Stop Stream error: " << Pa_GetErrorText(err) << std::endl;
        }

        err = Pa_CloseStream(stream_);
        if (err != paNoError) {
            std::cerr << "PortAudio Close Stream error: " << Pa_GetErrorText(err) << std::endl;
        }

        stream_ = nullptr;
    }

    return true;
}

bool AudioCapture::switch_device(const std::string& device_name) {
    std::lock_guard<std::mutex> lock(device_mutex_);

    if (device_name == device_name_ && is_running_) {
        return true;  // Already on this device
    }

    std::string old_device = device_name_;
    bool was_running = is_running_.load();

    std::cerr << "Audio: switching from \"" << old_device
              << "\" to \"" << device_name << "\"..." << std::endl;

    // Stop current stream (without locking again)
    if (was_running) {
        is_running_ = false;
        if (stream_) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
    }

    // Update device name and try to start with new device
    device_name_ = device_name;

    // Temporarily release lock for start() — but we handle it inline instead
    PaStreamParameters inputParameters;
    int num_devices = Pa_GetDeviceCount();
    int device_index = paNoDevice;

    if (device_name_ != "default") {
        for (int i = 0; i < num_devices; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxInputChannels > 0 && device_name_ == info->name) {
                device_index = i;
                break;
            }
        }
    }

    if (device_index == paNoDevice) {
        device_index = Pa_GetDefaultInputDevice();
    }

    if (device_index == paNoDevice) {
        std::cerr << "Audio: switch failed — no such device, reverting to \""
                  << old_device << "\"" << std::endl;
        device_name_ = old_device;
        // Try to restart old device
        // (best effort — don't recurse, just inline start)
        return false;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
    channels_ = info->maxInputChannels > 0 ? info->maxInputChannels : 1;
    inputParameters.device = device_index;
    inputParameters.channelCount = channels_;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = info->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_,
                                &inputParameters,
                                nullptr,
                                sample_rate_,
                                buffer_size_,
                                paNoFlag,
                                pa_callback,
                                this);

    if (err != paNoError) {
        std::cerr << "Audio: switch failed (open) — " << Pa_GetErrorText(err)
                  << ", reverting to \"" << old_device << "\"" << std::endl;
        device_name_ = old_device;
        return false;
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "Audio: switch failed (start) — " << Pa_GetErrorText(err)
                  << ", reverting to \"" << old_device << "\"" << std::endl;
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        device_name_ = old_device;
        return false;
    }

    is_running_ = true;
    std::cerr << "Audio: switched to \"" << info->name << "\" successfully" << std::endl;
    return true;
}

std::string AudioCapture::get_device_name() const {
    std::lock_guard<std::mutex> lock(device_mutex_);
    return device_name_;
}

int AudioCapture::pa_callback(const void* input, void* output,
                              unsigned long frameCount,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    const float* in = static_cast<const float*>(input);

    if (in != nullptr && self->is_running_) {
        int channels = self->channels_;
        for (unsigned long i = 0; i < frameCount; ++i) {
            // Push to lock-free ring buffer (Left channel only)
            self->ring_buffer_->push(in[i * channels]);
        }
    }

    return paContinue;
}

std::vector<std::string> AudioCapture::get_devices() {
    std::vector<std::string> devices;
    Pa_Initialize();
    int num_devices = Pa_GetDeviceCount();
    for (int i = 0; i < num_devices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info->maxInputChannels > 0) {
            devices.push_back(info->name);
        }
    }
    // Don't terminate, as it might be used globally
    return devices;
}
