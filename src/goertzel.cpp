// cwdaemon DSP module — faithful reimplementation of fldigi cw.cxx
// fldigi source: src/cw_rtty/cw.cxx (rx_FFTprocess, decode_stream, mixer)
// fldigi source: src/include/misc.h (decayavg, clamp)
// fldigi source: src/include/cw.h (constants)

#include "goertzel.hpp"
#include <cmath>
#include <iostream>

// =========================================================================
// CWDsp constructor
// Initializes the signal processing pipeline to match fldigi's CW modem.
//
// fldigi architecture at 8000 Hz:
//   1. mixer() shifts the CW tone to baseband (complex NCO)
//   2. cw_FFT_filter (overlap-save FFT bandpass) filters the baseband signal
//   3. Decimation by DEC_RATIO (16) → effective rate 500 Hz
//   4. bitfilter (moving average) smooths the envelope
//   5. decode_stream() applies AGC, computes adaptive thresholds, and
//      fires KEYDOWN/KEYUP/QUERY events
//
// For native sample rates other than 8000 (e.g. 48000), we first
// decimate to 8000 Hz, then apply the fldigi pipeline identically.
// =========================================================================

CWDsp::CWDsp(int native_sample_rate, double cw_frequency, double bandwidth, int speed_wpm)
    // fldigi: cw.cxx:356 — bandwidth filter: 1.0 * bandwidth / samplerate
    // For matched filter: bandwidth = 5.0 * speed / 1.2
    : bpf_(bandwidth, CW_SAMPLERATE, 127),
      // fldigi: cw.cxx:358-359 — bitfilter length = symbollen / (2 * DEC_RATIO)
      // symbollen = samplerate * 1.2 / speed = 8000 * 1.2 / 18 ≈ 533
      // bitfilter len = 533 / 32 ≈ 16
      bitfilter_(std::max(1, static_cast<int>(CW_SAMPLERATE * 1.2 / speed_wpm) / (2 * DEC_RATIO))),
      phaseacc_(0.0),
      frequency_(cw_frequency),
      native_sr_(native_sample_rate),
      decimation_(native_sample_rate / CW_SAMPLERATE),
      dec_counter_(0),
      smpl_ctr_(0),
      dec_ratio_ctr_(0),
      // fldigi: cw.cxx:347, 376-377
      agc_peak_(1.0),
      noise_floor_(1.0),
      sig_avg_(0.0),
      metric_(0.0),
      upper_threshold_(0.5),
      lower_threshold_(0.5),
      cw_receive_state_(RS_IDLE),
      event_cb_(nullptr),
      event_user_(nullptr)
{
    if (decimation_ < 1) decimation_ = 1;
}

// =========================================================================
// mixer — fldigi: cw.cxx:576-585
// Frequency-shifts the input sample to baseband using a complex NCO
// =========================================================================
void CWDsp::mixer(double sample, double& out_i, double& out_q) {
    out_i = sample * std::cos(phaseacc_);
    out_q = sample * std::sin(phaseacc_);

    phaseacc_ += 2.0 * M_PI * frequency_ / CW_SAMPLERATE;
    if (phaseacc_ > 2.0 * M_PI) phaseacc_ -= 2.0 * M_PI;
}

