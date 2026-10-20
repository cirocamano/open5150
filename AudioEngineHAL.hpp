#pragma once
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <memory>
#include "AmpParameters.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

#include "dsp/Amp5650.hpp"
#include "NoiseGate.hpp"
#include "Limiter.hpp"
#include "CabSim.hpp"
#include "AudioRecorder.hpp"
#include "RingBuffer.hpp"
#include "AudioDeviceManager.hpp"

namespace Amp5650 {

// Helper to disable denormals on real-time audio threads
inline void setDenormalFlags() {
#if defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__arm64__) || defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1 << 24); // FZ: Flush to Zero
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}

struct MeterStats {
    std::atomic<float> peakIn{0.0f};
    std::atomic<float> rmsIn{0.0f};
    std::atomic<float> gateReductionDb{0.0f};
    std::atomic<bool> gateOpen{false};
    std::atomic<float> peakOut{0.0f};
    std::atomic<float> rmsOut{0.0f};
};

class LiveAudioEngine {
public:
    AudioDeviceID inputDeviceID = 0;
    AudioDeviceID outputDeviceID = 0;
    double sampleRate = 48000.0;
    UInt32 bufferSize = 64; // Default to ultra-low latency 64 samples (~1.33 ms at 48kHz)

    Amp5650Engine ampL;
    Amp5650Engine ampR;
    NoiseGate noiseGate;
    CabSim cabSim;
    AudioRecorder recorder;
    MasterLimiter limiterL;
    MasterLimiter limiterR;
    MeterStats meters;

    bool isRunning = false;

    LiveAudioEngine() : ringBufL(65536), ringBufR(65536) {
        inputDeviceID = AudioDeviceManager::getDefaultInputDeviceID();
        outputDeviceID = AudioDeviceManager::getDefaultOutputDeviceID();

        // Pre-allocate thread-isolated buffers (no allocations in callbacks!)
        inProcBufL.assign(4096, 0.0f);
        inProcBufR.assign(4096, 0.0f);
        outProcBufL.assign(4096, 0.0f);
        outProcBufR.assign(4096, 0.0f);
        singleProcBufL.assign(4096, 0.0f);
        singleProcBufR.assign(4096, 0.0f);
    }

    ~LiveAudioEngine() {
        stop();
    }

    bool start() {
        if (isRunning) return true;

        sampleRate = AudioDeviceManager::getSampleRate(outputDeviceID);
        if (sampleRate < 44100.0 || sampleRate > 192000.0) sampleRate = 48000.0;

        ampL.prepare((float)sampleRate);
        ampR.prepare((float)sampleRate);
        noiseGate.prepare((float)sampleRate);
        cabSim.prepare((float)sampleRate);
        activeCab = std::make_unique<CabSim>(cabSim);
        {
            std::lock_guard<std::mutex> lock(parameterMutex);
            pendingCab.reset();
            cabPending = false;
            if (!cabSim.lastError.empty()) parameters.cabEnabled = false;
            appliedVersion = 0;
        }
        recorder.setSampleRate(sampleRate);

        ringBufL.reset();
        ringBufR.reset();
        isPreRolled.store(false, std::memory_order_relaxed);
        lastConcealL = 0.0f;
        lastConcealR = 0.0f;

        // Check if devices are the same or different
        if (inputDeviceID == outputDeviceID && inputDeviceID != 0) {
            isSingleUnitMode = true;
            return startSingleUnit();
        } else {
            isSingleUnitMode = false;
            return startDualUnits();
        }
    }

    void stop() {
        if (!isRunning) return;
        isRunning = false;

        if (singleAU) {
            AudioOutputUnitStop(singleAU);
            AudioUnitUninitialize(singleAU);
            AudioComponentInstanceDispose(singleAU);
            singleAU = nullptr;
        }
        if (inputAU) {
            AudioOutputUnitStop(inputAU);
            AudioUnitUninitialize(inputAU);
            AudioComponentInstanceDispose(inputAU);
            inputAU = nullptr;
        }
        if (outputAU) {
            AudioOutputUnitStop(outputAU);
            AudioUnitUninitialize(outputAU);
            AudioComponentInstanceDispose(outputAU);
            outputAU = nullptr;
        }
    }

