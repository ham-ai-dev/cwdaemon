#pragma once
// cwdaemon DSP module — faithful reimplementation of fldigi cw.cxx
// fldigi: cw.cxx rx_FFTprocess(), decode_stream()
//
// Signal path matches fldigi exactly:
//   audio sample → mixer (freq shift to baseband) → FFT bandpass filter
//   → decimate by DEC_RATIO(16) → bitfilter (moving avg) → decode_stream
//
// decode_stream implements fldigi's AGC with decayavg(), dual adaptive
// thresholds (upper/lower), and hysteresis tone detector.

#include <cmath>
#include <vector>
#include <array>
#include <cstring>

// fldigi: cw.h:41
static constexpr int CW_SAMPLERATE = 8000;
// fldigi: cw.h:55  — KWPM = 12 * CW_SAMPLERATE / 10
static constexpr int KWPM = 12 * CW_SAMPLERATE / 10;  // = 9600
// fldigi: cw.h:90
static constexpr int DEC_RATIO = 16;
// fldigi: cw.h:81
static constexpr int CW_FFT_SIZE = 2048;

// fldigi: misc.h:59 — decayavg
inline double decayavg(double average, double input, int weight) {
    if (weight <= 1) return input;
    return ((input - average) / static_cast<double>(weight)) + average;
}

inline double clamp_val(double x, double mn, double mx) {
    return (x < mn) ? mn : ((x > mx) ? mx : x);
}

// =========================================================================
// Moving average filter — matches fldigi filters.h Cmovavg
// =========================================================================
class MovingAverage {
public:
    explicit MovingAverage(int len) : len_(len), ptr_(0), out_(0.0), empty_(true) {
        buf_.resize(len, 0.0);
    }

    double run(double a) {
        if (empty_) {
            empty_ = false;
            for (int i = 0; i < len_; i++) buf_[i] = a;
            out_ = a * len_;
            ptr_ = 0;
            return a;
        }
        out_ -= buf_[ptr_];
        buf_[ptr_] = a;
        out_ += a;
        ptr_ = (ptr_ + 1) % len_;
        return out_ / len_;
    }

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        out_ = 0.0;
        ptr_ = 0;
        empty_ = true;
    }

    double value() const { return out_ / (len_ > 0 ? len_ : 1); }

    void set_length(int len) {
        len_ = len;
        buf_.resize(len, 0.0);
        reset();
    }

private:
    int len_;
    int ptr_;
    double out_;
    bool empty_;
    std::vector<double> buf_;
};

// =========================================================================
// FFT-based bandpass filter (overlap-save) — simplified from fldigi fftfilt
// For CW we only need a low-pass filter on the baseband-shifted signal.
// We implement this as a frequency-domain FIR using real FFT.
// =========================================================================

// For simplicity and zero-dependency, we use a time-domain FIR filter
// with a sinc-windowed (Blackman) lowpass kernel, which is mathematically
// equivalent to fldigi's fftfilt overlap-save for our single-channel case.
class BandpassFilter {
public:
    BandpassFilter(double bandwidth_hz, double sample_rate, int fir_len = 127)
        : fir_len_(fir_len), ptr_(0), sample_rate_(sample_rate)
    {
        h_.resize(fir_len_, 0.0);
        buf_i_.resize(fir_len_, 0.0);
        buf_q_.resize(fir_len_, 0.0);
        create_lpf(bandwidth_hz / sample_rate);
    }

    void create_lpf(double fc) {
        // Windowed sinc LPF, matches fldigi fftfilt::create_lpf logic
        int mid = fir_len_ / 2;
        for (int i = 0; i < fir_len_; i++) {
            double x = i - mid;
            double sinc_val = (std::fabs(x) < 1e-10) ? 2.0 * fc
                : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
            // Blackman window — same as fldigi fftfilt::_blackman
            double win = 0.42 - 0.50 * std::cos(2.0 * M_PI * i / fir_len_)
                              + 0.08 * std::cos(4.0 * M_PI * i / fir_len_);
            h_[i] = sinc_val * win;
        }
    }

