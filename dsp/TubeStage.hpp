#pragma once
#include "StateVariableFilter.hpp"
#include <algorithm>
#include <cmath>

namespace Amp5650 {
// Triode waveshaper with dynamic bias and DC blocking.
class TubeStage {
public:
    float gain = 1, inputGain = 1.5f, bias = 0, biasAmount = .4f, dryGain = 0;
    float Vg0 = 1.5f, VposMax = 4, VnegMax = -1.5f, negTanhSlope = .7f;
    double dcPole = 0, dcPrevX = 0, dcPrevY = 0;
    StateVariableLowpass biasFilter;

    void prepare(float rate, float stageGain = 1, float slope = .7f) {
        gain = stageGain;
        negTanhSlope = slope;
        dcPole = 1 - 1 / (double(rate) * .001 * 3 * 4.535147190093994);
        biasFilter.prepare(3, 1.1f, rate);
        reset();
    }
    void reset() { biasFilter.reset(); dcPrevX = dcPrevY = 0; }

    // Conformación asimétrica de onda:
    // Semiciclo positivo: Polinomio Cubic Hermite (1.5u - 0.5u^3)
    // Semiciclo negativo: Hipérbola tangente normalizada (tanh(u*s)/tanh(s))
    inline float shape(float x, float biasOffset) const {
        float s0 = x + biasOffset;
        const float s2 = Vg0;
        const float s4 = VposMax;
        const float s5 = VnegMax;
        const float s8 = negTanhSlope;

        if (s0 > s2) {
            if (s0 >= s4) {
                return s4;
            } else {
                // Keep normalization bounded when the positive ceiling is <= 1.
                float range = s4 - s2;
                float norm = (s4 > 1.0f) ? range : 1.0f;
                float u = (s0 - s2) / norm;
                float cubic = 1.5f * u * (1.0f - (u * u) / 3.0f);
                return s2 + range * cubic;
            }
        } else if (s0 > 0.0f) {
            return s0;
        } else {
            if (s0 <= s5) {
                return s5;
            } else {
                float absNeg = std::abs(s5);
                float norm = (s5 < -1.0f) ? absNeg : 1.0f;
                float u = s0 / norm;
                float tNorm = std::tanh(s8);
                if (std::abs(tNorm) < 1e-6f) tNorm = 1.0f;
                return (std::tanh(u * s8) / tNorm) * absNeg;
            }
        }
    }

    float process(float input) {
        float x = inputGain * gain * input;
        if (x > 0) {
            const float delta = std::max(0.f, x - Vg0);
            x *= float(std::exp(-.3241584 * double(delta)) * .5451584 + .4473253);
        }
        const float operatingBias = bias + std::abs(std::min(biasFilter.process(x), 0.f) * biasAmount);
        const float ac = shape(x, operatingBias) - shape(operatingBias, 0);
        const double output = (double(ac) - dcPrevX) + dcPole * dcPrevY;
        dcPrevX = ac;
        dcPrevY = output;
        return input * dryGain - float(output);
    }
};
}