    LiveParameters getParameters() {
        std::lock_guard<std::mutex> lock(parameterMutex);
        return parameters;
    }

    void setNoiseGateThreshold(float db) {
        if (!std::isfinite(db)) return;
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.gateThreshold = std::clamp(db, -150.0f, 0.0f);
        ++parameterVersion;
    }

    void setChannel(int ch) {
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.channel = std::clamp(ch, 0, 1);
        ++parameterVersion;
    }

    void setMode(int channel, int crunch) {
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.channel = std::clamp(channel, 0, 1);
        parameters.crunch = crunch != 0;
        ++parameterVersion;
    }

    void setCrunch(int cr) {
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.crunch = cr != 0;
        ++parameterVersion;
    }

    void setBright(int br) {
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.bright = br != 0;
        ++parameterVersion;
    }

    void setPreGain(float v) { setChannelValue(&ChannelParameters::pre, v); }
    void setPostGain(float v) { setChannelValue(&ChannelParameters::post, v); }
    void setBass(float v) { setChannelValue(&ChannelParameters::bass, v); }
    void setMid(float v) { setChannelValue(&ChannelParameters::mid, v); }
    void setTreble(float v) { setChannelValue(&ChannelParameters::treble, v); }
    void setPresence(float v) { setChannelValue(&ChannelParameters::presence, v); }
    void setResonance(float v) { setChannelValue(&ChannelParameters::resonance, v); }

    void setToneStack(float bass, float mid, float treble) {
        if (!std::isfinite(bass) || !std::isfinite(mid) || !std::isfinite(treble)) return;
        std::lock_guard<std::mutex> lock(parameterMutex);
        auto& c = parameters.channels[parameters.channel];
        c.bass = std::clamp(bass, 0.0f, 10.0f);
        c.mid = std::clamp(mid, 0.0f, 10.0f);
        c.treble = std::clamp(treble, 0.0f, 10.0f);
        ++parameterVersion;
    }

    void setPowerAmp(float pres, float res, float post) {
        if (!std::isfinite(pres) || !std::isfinite(res) || !std::isfinite(post)) return;
        std::lock_guard<std::mutex> lock(parameterMutex);
        auto& c = parameters.channels[parameters.channel];
        c.presence = std::clamp(pres, 0.0f, 10.0f);
        c.resonance = std::clamp(res, 0.0f, 10.0f);
        c.post = std::clamp(post, 0.0f, 10.0f);
        ++parameterVersion;
    }

    bool loadCabinetIR(const std::string& path) {
        auto next = std::make_unique<CabSim>(cabSim);
        if (!next->loadIR(path)) return false;
        cabSim = *next;
        {
            std::lock_guard<std::mutex> lock(parameterMutex);
            parameters.cabEnabled = true;
            pendingCab.swap(next);
            cabPending = true;
            ++parameterVersion;
        }
        return true;
    }

    void setMicrophone(int idx) {
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.mic = std::clamp(idx, 0, 2);
        cabSim.currentMic = parameters.mic;
        ++parameterVersion;
    }
    void setMicPosition(float x, float z, bool is45) {
        if (!std::isfinite(x) || !std::isfinite(z)) return;
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.micX = std::clamp(x, 0.0f, 1.0f);
        parameters.micZ = std::clamp(z, 0.0f, 1.0f);
        parameters.mic45 = is45;
        cabSim.posX = parameters.micX; cabSim.distanceZ = parameters.micZ;
        cabSim.is45Degrees = is45;
        ++parameterVersion;
    }
    void setCabFilters(float low, float high) {
        if (!std::isfinite(low) || !std::isfinite(high)) return;
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.lowCut = std::clamp(low, 20.0f, 300.0f);
        parameters.highCut = std::clamp(high, 3000.0f, 20000.0f);
        cabSim.lowCutHz = parameters.lowCut; cabSim.highCutHz = parameters.highCut;
        ++parameterVersion;
    }
    void setCabEnabled(bool en) {
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.cabEnabled = en && !cabSim.customIrPath.empty();
        cabSim.setEnabled(en);
        ++parameterVersion;
    }

