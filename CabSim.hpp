#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <AudioToolbox/AudioToolbox.h>
#include <Accelerate/Accelerate.h>
#include "dsp/Biquad.hpp"

namespace Amp5650 {

class CabSim {
public:
    bool enabled = false;
    int currentMic = 0;       // 0: SM57, 1: MD421, 2: R-121
    float posX = 0.25f;       // 0.0 = Cap Center (Bright/Bite), 1.0 = Cone Edge (Dark/Warm)
    float distanceZ = 0.15f;  // 0.0 = Right on Grille (Proximity Bass), 1.0 = Far
    bool is45Degrees = false; // Off-axis tilt switch (smooths harsh top)
    float lowCutHz = 75.0f;   // Highpass filter (20 - 250 Hz)
    float highCutHz = 11500.0f; // Lowpass filter (4000 - 20000 Hz)
    float outputLevel = 1.0f;

    std::string customIrPath;
    std::string lastError;

    CabSim() {
        applyLoadedIR({1.0f}, {1.0f});
    }

    void prepare(float sampleRate) {
        sr = sampleRate;
        if (!customIrPath.empty()) {
            const bool wasEnabled = enabled;
            if (!loadIR(customIrPath)) enabled = false;
            else enabled = wasEnabled;
        }
        updateFilters();
    }

    bool loadIR(const std::string& path) {
        if (path.size() < 4 || (path.substr(path.size() - 4) != ".wav" &&
                               path.substr(path.size() - 4) != ".WAV")) {
            lastError = "Selecciona una IR WAV: " + path;
            return false;
        }
        std::vector<float> left, right;
        float fileRate = 0;
        if (!loadWavIR(path, left, right, fileRate) || left.empty()) {
            lastError = "No se pudo cargar la IR WAV: " + path;
            return false;
        }
        for (const auto* samples : {&left, &right}) {
            if (!std::all_of(samples->begin(), samples->end(), [](float x) { return std::isfinite(x); })) {
                lastError = "La IR contiene muestras no válidas: " + path;
                return false;
            }
        }
        applyLoadedIR(resampleIR(left, fileRate, sr), resampleIR(right, fileRate, sr));
        customIrPath = path;
        enabled = true;
        lastError.clear();
        return true;
    }

    void setMicrophone(int micIndex) {
        currentMic = std::clamp(micIndex, 0, 2);
        updateFilters();
    }

    void setMicPosition(float x, float z, bool angle45) {
        posX = std::clamp(x, 0.0f, 1.0f);
        distanceZ = std::clamp(z, 0.0f, 1.0f);
        is45Degrees = angle45;
        updateFilters();
    }

    void setFilters(float lowCut, float highCut) {
        lowCutHz = std::clamp(lowCut, 20.0f, 300.0f);
        highCutHz = std::clamp(highCut, 3000.0f, 20000.0f);
        updateFilters();
    }

    void setEnabled(bool isEn) {
        enabled = isEn && !customIrPath.empty();
    }

    // Process stereo audio in-place
    inline void processBlock(float* left, float* right, size_t numFrames) {
        if (!enabled) return;

        for (size_t i = 0; i < numFrames; ++i) {
            float inL = left[i];
            float inR = right[i];

            // 1. Mic Voicing EQ & Positioning Pre-Filter
            inL = micFilterL.process(inL);
            inL = posFilterL.process(inL);
            inL = proximityFilterL.process(inL);
            if (is45Degrees) inL = angleFilterL.process(inL);

            inR = micFilterR.process(inR);
            inR = posFilterR.process(inR);
            inR = proximityFilterR.process(inR);
            if (is45Degrees) inR = angleFilterR.process(inR);

            // 2. Studio Highpass & Lowpass cuts
            inL = hpFilterL.process(inL);
            inL = lpFilterL.process(inL);

            inR = hpFilterR.process(inR);
            inR = lpFilterR.process(inR);

            // 3. Zero-Latency Direct FIR Convolution with Cabinet Impulse Response
            const size_t length = irL.size();
            historyL[histIdx] = historyL[histIdx + length] = inL;
            historyR[histIdx] = historyR[histIdx + length] = inR;

            float convL = 0.0f;
            float convR = 0.0f;
            vDSP_dotpr(historyL.data() + histIdx + 1, 1, irL.data(), 1, &convL, length);
            vDSP_dotpr(historyR.data() + histIdx + 1, 1, irR.data(), 1, &convR, length);

            if (++histIdx >= (int)length) histIdx = 0;

            left[i]  = convL * outputLevel;
            right[i] = convR * outputLevel;
        }
    }

private:
    float sr = 48000.0f;

