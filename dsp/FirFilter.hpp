#pragma once
#include <Accelerate/Accelerate.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Amp5650 {
class FirFilter {
    std::vector<float> coefficients, history;
    size_t position = 0;
public:
    template<size_t N, size_t M, size_t P> void prepare(const float (&native)[N], const float (&at48k)[M], const float (&at96k)[P], float rate) {
        if (rate == 44100) coefficients.assign(native, native + N);
        else if (rate == 48000) coefficients.assign(at48k, at48k + M);
        else if (rate == 96000) coefficients.assign(at96k, at96k + P);
        else {
            // shortcut: resample responses outside 44.1/48/96 kHz; validate dedicated responses before extending calibration.
            const double ratio = double(rate) / 44100, cutoff = std::min(1., ratio);
            coefficients.assign(size_t(std::ceil(N * ratio)), 0);
            for (size_t i = 0; i < coefficients.size(); ++i) {
                const double source = i / ratio;
                for (int tap = int(source) - 32; tap <= int(source) + 32; ++tap) {
                    if (tap < 0 || tap >= int(N)) continue;
                    const double delta = source - tap;
                    if (std::abs(delta) > 32) continue;
                    const double x = 3.14159265358979323846 * delta * cutoff;
                    const double sinc = std::abs(x) < 1e-12 ? 1 : std::sin(x) / x;
                    coefficients[i] += float(native[tap] * sinc * cutoff * (.5 + .5 * std::cos(3.14159265358979323846 * delta / 32)));
                }
            }
        }
        std::reverse(coefficients.begin(), coefficients.end());
        history.assign(2 * coefficients.size(), 0);
        position = 0;
    }
    void reset() { std::fill(history.begin(), history.end(), 0); position = 0; }
    float process(float input) {
        const size_t length = coefficients.size();
        history[position] = history[position + length] = input;
        float output = 0;
        vDSP_dotpr(history.data() + position + 1, 1, coefficients.data(), 1, &output, length);
        if (++position == length) position = 0;
        return output;
    }
};
}