    // Audio Recording controls
    bool startRecording(const std::string& path = "") { return recorder.startRecording(path); }
    void stopRecording() { recorder.stopRecording(); }
    bool isRecording() const { return recorder.isRecording.load(); }
    double getRecordingDuration() const { return recorder.recordedSeconds.load(); }
    std::string getLastRecordingPath() const { return recorder.getLastRecordingPath(); }

private:
    friend struct LiveAudioEngineTest;
    std::mutex parameterMutex;
    LiveParameters parameters;
    uint64_t parameterVersion = 1, appliedVersion = 0;
    std::unique_ptr<CabSim> activeCab, pendingCab;
    bool cabPending = false;

    void setChannelValue(float ChannelParameters::*field, float value) {
        if (!std::isfinite(value)) return;
        std::lock_guard<std::mutex> lock(parameterMutex);
        parameters.channels[parameters.channel].*field = std::clamp(value, 0.0f, 10.0f);
        ++parameterVersion;
    }

    void applyPendingParameters() {
        LiveParameters next;
        {
            // The audio thread keeps its previous state if the UI is publishing a change.
            std::unique_lock<std::mutex> lock(parameterMutex, std::try_to_lock);
            if (!lock.owns_lock() || appliedVersion == parameterVersion) return;
            next = parameters;
            if (cabPending) {
                activeCab.swap(pendingCab);
                cabPending = false; // The old cab is retired on the control thread.
            }
            appliedVersion = parameterVersion;
        }
        for (auto* amp : {&ampL, &ampR}) {
            amp->channel = next.channel;
            amp->crunch = next.crunch;
            amp->bright = next.bright;
            next.channels[next.channel].apply(*amp);
        }
        noiseGate.setThreshold(next.gateThreshold);
        activeCab->setMicrophone(next.mic);
        activeCab->setMicPosition(next.micX, next.micZ, next.mic45);
        activeCab->setFilters(next.lowCut, next.highCut);
        activeCab->setEnabled(next.cabEnabled);
    }

    AudioUnit singleAU = nullptr;
    AudioUnit inputAU = nullptr;
    AudioUnit outputAU = nullptr;
    bool isSingleUnitMode = false;

    // SPSC Lock-Free Circular Ring Buffers for Dual Unit Mode
    SPSCQueue<float> ringBufL;
    SPSCQueue<float> ringBufR;
    std::atomic<bool> isPreRolled{false};
    float lastConcealL = 0.0f;
    float lastConcealR = 0.0f;

    // Dedicated scratch buffers per thread (eliminates thread data race)
    std::vector<float> inProcBufL;
    std::vector<float> inProcBufR;
    std::vector<float> outProcBufL;
    std::vector<float> outProcBufR;
    std::vector<float> singleProcBufL;
    std::vector<float> singleProcBufR;

    AudioUnit createHALUnit() {
        AudioComponentDescription desc;
        memset(&desc, 0, sizeof(desc));
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_HALOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        if (!comp) return nullptr;

        AudioComponentInstance au = nullptr;
        AudioComponentInstanceNew(comp, &au);
        return au;
    }

    AudioStreamBasicDescription makeStreamFormat(int channels) {
        AudioStreamBasicDescription asbd;
        memset(&asbd, 0, sizeof(asbd));
        asbd.mSampleRate = sampleRate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mBytesPerPacket = 4 * channels;
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerFrame = 4 * channels;
        asbd.mChannelsPerFrame = channels;
        asbd.mBitsPerChannel = 32;
        return asbd;
    }