    std::vector<float> irL;
    std::vector<float> irR;
    std::vector<float> historyL;
    std::vector<float> historyR;
    int histIdx = 0;

    // Mic voicing filters
    Biquad micFilterL, micFilterR;
    Biquad posFilterL, posFilterR;
    Biquad proximityFilterL, proximityFilterR;
    Biquad angleFilterL, angleFilterR;
    Biquad hpFilterL, hpFilterR;
    Biquad lpFilterL, lpFilterR;

    void updateFilters() {
        // 1. Microphone Model Voicing
        switch (currentMic) {
            case 0: // Shure SM57: Realce icónico en 4.2 kHz, ataque cortante en mezcla
                micFilterL.setup(FilterType::Peaking, 4200.0f, 0.85f, 3.8f, sr);
                micFilterR.setup(FilterType::Peaking, 4200.0f, 0.85f, 3.8f, sr);
                break;
            case 1: // Sennheiser MD421: Cuerpo masivo en graves y medios-altos articulados
                micFilterL.setup(FilterType::Peaking, 3500.0f, 0.70f, 2.2f, sr);
                micFilterR.setup(FilterType::Peaking, 3500.0f, 0.70f, 2.2f, sr);
                break;
            case 2: // Royer R-121 Ribbon: Calidez de cinta, cero estridencia, gran densidad
                micFilterL.setup(FilterType::Highshelf, 5000.0f, 0.707f, -4.5f, sr);
                micFilterR.setup(FilterType::Highshelf, 5000.0f, 0.707f, -4.5f, sr);
                break;
        }

        // 2. Posición X (Centro <-> Borde del cono)
        // En X=0: Haz directo brillante. En X=1: Borde oscuro y cálido
        float edgeAttenuation = -posX * 8.5f;
        posFilterL.setup(FilterType::Highshelf, 3400.0f, 0.75f, edgeAttenuation, sr);
        posFilterR.setup(FilterType::Highshelf, 3400.0f, 0.75f, edgeAttenuation, sr);

        // 3. Distancia Z (Efecto de proximidad)
        float proxBoost = (1.0f - distanceZ) * 4.5f;
        proximityFilterL.setup(FilterType::Lowshelf, 120.0f, 0.707f, proxBoost, sr);
        proximityFilterR.setup(FilterType::Lowshelf, 120.0f, 0.707f, proxBoost, sr);

        // 4. Inclinación 45 Grados (Off-axis suaviza agudos hirientes en 4.8 kHz)
        if (is45Degrees) {
            angleFilterL.setup(FilterType::Peaking, 4800.0f, 1.4f, -4.0f, sr);
            angleFilterR.setup(FilterType::Peaking, 4800.0f, 1.4f, -4.0f, sr);
        } else {
            angleFilterL.setup(FilterType::Peaking, 4800.0f, 1.0f, 0.0f, sr);
            angleFilterR.setup(FilterType::Peaking, 4800.0f, 1.0f, 0.0f, sr);
        }

        // 5. Filtros de corte de estudio
        hpFilterL.setup(FilterType::Highpass, lowCutHz, 0.707f, 0.0f, sr);
        hpFilterR.setup(FilterType::Highpass, lowCutHz, 0.707f, 0.0f, sr);

        lpFilterL.setup(FilterType::Lowpass, highCutHz, 0.707f, 0.0f, sr);
        lpFilterR.setup(FilterType::Lowpass, highCutHz, 0.707f, 0.0f, sr);

    }

