// cwdaemon timing discriminator — faithful reimplementation of fldigi
// fldigi: cw.cxx handle_event() lines 771-921
// fldigi: cw.cxx update_tracking() lines 524-536
// fldigi: cw.cxx sync_parameters() lines 466-517

#include "timing.hpp"
#include <map>
#include <iostream>
#include <cmath>

// =========================================================================
// Morse code lookup table — fldigi: morse.cxx rx_lookup
// =========================================================================
static const std::map<std::string, std::string> morse_table = {
    {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"},
    {"..-.", "F"}, {"--.", "G"}, {"....", "H"}, {"..", "I"}, {".---", "J"},
    {"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"}, {"---", "O"},
    {".--.", "P"}, {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"},
    {"..-", "U"}, {"...-", "V"}, {".--", "W"}, {"-..-", "X"}, {"-.--", "Y"},
    {"--..", "Z"},
    {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"},
    {"....-", "4"}, {".....", "5"}, {"-....", "6"}, {"--...", "7"},
    {"---..", "8"}, {"----.", "9"},
    {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"}, {"-.-.--", "!"},
    {"-....-", "-"}, {"-..-.", "/"}, {".--.-.", "@"}, {"-.--.", "("},
    {"-.--.-", ")"}, {"---...", ":"}, {"-.-.-.", ";"}, {"-...-", "<BT>"},
    {".-.-.", "<AR>"}, {".-...", "&"}, {"..--.-", "_"}, {".-..-.", "\""},
    {"...-..-", "$"},
    // =====================================================================
    // CW Prosigns — sent as merged characters without inter-character space
    // =====================================================================
    {"...-.-", "<SK>"},      // End of contact
    {"-.--." , "<KN>"},      // Go ahead, specific station only
    {".-...", "<AS>"},       // Wait / stand by
    {"...-.", "<SN>"},       // Understood / Verified
    {"...---...", "<SOS>"}, // Distress
};

// =========================================================================
// Constructor
// fldigi: cw::cw() lines 299-383
// =========================================================================
TimingDecoder::TimingDecoder(int initial_wpm, CharCallback callback)
    : on_char_(callback),
      cw_receive_state_(RS_IDLE),
      old_cw_receive_state_(RS_IDLE),
      cw_speed_(initial_wpm),
      cw_send_speed_(initial_wpm),
      cw_receive_speed_(initial_wpm),
      cw_upper_limit_(0),
      cw_lower_limit_(0),
      cw_noise_spike_threshold_(0),
      cw_send_dot_length_(0),
      cw_send_dash_length_(0),
      cw_receive_dot_length_(0),
      cw_receive_dash_length_(0),
      cw_rr_start_timestamp_(0),
      cw_rr_end_timestamp_(0),
      two_dots_(0),
      // fldigi: cw.cxx:363 — trackingfilter is Cmovavg(TRACKING_FILTER_SIZE=16)
      tracking_filter_(TRACKING_FILTER_SIZE),
      space_sent_(true),
      last_element_(0),
      wpm_lower_(5),
      wpm_upper_(50)
{
    // fldigi: cw.cxx:322-325
    two_dots_ = 2 * KWPM / cw_speed_;
    cw_noise_spike_threshold_ = two_dots_ / 4;
    cw_send_dot_length_ = KWPM / cw_send_speed_;
    cw_send_dash_length_ = 3 * cw_send_dot_length_;

    sync_parameters();
}

// =========================================================================
// sync_parameters — fldigi: cw.cxx:466-517
// Synchronize timing values from current speed settings
// =========================================================================
void TimingDecoder::sync_parameters() {
    // fldigi: cw.cxx:498-499
    cw_lower_limit_ = 2 * KWPM / wpm_upper_;
    cw_upper_limit_ = 2 * KWPM / wpm_lower_;

    // fldigi: cw.cxx:501-506 — adaptive tracking
    cw_receive_speed_ = KWPM / (two_dots_ / 2);

    // fldigi: cw.cxx:508-513
    if (cw_receive_speed_ > 0)
        cw_receive_dot_length_ = KWPM / cw_receive_speed_;
    else
        cw_receive_dot_length_ = KWPM / 5;

    cw_receive_dash_length_ = 3 * cw_receive_dot_length_;

    // fldigi: cw.cxx:515
    cw_noise_spike_threshold_ = cw_receive_dot_length_ / 2;
}

// =========================================================================
// update_tracking — fldigi: cw.cxx:524-536
// Called with a dot-dash or dash-dot pair to update adaptive speed.
// Uses MovingAverage(16) filter on the average of the pair.
// =========================================================================
void TimingDecoder::update_tracking(int dur_1, int dur_2) {
    // Use configurable WPM bounds instead of hardcoded 5/200 WPM
    // (fldigi hardcodes these — we make them configurable to support slow CW)
    int min_dot = KWPM / wpm_upper_;      // shortest valid dot at max WPM
    int max_dash = 3 * KWPM / wpm_lower_; // longest valid dash at min WPM

    // fldigi: cw.cxx:528-531 — sanity checks
    if ((dur_1 > dur_2) && (dur_1 > 4 * dur_2)) return;
    if ((dur_2 > dur_1) && (dur_2 > 4 * dur_1)) return;
    if (dur_1 < min_dot || dur_2 < min_dot) return;
    if (dur_1 > max_dash || dur_2 > max_dash) return;

    // fldigi: cw.cxx:533 — filter the average of the pair
    // The average of a dot+dash = (1+3)/2 = 2 dot lengths
    two_dots_ = static_cast<int>(tracking_filter_.run((dur_1 + dur_2) / 2.0));

    // fldigi: cw.cxx:535
    sync_parameters();
}

// =========================================================================
// handle_event — fldigi: cw.cxx:771-921
// The core CW decoder state machine. Receives KEYDOWN, KEYUP, and QUERY
// events and decodes Morse characters.
// =========================================================================
bool TimingDecoder::handle_event(int event, unsigned int smpl_ctr, std::string& decoded) {
    int element_usec; // sample count difference (not actually microseconds in our impl)

    switch (event) {
    // =====================================================================
    // CW_KEYDOWN_EVENT (1) — fldigi: cw.cxx:787-805
    // =====================================================================
    case 1: // CW_KEYDOWN_EVENT
        // Can only start a tone while idle or after a tone
        if (cw_receive_state_ == RS_IN_TONE)
            return false;

        // First tone in idle state: reset
        if (cw_receive_state_ == RS_IDLE) {
            rx_rep_buf_.clear();
        }

        // fldigi: cw.cxx:800 — save timestamp
        cw_rr_start_timestamp_ = smpl_ctr;

        // fldigi: cw.cxx:802-803
        old_cw_receive_state_ = cw_receive_state_;
        cw_receive_state_ = RS_IN_TONE;
        return false;

    // =====================================================================
    // CW_KEYUP_EVENT (2) — fldigi: cw.cxx:806-872
    // =====================================================================
    case 2: // CW_KEYUP_EVENT
        if (cw_receive_state_ != RS_IN_TONE)
            return false;

        // fldigi: cw.cxx:811-812 — measure tone duration
        cw_rr_end_timestamp_ = smpl_ctr;
        element_usec = sample_diff(cw_rr_start_timestamp_, cw_rr_end_timestamp_);

        // fldigi: cw.cxx:815
        sync_parameters();

        // fldigi: cw.cxx:818-822 — noise spike rejection
        if (cw_noise_spike_threshold_ > 0 && element_usec < cw_noise_spike_threshold_) {
            cw_receive_state_ = RS_IDLE;
            return false;
        }

        // fldigi: cw.cxx:832-843 — adaptive speed tracking on dot-dash pairs
        if (last_element_ > 0) {
            // Check for dot-dash sequence (current should be ~3x last)
            if ((element_usec > 2 * last_element_) &&
                (element_usec < 4 * last_element_)) {
                update_tracking(last_element_, element_usec);
            }
            // Check for dash-dot sequence (last should be ~3x current)
            if ((last_element_ > 2 * element_usec) &&
                (last_element_ < 4 * element_usec)) {
                update_tracking(element_usec, last_element_);
            }
        }
        last_element_ = element_usec;

        // fldigi: cw.cxx:847-855 — dit or dah?
        // A dot is anything shorter than two_dots (2 dot lengths)
        if (element_usec <= two_dots_) {
            rx_rep_buf_ += '.';
        } else {
            // A dash is anything longer than two_dots
            rx_rep_buf_ += '-';
        }

        // fldigi: cw.cxx:858-868 — overflow check
        if (rx_rep_buf_.length() > MAX_MORSE_ELEMENTS) {
            cw_receive_state_ = RS_IDLE;
            return false;
        }

        // fldigi: cw.cxx:870 — move to after-tone state
        cw_receive_state_ = RS_AFTER_TONE;
        return false;

    // =====================================================================
    // CW_QUERY_EVENT (3) — fldigi: cw.cxx:873-917
    // Called every decimated sample to check for character/word completion
    // =====================================================================
    case 3: // CW_QUERY_EVENT
        // Nothing to do if we're in a tone
        if (cw_receive_state_ == RS_IN_TONE)
            return false;

        // fldigi: cw.cxx:880-881 — compute silence duration
        sync_parameters();
        element_usec = sample_diff(cw_rr_end_timestamp_, smpl_ctr);

        // fldigi: cw.cxx:883-884 — SHORT silence: nothing to do yet
        if (element_usec < (2 * cw_receive_dot_length_))
            return false;

        // fldigi: cw.cxx:888-907 — MEDIUM silence: character space
        // Character space is between 2 and 4 dot lengths
        if (element_usec >= (2 * cw_receive_dot_length_) &&
            element_usec <= (4 * cw_receive_dot_length_) &&
            cw_receive_state_ == RS_AFTER_TONE) {

            // Look up the morse representation
            std::string s = lookup_morse(rx_rep_buf_);
            if (!s.empty()) {
                decoded = s;
                on_char_(s);
            } else {
                decoded = "*";
                on_char_("*");
            }

            rx_rep_buf_.clear();
            cw_receive_state_ = RS_IDLE;
            space_sent_ = false;
            return true;
        }

        // fldigi: cw.cxx:910-913 — LONG silence: word space
        if ((element_usec > (4 * cw_receive_dot_length_)) && !space_sent_) {
            decoded = " ";
            on_char_(" ");
            space_sent_ = true;
            return true;
        }

        return false;
    }

    return false;
}

// =========================================================================
// lookup_morse — returns decoded string (single char or prosign tag)
// Falls back to truncating trailing/leading noise elements if exact match fails,
// which recovers common timing errors (e.g. .-.- → .-. = R, --.-. → --.- = Q)
// =========================================================================
std::string TimingDecoder::lookup_morse(const std::string& repr) {
    // Exact match first
    auto it = morse_table.find(repr);
    if (it != morse_table.end()) {
        return it->second;
    }

    if (repr.length() > 1) {
        // Fallback 1: try removing last element (trailing noise)
        std::string trunc_end = repr.substr(0, repr.length() - 1);
        auto it2 = morse_table.find(trunc_end);
        if (it2 != morse_table.end()) {
            return it2->second;
        }

        // Fallback 2: try removing first element (leading noise)
        std::string trunc_start = repr.substr(1);
        auto it3 = morse_table.find(trunc_start);
        if (it3 != morse_table.end()) {
            return it3->second;
        }
    }

    return "";
}
