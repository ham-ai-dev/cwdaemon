#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <cstdint>

struct WavHeader {
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    int wav_size = 0;
    char wave_header[4] = {'W', 'A', 'V', 'E'};
    char fmt_header[4] = {'f', 'm', 't', ' '};
    int fmt_chunk_size = 16;
    short audio_format = 1;
    short num_channels = 1;
    int sample_rate = 8000;
    int byte_rate = 16000;
    short sample_alignment = 2;
    short bit_depth = 16;
    char data_header[4] = {'d', 'a', 't', 'a'};
    int data_bytes = 0;
};

static const std::map<char, std::string> char_to_morse = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
    {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
    {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."}, {'0', "-----"}, {' ', " "}
};

void generate_tone(std::vector<int16_t>& buffer, int duration_ms, int freq, int sample_rate) {
    int num_samples = (duration_ms * sample_rate) / 1000;
    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float val = std::sin(2.0f * M_PI * freq * t);
        // Simple raised cosine envelope for clicks reduction
        if (i < 100) val *= (1.0f - std::cos(M_PI * i / 100.0f)) / 2.0f;
        if (i > num_samples - 100) val *= (1.0f - std::cos(M_PI * (num_samples - i) / 100.0f)) / 2.0f;
        buffer.push_back(static_cast<int16_t>(val * 30000));
    }
}

void generate_silence(std::vector<int16_t>& buffer, int duration_ms, int sample_rate) {
    int num_samples = (duration_ms * sample_rate) / 1000;
    for (int i = 0; i < num_samples; ++i) {
        buffer.push_back(0);
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <output.wav> <WPM> <Text...>" << std::endl;
        return 1;
    }

    std::string out_file = argv[1];
    int wpm = std::stoi(argv[2]);
    std::string text = argv[3];

    for (int i = 4; i < argc; ++i) {
        text += " ";
        text += argv[i];
    }

    int sample_rate = 8000;
    int tone_freq = 600;
    
    // dot length in ms = 1200 / WPM
    int dot_ms = 1200 / wpm;
    int dash_ms = dot_ms * 3;
    int ele_space_ms = dot_ms;
    int char_space_ms = dot_ms * 3;
    int word_space_ms = dot_ms * 7;

    std::vector<int16_t> audio;

    for (char c : text) {
        c = std::toupper(c);
        if (c == ' ') {
            generate_silence(audio, word_space_ms - char_space_ms, sample_rate);
            continue;
        }

        auto it = char_to_morse.find(c);
        if (it != char_to_morse.end()) {
            std::string morse = it->second;
            for (size_t i = 0; i < morse.length(); ++i) {
                if (morse[i] == '.') {
                    generate_tone(audio, dot_ms, tone_freq, sample_rate);
                } else if (morse[i] == '-') {
                    generate_tone(audio, dash_ms, tone_freq, sample_rate);
                }
                if (i < morse.length() - 1) {
                    generate_silence(audio, ele_space_ms, sample_rate);
                }
            }
            generate_silence(audio, char_space_ms, sample_rate);
        }
    }

    WavHeader header;
    header.data_bytes = audio.size() * sizeof(int16_t);
    header.wav_size = header.data_bytes + 36;

    std::ofstream file(out_file, std::ios::binary);
    file.write(reinterpret_cast<char*>(&header), sizeof(WavHeader));
    file.write(reinterpret_cast<char*>(audio.data()), header.data_bytes);

    std::cout << "Generated " << out_file << " (" << text.length() << " chars, " << wpm << " WPM)\n";
    return 0;
}