    // Process one complex sample (I, Q), return magnitude
    double run(double in_i, double in_q) {
        buf_i_[ptr_] = in_i;
        buf_q_[ptr_] = in_q;

        double sum_i = 0.0, sum_q = 0.0;
        int idx = ptr_;
        for (int i = 0; i < fir_len_; i++) {
            sum_i += h_[i] * buf_i_[idx];
            sum_q += h_[i] * buf_q_[idx];
            if (--idx < 0) idx = fir_len_ - 1;
        }

        ptr_ = (ptr_ + 1) % fir_len_;
        return std::sqrt(sum_i * sum_i + sum_q * sum_q);
    }

private:
    int fir_len_;
    int ptr_;
    double sample_rate_;
    std::vector<double> h_;
    std::vector<double> buf_i_;
    std::vector<double> buf_q_;
};

// =========================================================================
// CW RX State — fldigi: cw.h:74
// =========================================================================
enum CW_RX_STATE {
    RS_IDLE = 0,
    RS_IN_TONE,
    RS_AFTER_TONE
};

// =========================================================================
// CWDsp — full fldigi-faithful CW receive DSP pipeline
// =========================================================================
class CWDsp {
public:
    // callback: called with CW_KEYDOWN_EVENT(1), CW_KEYUP_EVENT(2), CW_QUERY_EVENT(3)
    // and the current sample counter
    using EventCallback = void(*)(int event, unsigned int smpl_ctr, void* user);

    CWDsp(int native_sample_rate, double cw_frequency, double bandwidth, int speed_wpm);

    // Feed one audio sample at native sample rate.
    // Internally downsamples, filters, and calls the event callback.
    void process_sample(double sample);

    // Getters for decode_stream state
    double get_agc_peak() const { return agc_peak_; }
    double get_noise_floor() const { return noise_floor_; }
    double get_sig_avg() const { return sig_avg_; }
    double get_metric() const { return metric_; }
    double get_upper_threshold() const { return upper_threshold_; }
    double get_lower_threshold() const { return lower_threshold_; }
    CW_RX_STATE get_rx_state() const { return cw_receive_state_; }
    unsigned int get_smpl_ctr() const { return smpl_ctr_; }

    void set_event_callback(EventCallback cb, void* user) {
        event_cb_ = cb;
        event_user_ = user;
    }

    void set_frequency(double freq) { frequency_ = freq; }

    // Share rx state with TimingDecoder — in fldigi both live in the cw class
    void set_rx_state_ptr(CW_RX_STATE* ptr) { rx_state_ptr_ = ptr; }
    void set_rx_state(CW_RX_STATE s) {
        cw_receive_state_ = s;
        if (rx_state_ptr_) *rx_state_ptr_ = s;
    }

private:
    // fldigi: cw.cxx:576 mixer()
    void mixer(double sample, double& out_i, double& out_q);

    // fldigi: cw.cxx:593 decode_stream()
    void decode_stream(double value);

    // fldigi signal path components
    BandpassFilter bpf_;
    MovingAverage bitfilter_;

    // Mixer state — fldigi: cw.cxx phaseacc
    double phaseacc_;
    double frequency_;

    // Sample rate management
    int native_sr_;        // e.g. 48000
    int decimation_;       // native_sr_ / CW_SAMPLERATE (e.g. 6)
    int dec_counter_;      // counter for first decimation stage

    // fldigi: cw.cxx smpl_ctr — counts at CW_SAMPLERATE (8000Hz)
    unsigned int smpl_ctr_;

    // fldigi DEC_RATIO decimation counter
    int dec_ratio_ctr_;

    // fldigi: decode_stream state
    double agc_peak_;
    double noise_floor_;
    double sig_avg_;
    double metric_;
    double upper_threshold_;
    double lower_threshold_;

    // fldigi: cw_receive_state — shared with TimingDecoder
    CW_RX_STATE cw_receive_state_;
    CW_RX_STATE* rx_state_ptr_ = nullptr; // points to TimingDecoder's state

    // Event callback
    EventCallback event_cb_;
    void* event_user_;
};
