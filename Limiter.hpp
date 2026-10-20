#pragma once
#include <cmath>
#include <algorithm>

namespace Amp5650 {

class MasterLimiter {
public:
    float ceiling = 0.944f; // -0.5 dBFS de margen de seguridad
    bool enabled = true;

    inline float process(float in) {
        if (!enabled) return in;
        float x = in;
        if (std::abs(x) < ceiling * 0.8f) {
            return x; // Zona lineal
        }
        // Soft-knee saturador suave
        float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        float ax = std::abs(x);
        float over = ax - ceiling * 0.8f;
        float range = ceiling * 0.2f;
        float compressed = ceiling * 0.8f + range * std::tanh(over / range);
        return sign * std::min(compressed, ceiling);
    }
};

} // namespace Amp5650
