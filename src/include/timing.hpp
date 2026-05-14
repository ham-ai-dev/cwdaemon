#pragma once
// cwdaemon timing discriminator — faithful reimplementation of fldigi
// fldigi: cw.cxx handle_event(), update_tracking(), sync_parameters()
// fldigi: cw.h constants: KWPM, CW_SAMPLERATE, TRACKING_FILTER_SIZE
// fldigi: morse.cxx — character lookup

#include <string>
#include <functional>
#include <cstdint>
#include "goertzel.hpp"  // for CW_SAMPLERATE, KWPM, DEC_RATIO, MovingAverage, CW_RX_STATE

// fldigi: cw.h:45 — increased from 6 to 9 to support prosigns
static constexpr int MAX_MORSE_ELEMENTS = 9;
// fldigi: cw.h:70
static constexpr int TRACKING_FILTER_SIZE = 16;

class TimingDecoder {
public:
    using CharCallback = std::function<void(const std::string&)>;

    TimingDecoder(int initial_wpm, CharCallback callback);

    // Called by the DSP module with events:
    //   1 = CW_KEYDOWN_EVENT
    //   2 = CW_KEYUP_EVENT
    //   3 = CW_QUERY_EVENT
    // Returns true if a character was decoded (on QUERY)
    bool handle_event(int event, unsigned int smpl_ctr, std::string& decoded);

    // Update the CW receive state (called from DSP layer)
    void set_rx_state(CW_RX_STATE state) { cw_receive_state_ = state; }
    CW_RX_STATE get_rx_state() const { return cw_receive_state_; }
    CW_RX_STATE* get_rx_state_ptr() { return &cw_receive_state_; }

    float get_wpm() const { return cw_receive_speed_; }
    int get_two_dots() const { return two_dots_; }

private:
    // fldigi: cw.cxx:524 update_tracking()
    void update_tracking(int dur_1, int dur_2);
    // fldigi: cw.cxx:466 sync_parameters()
    void sync_parameters();
    // Morse lookup — returns decoded string (supports prosigns)
    std::string lookup_morse(const std::string& repr);

    // fldigi: cw.cxx:754 usec_diff (sample diff)
    int sample_diff(unsigned int earlier, unsigned int later) {
        return (earlier >= later) ? 0 : static_cast<int>(later - earlier);
    }

    CharCallback on_char_;

    // fldigi: cw.h:120-122
    CW_RX_STATE cw_receive_state_;
    CW_RX_STATE old_cw_receive_state_;

    // fldigi: cw.h:132-136
    int cw_speed_;
    int cw_send_speed_;
    int cw_receive_speed_;

    // fldigi: cw.h:139-140
    int cw_upper_limit_;
    int cw_lower_limit_;

    // fldigi: cw.h:142
    int cw_noise_spike_threshold_;

    // fldigi: cw.h:146-148
    int cw_send_dot_length_;
    int cw_send_dash_length_;

    // fldigi: cw.h:162-163
    int cw_receive_dot_length_;
    int cw_receive_dash_length_;

    // fldigi: cw.h:166-169
    std::string rx_rep_buf_;
    unsigned int cw_rr_start_timestamp_;
    unsigned int cw_rr_end_timestamp_;

    // fldigi: cw.h:171
    int two_dots_;

    // fldigi: cw.h:175-176 (tracking via movavg instead of IIR)
    MovingAverage tracking_filter_;

    // Space-sent flag — fldigi: handle_event static
    bool space_sent_;
    // Last element length — fldigi: handle_event static
    int last_element_;

    // WPM bounds
    int wpm_lower_;
    int wpm_upper_;
};
