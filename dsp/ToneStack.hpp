#pragma once
#include "VoicingData.hpp"
#include <array>

namespace Amp5650 {
class FilterBank {
public:
    std::array<Biquad, 6> filters;
    std::array<bool, 6> enabled{};
    size_t count = 0;
    template<size_t N> void setup(const FilterSpec (&specs)[N], float rate, float scale = 1) {
        static_assert(N <= 6);
        count = N;
        for (size_t i = 0; i < N; ++i) {
            enabled[i] = specs[i].enabled;
            filters[i].snapTinyState = specs[i].type != FilterType::Lowshelf;
            filters[i].setup(specs[i].type, specs[i].frequency, specs[i].q, specs[i].db * scale, rate);
        }
    }
    void reset() { for (auto& filter : filters) filter.reset(); }
    float process(float input) {
        for (size_t i = 0; i < count; ++i) if (enabled[i]) input = filters[i].process(input);
        return input;
    }
};

class ToneStack {
public:
    FilterBank fixed, bassBoost, bassCut, midBoost, midCut, highBoost, highCut, brightFilter;
    float currentSampleRate = 44100;
    int currentChannel = 1, currentBright = 0;
    void prepare(float rate) { currentSampleRate = rate; update(5, 5, 5); reset(); }
    void reset() {
        for (auto* bank : {&fixed, &bassBoost, &bassCut, &midBoost, &midCut, &highBoost, &highCut, &brightFilter}) bank->reset();
    }
    void update(float bass, float mid, float high, int channel = 1, int /*crunch*/ = 1, int bright = 0) {
        currentChannel = channel; currentBright = bright;
        const float b = std::clamp(bass / 10.f, 0.f, 1.f) * 2 - 1;
        const float m = std::clamp(mid / 10.f, 0.f, 1.f) * 2 - 1;
        const float h = std::clamp(high / 10.f, .01f, 1.f);
        const float interaction = 1 - std::max(m, 0.f) * .8f;
        const float t = ((1 - interaction) * .5f + h * interaction) * 2 - 1;
        const float fixedScale = std::clamp(std::min(h, .5f) * 2 * std::max(m, 0.f) + (1 - std::max(m, 0.f)), .2f, 1.f);
        const float rate = currentSampleRate;
        if (channel == 1) {
            fixed.setup(redFixedSpecs, rate, fixedScale);
            bassBoost.setup(redBassBoostSpecs, rate, std::max(b, 0.f));
            bassCut.setup(redBassCutSpecs, rate, std::max(-b, 0.f));
            midBoost.setup(redMidBoostSpecs, rate, std::max(m, 0.f));
            midCut.setup(redMidCutSpecs, rate, std::max(-m, 0.f));
            highBoost.setup(redHighBoostSpecs, rate, std::max(t, 0.f));
            highCut.setup(redHighCutSpecs, rate, std::max(-t, 0.f));
        } else {
            fixed.setup(greenFixedSpecs, rate, fixedScale);
            bassBoost.setup(greenBassBoostSpecs, rate, std::max(b, 0.f));
            bassCut.setup(greenBassCutSpecs, rate, std::max(-b, 0.f));
            midBoost.setup(greenMidBoostSpecs, rate, std::max(m, 0.f));
            midCut.setup(greenMidCutSpecs, rate, std::max(-m, 0.f));
            highBoost.setup(greenHighBoostSpecs, rate, std::max(t, 0.f));
            highCut.setup(greenHighCutSpecs, rate, std::max(-t, 0.f));
        }
        brightFilter.setup(greenBrightSpecs, rate);
    }
    float process(float input) {
        for (auto* bank : {&fixed, &bassBoost, &bassCut, &midBoost, &midCut, &highBoost, &highCut}) input = bank->process(input);
        return currentChannel == 0 && currentBright ? brightFilter.process(input) : input;
    }
};
}