    bool loadWavIR(const std::string& path, std::vector<float>& left,
                   std::vector<float>& right, float& rate) {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8*>(path.data()), path.size(), false);
        if (!url) return false;
        ExtAudioFileRef file = nullptr;
        const OSStatus opened = ExtAudioFileOpenURL(url, &file);
        CFRelease(url);
        if (opened != noErr) return false;
        AudioFileID audioFile = nullptr;
        AudioFileTypeID type = 0;
        UInt32 typeSize = sizeof(audioFile);
        const bool isWav = ExtAudioFileGetProperty(file, kExtAudioFileProperty_AudioFile,
            &typeSize, &audioFile) == noErr;
        typeSize = sizeof(type);
        const bool wavType = isWav && AudioFileGetProperty(audioFile, kAudioFilePropertyFileFormat,
            &typeSize, &type) == noErr && type == kAudioFileWAVEType;
        if (!wavType) { ExtAudioFileDispose(file); return false; }
        AudioStreamBasicDescription format{};
        SInt64 frames = 0;
        UInt32 size = sizeof(format);
        bool valid = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &format) == noErr;
        size = sizeof(frames);
        valid = valid && ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size, &frames) == noErr;
        // shortcut: reject IRs over two seconds; use partitioned convolution before supporting longer files.
        valid = valid && format.mSampleRate >= 22050 && format.mSampleRate <= 192000 &&
            (format.mChannelsPerFrame == 1 || format.mChannelsPerFrame == 2) && frames > 0 &&
            frames <= format.mSampleRate * 2;
        if (!valid) { ExtAudioFileDispose(file); return false; }
        AudioStreamBasicDescription client{};
        client.mSampleRate = format.mSampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        client.mChannelsPerFrame = format.mChannelsPerFrame;
        client.mBitsPerChannel = 32;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = client.mBytesPerPacket = 4 * client.mChannelsPerFrame;
        valid = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client) == noErr;
        std::vector<float> interleaved(static_cast<size_t>(frames) * client.mChannelsPerFrame);
        AudioBufferList buffer{};
        buffer.mNumberBuffers = 1;
        buffer.mBuffers[0] = {client.mChannelsPerFrame,
            static_cast<UInt32>(interleaved.size() * sizeof(float)), interleaved.data()};
        UInt32 actual = static_cast<UInt32>(frames);
        valid = valid && ExtAudioFileRead(file, &actual, &buffer) == noErr && actual == frames;
        ExtAudioFileDispose(file);
        if (!valid) return false;
        left.resize(actual); right.resize(actual);
        for (size_t i = 0; i < actual; ++i) {
            left[i] = interleaved[i * client.mChannelsPerFrame];
            right[i] = interleaved[i * client.mChannelsPerFrame + client.mChannelsPerFrame - 1];
        }
        rate = static_cast<float>(format.mSampleRate);
        return true;
    }

    static std::vector<float> resampleIR(const std::vector<float>& input, float from, float to) {
        if (from == to) return input;
        const size_t count = static_cast<size_t>(std::ceil(input.size() * double(to) / from));
        std::vector<float> output(count);
        const double cutoff = std::min(1.0, double(to) / from);
        for (size_t i = 0; i < count; ++i) {
            const double position = i * double(from) / to;
            double sum = 0;
            for (int tap = -32; tap <= 32; ++tap) {
                const auto index = static_cast<int64_t>(std::floor(position)) + tap;
                const double distance = position - index;
                if (index < 0 || size_t(index) >= input.size() || std::abs(distance) >= 32) continue;
                const double phase = M_PI * distance * cutoff;
                const double sinc = std::abs(phase) < 1e-12 ? 1 : std::sin(phase) / phase;
                const double window = .5 + .5 * std::cos(M_PI * distance / 32);
                sum += input[index] * cutoff * sinc * window;
            }
            output[i] = static_cast<float>(sum * from / to);
        }
        return output;
    }

    void applyLoadedIR(const std::vector<float>& left, const std::vector<float>& right) {
        irL.assign(left.rbegin(), left.rend());
        irR.assign(right.rbegin(), right.rend());
        historyL.assign(2 * irL.size(), 0);
        historyR.assign(2 * irR.size(), 0);
        histIdx = 0;
    }
};

} // namespace Amp5650