// =========================================================================
// decode_stream — fldigi: cw.cxx:593-681
// Implements the full AGC, adaptive threshold, and hysteresis detector.
// This is called at the decimated rate (CW_SAMPLERATE / DEC_RATIO = 500 Hz).
// =========================================================================
void CWDsp::decode_stream(double value) {
    // fldigi: cw.cxx:599-608 — attack/decay constants
    // Using medium defaults: attack=200, decay=1000
    int attack = 200;
    int decay = 1000;

    // fldigi: cw.cxx:610 — sig_avg tracks signal average with slow decay
    sig_avg_ = decayavg(sig_avg_, value, decay);

    // fldigi: cw.cxx:612-617 — noise floor tracking
    if (value < sig_avg_) {
        if (value < noise_floor_)
            noise_floor_ = decayavg(noise_floor_, value, attack);
        else
            noise_floor_ = decayavg(noise_floor_, value, decay);
    }

    // fldigi: cw.cxx:618-623 — AGC peak tracking
    if (value > sig_avg_) {
        if (value > agc_peak_)
            agc_peak_ = decayavg(agc_peak_, value, attack);
        else
            agc_peak_ = decayavg(agc_peak_, value, decay);
    }

    // fldigi: cw.cxx:625-632 — normalize value by AGC peak
    double norm_noise = (agc_peak_ > 0) ? noise_floor_ / agc_peak_ : 0;
    double norm_sig   = (agc_peak_ > 0) ? sig_avg_ / agc_peak_ : 0;

    if (agc_peak_ > 0)
        value /= agc_peak_;
    else
        value = 0;

    // fldigi: cw.cxx:634-636 — metric (SNR-like display value)
    metric_ = 0.8 * metric_;
    if ((noise_floor_ > 1e-4) && (noise_floor_ < sig_avg_))
        metric_ += 0.2 * clamp_val(2.5 * (20.0 * std::log10(sig_avg_ / noise_floor_)), 0, 100);

    // fldigi: cw.cxx:638-641 — adaptive thresholds
    double diff = norm_sig - norm_noise;
    upper_threshold_ = norm_sig - 0.2 * diff;
    lower_threshold_ = norm_noise + 0.7 * diff;

    // fldigi: cw.cxx:649-656 — hysteresis tone detector
    // CRITICAL: read state from shared ptr (synced with TimingDecoder)
    CW_RX_STATE cur_state = rx_state_ptr_ ? *rx_state_ptr_ : cw_receive_state_;

    // Upward trend: tone starting
    if ((value > upper_threshold_) && (cur_state != RS_IN_TONE)) {
        if (event_cb_) event_cb_(1, smpl_ctr_, event_user_); // CW_KEYDOWN_EVENT
        // Re-read state after callback (TimingDecoder may have updated it)
        if (rx_state_ptr_) cw_receive_state_ = *rx_state_ptr_;
    }

    // Re-read for KEYUP check
    cur_state = rx_state_ptr_ ? *rx_state_ptr_ : cw_receive_state_;

    // Downward trend: tone stopping
    if ((value < lower_threshold_) && (cur_state == RS_IN_TONE)) {
        if (event_cb_) event_cb_(2, smpl_ctr_, event_user_); // CW_KEYUP_EVENT
        if (rx_state_ptr_) cw_receive_state_ = *rx_state_ptr_;
    }

    // fldigi: cw.cxx:658 — query for character completion
    if (event_cb_) event_cb_(3, smpl_ctr_, event_user_); // CW_QUERY_EVENT
    if (rx_state_ptr_) cw_receive_state_ = *rx_state_ptr_;
}

// =========================================================================
// process_sample — called once per native-rate audio sample
// Implements the full fldigi rx_FFTprocess pipeline:
//   decimate to 8kHz → mixer → bandpass → decimate by DEC_RATIO → bitfilter → decode_stream
// fldigi: cw.cxx:683-715
// =========================================================================
void CWDsp::process_sample(double sample) {
    // Stage 1: Decimate from native rate to CW_SAMPLERATE (8000 Hz)
    if (++dec_counter_ < decimation_) return;
    dec_counter_ = 0;

    // fldigi: cw.cxx:702 — increment sample counter (at 8kHz rate)
    ++smpl_ctr_;

    // fldigi: cw.cxx:690-694 — mixer to baseband
    double mix_i, mix_q;
    mixer(sample, mix_i, mix_q);

    // fldigi: cw.cxx:696 — bandpass filter
    double filtered = bpf_.run(mix_i, mix_q);

    // fldigi: cw.cxx:704 — decimate by DEC_RATIO (16)
    if (++dec_ratio_ctr_ < DEC_RATIO) return;
    dec_ratio_ctr_ = 0;

    // fldigi: cw.cxx:707-708 — demodulate + bitfilter (moving average)
    double smoothed = bitfilter_.run(filtered);

    // fldigi: cw.cxx:710 — feed into decode_stream
    decode_stream(smoothed);
}
