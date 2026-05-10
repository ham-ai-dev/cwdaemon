#pragma once

#include <string>
#include <memory>
#include <thread>
#include <drogon/drogon.h>
#include "audio.hpp"
#include "rigctl.hpp"

class ApiServer {
public:
    ApiServer(std::shared_ptr<AudioCapture> audio, std::shared_ptr<RigCtl> rigctl);
    ~ApiServer();

    void start(int port);
    void stop();

    // Broadcast helpers for WebSockets
    static void broadcast_decoded_char(char c);
    static void broadcast_metrics(float snr, float wpm, float freq, float agc);

    // Global state pointers for endpoints
    static std::shared_ptr<AudioCapture> audio_sys;
    static std::shared_ptr<RigCtl> rigctl_sys;
    static std::atomic<float> current_wpm;
    static std::atomic<float> current_snr;
    static std::atomic<float> current_freq;

private:
    std::thread thread_;
    bool is_running_;
};