    // --- Dual Units Mode: Input Device != Output Device ---
    bool startDualUnits() {
        // 1. Request hardware buffer frame size on both devices
        AudioObjectPropertyAddress bufferAddr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectSetPropertyData(inputDeviceID, &bufferAddr, 0, nullptr, sizeof(bufferSize), &bufferSize);
        AudioObjectSetPropertyData(outputDeviceID, &bufferAddr, 0, nullptr, sizeof(bufferSize), &bufferSize);

        // 2. Setup Input AU (Capture)
        inputAU = createHALUnit();
        if (!inputAU) return false;

        UInt32 enableIO = 1;
        AudioUnitSetProperty(inputAU, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
        UInt32 disableIO = 0;
        AudioUnitSetProperty(inputAU, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &disableIO, sizeof(disableIO));
        AudioUnitSetProperty(inputAU, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &inputDeviceID, sizeof(inputDeviceID));

        AudioStreamBasicDescription inFormat = makeStreamFormat(1); // Capture mono
        AudioUnitSetProperty(inputAU, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &inFormat, sizeof(inFormat));

        AURenderCallbackStruct inCb;
        inCb.inputProc = InputCallback;
        inCb.inputProcRefCon = this;
        AudioUnitSetProperty(inputAU, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &inCb, sizeof(inCb));

        UInt32 maxSlice = 4096;
        AudioUnitSetProperty(inputAU, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxSlice, sizeof(maxSlice));
        AudioUnitInitialize(inputAU);

        // 3. Setup Output AU (Playback)
        outputAU = createHALUnit();
        if (!outputAU) {
            AudioComponentInstanceDispose(inputAU);
            inputAU = nullptr;
            return false;
        }

        enableIO = 1;
        AudioUnitSetProperty(outputAU, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));
        disableIO = 0;
        AudioUnitSetProperty(outputAU, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &disableIO, sizeof(disableIO));
        AudioUnitSetProperty(outputAU, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &outputDeviceID, sizeof(outputDeviceID));

        AudioStreamBasicDescription outFormat = makeStreamFormat(2); // Stereo output
        AudioUnitSetProperty(outputAU, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &outFormat, sizeof(outFormat));

        AURenderCallbackStruct outCb;
        outCb.inputProc = OutputCallback;
        outCb.inputProcRefCon = this;
        AudioUnitSetProperty(outputAU, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &outCb, sizeof(outCb));

        AudioUnitSetProperty(outputAU, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxSlice, sizeof(maxSlice));
        AudioUnitInitialize(outputAU);

        // Query actual buffer size granted by macOS
        UInt32 actualBufSize = bufferSize;
        UInt32 propSize = sizeof(actualBufSize);
        AudioObjectGetPropertyData(outputDeviceID, &bufferAddr, 0, nullptr, &propSize, &actualBufSize);
        bufferSize = actualBufSize;

        AudioOutputUnitStart(inputAU);
        AudioOutputUnitStart(outputAU);
        isRunning = true;
        return true;
    }

    // --- Single Unit Mode: Input Device == Output Device ---
    bool startSingleUnit() {
        singleAU = createHALUnit();
        if (!singleAU) return false;

        UInt32 enableIO = 1;
        AudioUnitSetProperty(singleAU, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
        AudioUnitSetProperty(singleAU, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));
        AudioUnitSetProperty(singleAU, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &inputDeviceID, sizeof(inputDeviceID));

        AudioStreamBasicDescription inFormat = makeStreamFormat(1);
        AudioUnitSetProperty(singleAU, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &inFormat, sizeof(inFormat));

        AudioStreamBasicDescription outFormat = makeStreamFormat(2);
        AudioUnitSetProperty(singleAU, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &outFormat, sizeof(outFormat));

        AURenderCallbackStruct cb;
        cb.inputProc = SingleUnitCallback;
        cb.inputProcRefCon = this;
        AudioUnitSetProperty(singleAU, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));

        AudioObjectPropertyAddress bufferAddr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectSetPropertyData(inputDeviceID, &bufferAddr, 0, nullptr, sizeof(bufferSize), &bufferSize);

        UInt32 maxSlice = 4096;
        AudioUnitSetProperty(singleAU, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxSlice, sizeof(maxSlice));

