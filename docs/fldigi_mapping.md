# fldigi Mapping Documentation

This document serves as the regression and audit trail for `cwdaemon`, mapping its functions and constants back to the original `fldigi` 4.2.x source code. 

## 1. Constants and Thresholds

| `cwdaemon` Entity | `fldigi` Source Location | Description |
|-------------------|--------------------------|-------------|
| `Config::tone_frequency = 600` | `cw.h` (GUI config) | Default tone frequency |
| `dot_len_samples_ = (12 * 8000 / 10) / wpm` | `cw.h:55` (`KWPM`) | Samples per dot (`KWPM = 12 * CW_SAMPLERATE/10`) |
| `dash_len_samples_ = dot_len * 3` | `morse.cxx` | Standard Morse timing |
| `TimingDecoder::update_tracking()` | `cw.cxx:191` | Adaptive WPM tracking (IIR moving average with 0.1/0.9 alpha) |
| `is_tone_present` condition (`> threshold`) | `cw.cxx` (rx_process) | Envelope threshold detection (50% AGC peak) |

## 2. DSP Algorithms

### 2.1 Envelope Detection and AGC
**cwdaemon**: `EnvelopeDetector::process()`, `get_agc_threshold()` (`src/goertzel.cpp`)
**fldigi**: `cw::rx_process()` (`src/cw/cw.cxx`)
- **Mapping Note**: `fldigi` uses an FFT/FIR low-pass filter pipeline (`rx_FFTprocess`/`rx_FIRprocess`) followed by moving-average windowing. `cwdaemon` implements an equivalent sliding window and fast-attack/slow-decay AGC mapping back to the logic in `cw::rx_process()` threshold computation.

### 2.2 Tone Extraction
**cwdaemon**: `Goertzel::process_sample()` (`src/goertzel.cpp`)
**fldigi**: `cw::rx_FFTprocess()`, `nco(double freq)` (`src/cw/cw.cxx`)
- **Mapping Note**: `cwdaemon` replaces the full `fftfilt` pipeline and mixer with a sliding Goertzel algorithm tuned to the target frequency. This acts identically for extracting tone magnitude as the FFT/mixer approach but is computationally lighter for a daemon. 

## 3. Timing and Discrimination

### 3.1 State Machine
**cwdaemon**: `RxState` enum and `TimingDecoder::process()` switch statement (`src/timing.cpp`)
**fldigi**: `CW_RX_STATE` (`RS_IDLE`, `RS_IN_TONE`, `RS_AFTER_TONE`) (`src/include/cw.h:74`)
- **Mapping Note**: Identical state progression. A tone detection moves the state from `IDLE` to `IN_TONE`. Tone loss transitions to `AFTER_TONE`. 

### 3.2 Symbol Decoding
**cwdaemon**: `TimingDecoder::process()` (`src/timing.cpp`)
**fldigi**: `morse.cxx` character framing and `som_table` (`src/cw/cw.cxx:94`)
- **Mapping Note**: 
  - Dot detection: `duration > (dot_tracking / 2) && duration < (dot_tracking * 2.0)`
  - Dash detection: `duration > (dot_tracking * 2.0)`
  - Character Space: `space_duration > (dot_tracking * 2.5)`
  - Word Space: `space_duration > (dot_tracking * 5.0)`
  - These match the `two_dots` and spacing logic of fldigi exactly.

### 3.3 Morse Code Table
**cwdaemon**: `morse_table` mapping (`src/timing.cpp`)
**fldigi**: `som_table[]` definition (`src/cw/cw.cxx:193` & `morse.cxx`)
- **Mapping Note**: The ASCII-to-Morse and Morse-to-ASCII mappings match the standardized table defined in `fldigi` for standard international Morse.

## 4. Architectural Equivalents

### 4.1 Audio Buffering
**cwdaemon**: Lock-free `RingBuffer` (`src/include/ringbuffer.hpp`)
**fldigi**: `mbuffer<double>` implementation (`src/include/cw.h:126`)
- **Mapping Note**: `fldigi` relies on a thread-safe `mbuffer` structure for audio passing between the PortAudio callback and the DSP thread. `cwdaemon` explicitly implements this as a C++23 atomic `RingBuffer`.
