#pragma once
#include <cmath>
#include <algorithm>

namespace Amp5650 {

enum class FilterType {
    Lowpass,
    Highpass,
    Bandpass,
    Notch,
    Peaking,
    Lowshelf,
    Highshelf
};

// Filtro Biquad Direct Form II Transpuesto (Numéricamente estable para audio flotante)
class Biquad {
public:
    double b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    double a1 = 0.0f, a2 = 0.0f;
    double z1 = 0.0f, z2 = 0.0f;
    bool snapTinyState = false;

    inline float process(float x) {
        if (rampLeft > 0) {
            b0 += step[0]; b1 += step[1]; b2 += step[2]; a1 += step[3]; a2 += step[4];
            if (--rampLeft == 0) {
                b0 = target[0]; b1 = target[1]; b2 = target[2]; a1 = target[3]; a2 = target[4];
            }
        }
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (snapTinyState) {
            constexpr double floor = double(1e-8f);
            if (std::abs(z1) <= floor) z1 = 0;
            if (std::abs(z2) <= floor) z2 = 0;
        }
        return y;
    }

    void reset() {
        z1 = 0.0f;
        z2 = 0.0f;
        if (initialized) {
            b0 = target[0]; b1 = target[1]; b2 = target[2]; a1 = target[3]; a2 = target[4];
        }
        rampLeft = 0;
    }

    void setup(FilterType type, float fc, float Q, float gainDB, float sampleRate) {
        if (!std::isfinite(fc) || !std::isfinite(Q) || !std::isfinite(gainDB) ||
            !std::isfinite(sampleRate) || sampleRate <= 0 || Q <= 0) return;
        fc = std::clamp(fc, 0.001f, sampleRate * 0.499f);
        const double previous[] = {b0, b1, b2, a1, a2};
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sampleRate;
        const double cosW = std::cos(w0);
        const double sinW = std::sin(w0);
        const double alpha = sinW / (2.0f * Q);
        const double A = std::pow(10.0, double(gainDB) / 40.0);

        double a0 = 1.0f;

        switch (type) {
            case FilterType::Lowpass:
                b0 = (1.0f - cosW) * 0.5f;
                b1 = 1.0f - cosW;
                b2 = (1.0f - cosW) * 0.5f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosW;
                a2 = 1.0f - alpha;
                break;
            case FilterType::Highpass:
                b0 = (1.0f + cosW) * 0.5f;
                b1 = -(1.0f + cosW);
                b2 = (1.0f + cosW) * 0.5f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosW;
                a2 = 1.0f - alpha;
                break;
            case FilterType::Bandpass:
                b0 = alpha; b1 = 0; b2 = -alpha;
                a0 = 1 + alpha; a1 = -2 * cosW; a2 = 1 - alpha;
                break;
            case FilterType::Notch:
                b0 = 1; b1 = -2 * cosW; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cosW; a2 = 1 - alpha;
                break;
            case FilterType::Peaking: {
                b0 = 1.0f + alpha * A;
                b1 = -2.0f * cosW;
                b2 = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A;
                a1 = -2.0f * cosW;
                a2 = 1.0f - alpha / A;
                break;
            }
            case FilterType::Lowshelf: {
                const double sqrtA = std::sqrt(A);
                b0 = A * ((A + 1.0f) - (A - 1.0f) * cosW + 2.0f * sqrtA * alpha);
                b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW);
                b2 = A * ((A + 1.0f) - (A - 1.0f) * cosW - 2.0f * sqrtA * alpha);
                a0 = (A + 1.0f) + (A - 1.0f) * cosW + 2.0f * sqrtA * alpha;
                a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW);
                a2 = (A + 1.0f) + (A - 1.0f) * cosW - 2.0f * sqrtA * alpha;
                break;
            }
            case FilterType::Highshelf: {
                const double sqrtA = std::sqrt(A);
                b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW + 2.0f * sqrtA * alpha);
                b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW);
                b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW - 2.0f * sqrtA * alpha);
                a0 = (A + 1.0f) - (A - 1.0f) * cosW + 2.0f * sqrtA * alpha;
                a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW);
                a2 = (A + 1.0f) - (A - 1.0f) * cosW - 2.0f * sqrtA * alpha;
                break;
            }
            default:
                b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
                a0 = 1.0f; a1 = 0.0f; a2 = 0.0f;
                break;
        }

        // Normalización respecto a a0
        const double invA0 = 1.0f / a0;
        b0 *= invA0;
        b1 *= invA0;
        b2 *= invA0;
        a1 *= invA0;
        a2 *= invA0;
        const double next[] = {b0, b1, b2, a1, a2};
        if (!initialized) {
            std::copy(std::begin(next), std::end(next), target);
            initialized = true;
            return;
        }
        if (!std::equal(std::begin(next), std::end(next), target)) {
            // shortcut: 5 ms coefficient ramps; match each native control ramp after runtime verification.
            rampLeft = std::max(1, static_cast<int>(sampleRate * .005f));
            for (int i = 0; i < 5; ++i) {
                target[i] = next[i];
                step[i] = (next[i] - previous[i]) / rampLeft;
            }
        }
        b0 = previous[0]; b1 = previous[1]; b2 = previous[2]; a1 = previous[3]; a2 = previous[4];
    }

private:
    bool initialized = false;
    int rampLeft = 0;
    double target[5]{}, step[5]{};
};

// Bloqueador de offset DC para desacople entre etapas de triodo
class DCBlocker {
private:
    float x_prev = 0.0f;
    float y_prev = 0.0f;
    const float R = 0.995f;

public:
    inline float process(float x) {
        float y = x - x_prev + R * y_prev;
        x_prev = x;
        y_prev = y;
        return y;
    }

    void reset() {
        x_prev = 0.0f;
        y_prev = 0.0f;
    }
};

} // namespace Amp5650
