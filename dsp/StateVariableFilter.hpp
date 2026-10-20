#pragma once
#include <algorithm>
#include <cmath>

namespace Amp5650 {
// TPT low-pass for the triode operating-point envelope.
class StateVariableLowpass {
    double a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
public:
    void prepare(float frequency, float q, float rate) {
        const double g = std::tan(3.14159265358979323846 * std::clamp(frequency, .001f, rate * .499f) / rate);
        a1 = 1 / (1 + g * (g + 1 / double(q)));
        a2 = g * a1;
        a3 = g * a2;
        reset();
    }
    void reset() { ic1 = ic2 = 0; }
    float process(float input) {
        const double v3 = double(input) - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return float(v2);
    }
};

class CouplingDCBlocker {
    double previousInput = 0, previousOutput = 0, pole = 0;
public:
    void prepare(float rate) {
        pole = 1 - 1 / (double(rate) * .001 * 3 * 4.535147190093994);
        reset();
    }
    void reset() { previousInput = previousOutput = 0; }
    float process(float input) {
        const double output = (double(input) - previousInput) + pole * previousOutput;
        previousInput = input;
        previousOutput = output;
        return float(output);
    }
};
}