        AudioUnitInitialize(singleAU);

        UInt32 actualBufSize = bufferSize;
        UInt32 propSize = sizeof(actualBufSize);
        AudioObjectGetPropertyData(inputDeviceID, &bufferAddr, 0, nullptr, &propSize, &actualBufSize);
        bufferSize = actualBufSize;

        AudioOutputUnitStart(singleAU);
        isRunning = true;
        return true;
    }

    // Dual Unit: Input Proc (Executed on Input IO Thread)
    static OSStatus InputCallback(void* inRefCon,
                                  AudioUnitRenderActionFlags* ioActionFlags,
                                  const AudioTimeStamp* inTimeStamp,
                                  UInt32 inBusNumber,
                                  UInt32 inNumberFrames,
                                  AudioBufferList* ioData) {
        (void)inBusNumber;
        (void)ioData;
        setDenormalFlags();
        LiveAudioEngine* self = static_cast<LiveAudioEngine*>(inRefCon);

        AudioBufferList abl;
        abl.mNumberBuffers = 1;
        abl.mBuffers[0].mNumberChannels = 1;
        abl.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(float);
        abl.mBuffers[0].mData = self->inProcBufL.data();

        OSStatus err = AudioUnitRender(self->inputAU, ioActionFlags, inTimeStamp, 1, inNumberFrames, &abl);
        if (err != noErr) return err;

        // Process DSP pipeline on inProcBufL (Mono in -> Stereo L/R out)
        self->processDSP(self->inProcBufL.data(), self->inProcBufR.data(), inNumberFrames);

        // Push into Ring Buffers
        self->ringBufL.write(self->inProcBufL.data(), inNumberFrames);
        self->ringBufR.write(self->inProcBufR.data(), inNumberFrames);
        return noErr;
    }

    // Dual Unit: Output Proc (Executed on Output IO Thread)
    static OSStatus OutputCallback(void* inRefCon,
                                   AudioUnitRenderActionFlags* ioActionFlags,
                                   const AudioTimeStamp* inTimeStamp,
                                   UInt32 inBusNumber,
                                   UInt32 inNumberFrames,
                                   AudioBufferList* ioData) {
        (void)ioActionFlags;
        (void)inTimeStamp;
        (void)inBusNumber;
        setDenormalFlags();
        LiveAudioEngine* self = static_cast<LiveAudioEngine*>(inRefCon);
        float* outInterleaved = static_cast<float*>(ioData->mBuffers[0].mData);

        // Pre-roll check: wait until ring buffer has buffered a safe cushion
        if (!self->isPreRolled.load(std::memory_order_acquire)) {
            size_t targetCushion = std::max((size_t)self->bufferSize * 4, (size_t)128);
            if (self->ringBufL.availableRead() < targetCushion) {
                std::memset(outInterleaved, 0, inNumberFrames * 2 * sizeof(float));
                return noErr;
            }
            self->isPreRolled.store(true, std::memory_order_release);
        }

        // Manage clock drift backlog: keep latency low (< 1536 samples)
        size_t available = self->ringBufL.availableRead();
        if (available > 2048) {
            size_t drop = available - 1024;
            float discardBuf[256];
            while (drop > 0) {
                size_t step = std::min(drop, (size_t)256);
                self->ringBufL.read(discardBuf, step);
                self->ringBufR.read(discardBuf, step);
                drop -= step;
            }
        }

        size_t readL = self->ringBufL.read(self->outProcBufL.data(), inNumberFrames);
        size_t readR = self->ringBufR.read(self->outProcBufR.data(), inNumberFrames);

        // Anti-click concealment: never step abruptly to 0.0f on underrun
        for (UInt32 i = 0; i < inNumberFrames; ++i) {
            float sL, sR;
            if (i < readL) {
                sL = self->outProcBufL[i];
                self->lastConcealL = sL;
            } else {
                self->lastConcealL *= 0.95f; // Smooth exponential decay
                sL = self->lastConcealL;
            }

            if (i < readR) {
                sR = self->outProcBufR[i];
                self->lastConcealR = sR;
            } else {
                self->lastConcealR *= 0.95f;
                sR = self->lastConcealR;
            }

            outInterleaved[2 * i]     = sL;
            outInterleaved[2 * i + 1] = sR;
        }
        return noErr;
    }

