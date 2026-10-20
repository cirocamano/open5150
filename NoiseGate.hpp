#pragma once
#include <cmath>
#include <algorithm>

namespace Amp5650 {

class NoiseGate {
public:
    float thresholdDb = -55.0f; // Umbral en dBFS (-90 a -20)
    float attackMs = 1.0f;      // Ataque de apertura ultra-rápido (ms)
    float holdMs = 25.0f;       // Tiempo de retención para notas sostenidas (ms)
    float releaseMs = 60.0f;    // Decaimiento suave (ms)
    float hysteresisDb = 3.0f;  // Diferencia entre apertura y cierre (anti-chatter)
    bool enabled = true;

    float currentGain = 0.0f;   // Ganancia lineal actual (0.0 a 1.0)
    float gainReductionDb = 0.0f;
    bool isOpen = false;

    void prepare(float sampleRate) {
        sr = sampleRate;
        attackCoeff = std::exp(-1.0f / (attackMs * 0.001f * sr));
        releaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * sr));
        holdSamples = (int)(holdMs * 0.001f * sr);
        holdCounter = 0;
        envelope = 0.0f;
        currentGain = 1.0f;
        gainReductionDb = 0.0f;
        isOpen = true;
    }

    void setThreshold(float thDb) {
        thresholdDb = thDb;
    }

    inline float process(float in) {
        if (!enabled) {
            currentGain = 1.0f;
            gainReductionDb = 0.0f;
            isOpen = true;
            return in;
        }

        // 1. Detección de envolvente de pico
        float absIn = std::abs(in);
        if (absIn > envelope) {
            envelope = absIn;
        } else {
            envelope = 0.999f * envelope;
        }

        float envDb = (envelope > 1e-5f) ? (20.0f * std::log10(envelope)) : -100.0f;

        // 2. Lógica de estado con histéresis y tiempo de retención (Hold)
        float openThreshold = thresholdDb;
        float closeThreshold = thresholdDb - hysteresisDb;

        if (envDb >= openThreshold) {
            isOpen = true;
            holdCounter = holdSamples;
        } else if (holdCounter > 0) {
            holdCounter--;
            isOpen = true;
        } else if (envDb < closeThreshold) {
            isOpen = false;
        }

        // 3. Suavizado balístico de ganancia (Attack / Release)
        float targetGain = isOpen ? 1.0f : 0.0f;
        if (targetGain > currentGain) {
            currentGain = (1.0f - attackCoeff) * targetGain + attackCoeff * currentGain;
        } else {
            currentGain = (1.0f - releaseCoeff) * targetGain + releaseCoeff * currentGain;
        }

        gainReductionDb = (currentGain > 1e-4f) ? (20.0f * std::log10(currentGain)) : -80.0f;
        return in * currentGain;
    }

private:
    float sr = 48000.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    int holdSamples = 0;
    int holdCounter = 0;
    float envelope = 0.0f;
};

} // namespace Amp5650
