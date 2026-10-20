#pragma once
#include "Biquad.hpp"
#include "StateVariableFilter.hpp"

namespace Amp5650 {
// Asymmetric push-pull power stage.
class PowerAmp {
public:
    Biquad resonanceFilter, presenceFilter;
    CouplingDCBlocker positiveDC, negativeDC;
    float inputGain = 1, postGain = 1, presenceAmount = .159776f, resonanceAmount = 1.77f;
    float currentInputGain = 1, currentPostGain = 1, gainSmoothing = 1;

    void prepare(float rate) {
        gainSmoothing = 1 - std::exp(-1.f / (.005f * rate));
        resonanceFilter.setup(FilterType::Bandpass, 53, .73f, 0, rate);
        presenceFilter.setup(FilterType::Bandpass, 4181, .75f, 0, rate);
        resonanceFilter.snapTinyState = presenceFilter.snapTinyState = true;
        positiveDC.prepare(rate); negativeDC.prepare(rate);
        update(5, 5, 3);
        reset();
    }
    void reset() {
        resonanceFilter.reset(); presenceFilter.reset();
        positiveDC.reset(); negativeDC.reset();
        currentInputGain = inputGain; currentPostGain = postGain;
    }
    void update(float presence, float resonance, float post) {
        presenceAmount = std::pow(std::clamp(presence / 10.f, 0.f, 1.f), 1.f / .37794647f);
        resonanceAmount = 3 * std::pow(std::clamp(resonance / 10.f, 0.f, 1.f), 1.f / 1.3136925f);
        const float p = std::clamp(post / 10.f, 0.f, 1.f);
        inputGain = p < .3f ? p / .3f : std::pow(10.f, ((p - .3f) * 60.f / .7f) / 20.f);
        const float attenuation = p <= .3f ? 0.f : float(std::sin(double(p) * 2.36704 + 52.6495) * -30.12470054626465 + 1.4150700569152832);
        postGain = std::pow(10.f, -attenuation / 20.f);
    }
    float process(float input) {
        currentInputGain += (inputGain - currentInputGain) * gainSmoothing;
        currentPostGain += (postGain - currentPostGain) * gainSmoothing;
        if (std::abs(currentInputGain - inputGain) < 1e-6f) currentInputGain = inputGain;
        if (std::abs(currentPostGain - postGain) < 1e-6f) currentPostGain = postGain;
        const float presence = presenceFilter.process(input) * presenceAmount;
        const float resonance = resonanceFilter.process(input) * resonanceAmount;
        const float feedback = currentInputGain < 1 ? currentInputGain : 1 + currentInputGain * .3f;
        const float driven = input * currentInputGain + feedback * presence + feedback * resonance;
        const float x = driven * 1.7f;
        const float a = positiveDC.process(x / (driven > 0 ? 1 + x / .5f : 1 - x / 23.6f)) * 4;
        const float b = negativeDC.process(x / (driven > 0 ? 1 + x / 23.6f : 1 - x / .5f)) * 4;
        return (a / (1 + std::abs(a) / 1.01f) + b / (1 + std::abs(b) / 1.01f)) * currentPostGain;
    }
};
}