    // Single Unit: Synchronous Full Duplex Proc (Unified IO Thread)
    static OSStatus SingleUnitCallback(void* inRefCon,
                                       AudioUnitRenderActionFlags* ioActionFlags,
                                       const AudioTimeStamp* inTimeStamp,
                                       UInt32 inBusNumber,
                                       UInt32 inNumberFrames,
                                       AudioBufferList* ioData) {
        (void)inBusNumber;
        setDenormalFlags();
        LiveAudioEngine* self = static_cast<LiveAudioEngine*>(inRefCon);
        float* outInterleaved = static_cast<float*>(ioData->mBuffers[0].mData);

        AudioBufferList abl;
        abl.mNumberBuffers = 1;
        abl.mBuffers[0].mNumberChannels = 1;
        abl.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(float);
        abl.mBuffers[0].mData = self->singleProcBufL.data();

        OSStatus err = AudioUnitRender(self->singleAU, ioActionFlags, inTimeStamp, 1, inNumberFrames, &abl);
        if (err != noErr) {
            std::memset(outInterleaved, 0, inNumberFrames * 2 * sizeof(float));
            return err;
        }

        self->processDSP(self->singleProcBufL.data(), self->singleProcBufR.data(), inNumberFrames);

        for (UInt32 i = 0; i < inNumberFrames; ++i) {
            outInterleaved[2 * i]     = self->singleProcBufL[i];
            outInterleaved[2 * i + 1] = self->singleProcBufR[i];
        }
        return noErr;
    }

    // Core DSP processing pipeline (NoiseGate -> Amp5650 -> Limiter)
    void processDSP(float* l, float* r, UInt32 numFrames) {
        applyPendingParameters();
        float peakI = 0.0f, sumI = 0.0f;
        float peakO = 0.0f, sumO = 0.0f;

        // 1. Noise gate & input metering
        for (UInt32 i = 0; i < numFrames; ++i) {
            float inSample = l[i];
            float absIn = std::abs(inSample);
            if (absIn > peakI) peakI = absIn;
            sumI += inSample * inSample;

            float gated = noiseGate.process(inSample);
            l[i] = gated;
            r[i] = gated;
        }

        // 2. 5650+ Amp DSP (stereo)
        ampL.processBlock(l, numFrames);
        ampR.processBlock(r, numFrames);

        // 3. Cabinet & Microphone Simulation (FIR Convolution & Positioning)
        activeCab->processBlock(l, r, numFrames);

        // 4. Master Limiter & output metering
        for (UInt32 i = 0; i < numFrames; ++i) {
            l[i] = limiterL.process(l[i]);
            r[i] = limiterR.process(r[i]);

            float outPeak = std::max(std::abs(l[i]), std::abs(r[i]));
            if (outPeak > peakO) peakO = outPeak;
            sumO += l[i] * l[i] + r[i] * r[i];
        }

        // 5. Asynchronous High-Fidelity Audio Recording (Lock-Free)
        recorder.process(l, r, numFrames);

        // Update real-time atomic meters
        meters.peakIn.store(peakI, std::memory_order_relaxed);
        meters.rmsIn.store(std::sqrt(sumI / (numFrames > 0 ? numFrames : 1)), std::memory_order_relaxed);
        meters.gateReductionDb.store(noiseGate.gainReductionDb, std::memory_order_relaxed);
        meters.gateOpen.store(noiseGate.isOpen, std::memory_order_relaxed);
        meters.peakOut.store(peakO, std::memory_order_relaxed);
        meters.rmsOut.store(std::sqrt(sumO / (2 * (numFrames > 0 ? numFrames : 1))), std::memory_order_relaxed);
    }
};

} // namespace Amp5650
