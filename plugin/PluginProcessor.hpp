#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../AmpParameters.hpp"
#include "../CabSim.hpp"
#include "../NoiseGate.hpp"
#include "../Limiter.hpp"
#include <mutex>

class Open5150Processor final : public juce::AudioProcessor, private juce::Thread {
public:
    Open5150Processor();
    ~Open5150Processor() override;
    void prepareToPlay(double rate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "OPEN5150"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void requestIR(const juce::String& path);
    juce::String getIRStatus();
    juce::AudioProcessorValueTreeState parameters;

private:
    static constexpr const char* ids[] = {"mode", "bright", "pre", "post", "bass", "mid",
        "treble", "presence", "resonance", "gate", "cab", "mic", "micX", "micZ", "mic45",
        "lowCut", "highCut"};
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::array<std::atomic<float>*, std::size(ids)> values{};
    std::array<float, std::size(ids)> applied{};
    Amp5650::Amp5650Engine amps[2];
    Amp5650::NoiseGate gate;
    Amp5650::MasterLimiter limiter;
    std::unique_ptr<Amp5650::CabSim> cab, pendingCab;
    std::mutex irMutex;
    juce::String requestedPath, savedPath, irStatus = "Sin IR WAV: cabina en bypass";
    double irRate = 48000;
    uint64_t requestedVersion = 0, loadedVersion = 0;
    bool pending = false;
    void run() override;
    void updateParameters();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Open5150Processor)
};
