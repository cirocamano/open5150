#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <sys/stat.h>
#include <cstdlib>
#include <filesystem>
#include "RingBuffer.hpp"

namespace Amp5650 {

#pragma pack(push, 1)
struct WavRiffHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0; // 36 + dataSize
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4]  = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 3; // 3 = IEEE 32-bit Float
    uint16_t numChannels = 2; // Stereo
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 48000 * 2 * sizeof(float);
    uint16_t blockAlign = 2 * sizeof(float);
    uint16_t bitsPerSample = 32;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
#pragma pack(pop)

class AudioRecorder {
public:
    std::atomic<bool> isRecording{false};
    std::atomic<double> recordedSeconds{0.0};

    AudioRecorder() : recordQueue(524288) { // 524,288 samples cushion (~5.5 sec)
        interleavedScratch.resize(4096);
    }

    ~AudioRecorder() {
        stopRecording();
    }

    void setSampleRate(double sr) {
        sampleRate = (uint32_t)sr;
    }

    static std::string getDefaultRecordingsDirectory() {
        const char* userHome = std::getenv("HOME");
        std::string dir = userHome ? std::string(userHome) + "/Music/OPEN5150/Recordings" : "recordings";
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        return dir;
    }

    static std::string generateTimestampedPath() {
        std::string dir = getDefaultRecordingsDirectory();
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_r(&tt, &tm_buf);

        std::stringstream ss;
        ss << dir << "/open5150_take_"
           << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
           << ".wav";
        return ss.str();
    }

    bool startRecording(const std::string& destinationPath = "") {
        if (isRecording.load()) return true;

        if (destinationPath.empty()) {
            currentFilePath = generateTimestampedPath();
        } else {
            currentFilePath = destinationPath;
            size_t slash = currentFilePath.find_last_of("/\\");
            if (slash != std::string::npos) {
                std::string parent = currentFilePath.substr(0, slash);
                mkdir(parent.c_str(), 0755);
            }
        }

        // Open WAV file
        wavFile.open(currentFilePath, std::ios::binary | std::ios::trunc);
        if (!wavFile.is_open()) return false;

        // Write placeholder header
        WavRiffHeader hdr;
        hdr.sampleRate = sampleRate;
        hdr.byteRate = sampleRate * 2 * sizeof(float);
        wavFile.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

        totalSamplesWritten = 0;
        recordedSeconds.store(0.0, std::memory_order_relaxed);
        recordQueue.reset();

        stopRequested.store(false, std::memory_order_relaxed);
        isRecording.store(true, std::memory_order_release);

        // Start asynchronous disk writer thread
        writerThread = std::thread(&AudioRecorder::writerWorker, this);
        return true;
    }

    void stopRecording() {
        if (!isRecording.load()) return;

        isRecording.store(false, std::memory_order_release);
        stopRequested.store(true, std::memory_order_release);

        if (writerThread.joinable()) {
            writerThread.join();
        }

        lastRecordedPath = currentFilePath;
    }

    std::string getLastRecordingPath() const {
        return lastRecordedPath;
    }

    std::string getCurrentRecordingPath() const {
        return currentFilePath;
    }

    // Called on Real-Time CoreAudio Thread (Zero locks, zero disk I/O)
    inline void process(const float* left, const float* right, size_t numFrames) {
        if (!isRecording.load(std::memory_order_relaxed)) return;

        size_t totalSamples = numFrames * 2;
        if (interleavedScratch.size() < totalSamples) {
            interleavedScratch.resize(totalSamples);
        }

        for (size_t i = 0; i < numFrames; ++i) {
            interleavedScratch[2 * i]     = left[i];
            interleavedScratch[2 * i + 1] = right[i];
        }

        recordQueue.write(interleavedScratch.data(), totalSamples);
    }

private:
    uint32_t sampleRate = 48000;
    SPSCQueue<float> recordQueue;
    std::vector<float> interleavedScratch;

    std::ofstream wavFile;
    std::string currentFilePath;
    std::string lastRecordedPath;
    uint32_t totalSamplesWritten = 0;

    std::thread writerThread;
    std::atomic<bool> stopRequested{false};

    void writerWorker() {
        std::vector<float> batch(8192);

        while (!stopRequested.load(std::memory_order_acquire) || recordQueue.availableRead() > 0) {
            size_t available = recordQueue.availableRead();
            if (available > 0) {
                size_t toRead = std::min(available, batch.size());
                size_t readCount = recordQueue.read(batch.data(), toRead);

                if (readCount > 0 && wavFile.is_open()) {
                    wavFile.write(reinterpret_cast<const char*>(batch.data()), readCount * sizeof(float));
                    totalSamplesWritten += readCount;

                    // Update live duration (stereo = 2 samples per frame)
                    double dur = (double)(totalSamplesWritten / 2) / (double)sampleRate;
                    recordedSeconds.store(dur, std::memory_order_relaxed);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // Finalize RIFF header
        if (wavFile.is_open()) {
            uint32_t dataBytes = totalSamplesWritten * sizeof(float);
            uint32_t fileBytes = 36 + dataBytes;

            wavFile.seekp(4, std::ios::beg);
            wavFile.write(reinterpret_cast<const char*>(&fileBytes), 4);

            wavFile.seekp(40, std::ios::beg);
            wavFile.write(reinterpret_cast<const char*>(&dataBytes), 4);

            wavFile.flush();
            wavFile.close();
        }
    }
};

} // namespace Amp5650
