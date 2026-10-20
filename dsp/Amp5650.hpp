#pragma once
#include "TubeStage.hpp"
#include "ToneStack.hpp"
#include "PowerAmp.hpp"
#include "FirFilter.hpp"
#include "AmpFilterResponses.hpp"
#include <array>

namespace Amp5650 {
class Amp5650Engine {
public:
    // Eight preamp stages for the lead and crunch signal paths.
    std::array<TubeStage, 8> redStages, greenStages;
    ToneStack toneStack;
    PowerAmp powerAmp;
    FilterBank driveFilter, impedanceFilter;
    std::array<FirFilter, 3> inputFilters, outputFilters;
    int channel = 1, power = 1, crunch = 1, bright = 0;
    float preRed = 5, postRed = 3, preGreen = 5, postGreen = 3;
    float bass = 5, mid = 5, treble = 5, presence = 5, resonance = 5;
    float sampleRate = 44100, srScale = 1;
    float totalPreGain = 1, currentPreGain = 1, gainSmoothing = 1, volComp = 1, outputMakeup = 1;

    void prepare(float rate) {
        sampleRate = rate;
        srScale = std::sqrt(44100.f / rate);
        gainSmoothing = 1 - std::exp(-1.f / (.005f * rate));
        for (size_t i = 0; i < redStages.size(); ++i) redStages[i].prepare(rate, i == 1 ? 43.94580078125f : 1.f);
        for (size_t i = 0; i < greenStages.size(); ++i) greenStages[i].prepare(rate, i == 1 ? 5.14f : 1.f);
        inputFilters[0].prepare(redInput44100, redInput48000, redInput96000, rate);
        inputFilters[1].prepare(crunchInput44100, crunchInput48000, crunchInput96000, rate);
        inputFilters[2].prepare(cleanInput44100, cleanInput48000, cleanInput96000, rate);
        outputFilters[0].prepare(redOutput44100, redOutput48000, redOutput96000, rate);
        outputFilters[1].prepare(crunchOutput44100, crunchOutput48000, crunchOutput96000, rate);
        outputFilters[2].prepare(cleanOutput44100, cleanOutput48000, cleanOutput96000, rate);
        toneStack.prepare(rate); powerAmp.prepare(rate);
        impedanceFilter.setup(redImpedanceSpecs, rate);
        updateGains(); reset();
    }
    void reset() {
        for (auto& stage : redStages) stage.reset();
        for (auto& stage : greenStages) stage.reset();
        for (auto& filter : inputFilters) filter.reset();
        for (auto& filter : outputFilters) filter.reset();
        toneStack.reset(); powerAmp.reset(); driveFilter.reset(); impedanceFilter.reset();
        currentPreGain = totalPreGain;
    }
    void setChannel(int next) { channel = next; updateGains(); }
    void updateGains() {
        const bool red = channel == 1;
        const float g = preCurve(red ? preRed : preGreen, red);
        const float over = std::max(2 * (g - .5f), 0.f);
        const float drive = 132 + (red ? 880 : crunch ? 840 : 140) * over;
        const float boost = std::pow(10.f, 15 * over / 20);
        const float trim = red ? .0227553f : .1945525f;
        const float scale = red ? 1.5f : 3.255302f * (crunch ? 1 : 4.9493918f);
        totalPreGain = trim * srScale * drive * boost * std::min(g, .5f) * scale;
        volComp = red || crunch ? std::pow(10.f, -(red ? 6.0634f : 4.001844f) * over / 20) : 1;
        const float dry = g < .5f ? .1f : -.64f * (g - .5f);
        for (auto& stage : redStages) stage.dryGain = dry;
        for (auto& stage : greenStages) {
            stage.dryGain = crunch ? dry : 0;
            stage.negTanhSlope = crunch ? .7f : .5f;
        }
        if (red) driveFilter.setup(redDriveSpecs, sampleRate, over * .78f);
        else driveFilter.setup(greenDriveSpecs, sampleRate, over * .78f);
        toneStack.update(bass, mid, treble, channel, crunch, bright);
        powerAmp.update(presence, resonance, red ? postRed : postGreen);
        outputMakeup = .425f * srScale * (red ? 9.1133995f : 11.0556f * (crunch ? 1 : 4.05f)) * 1.5f * .66f;
    }
    void processBlock(float* buffer, int count) {
        if (!power || count <= 0) return;
        const int mode = channel == 1 ? 0 : crunch ? 1 : 2;
        for (int i = 0; i < count; ++i) {
            currentPreGain += (totalPreGain - currentPreGain) * gainSmoothing;
            if (std::abs(currentPreGain - totalPreGain) < 1e-6f) currentPreGain = totalPreGain;
            float x = driveFilter.process(inputFilters[mode].process(buffer[i]) * currentPreGain);
            if (channel == 1) for (auto& stage : redStages) x = stage.process(x);
            else for (int stage = 0; stage < (crunch ? 8 : 2); ++stage) x = greenStages[stage].process(x);
            x = powerAmp.process(toneStack.process(x * volComp) * .001f);
            if (channel == 1) x = impedanceFilter.process(x);
            buffer[i] = outputFilters[mode].process(x) * outputMakeup;
        }
    }
};
}
