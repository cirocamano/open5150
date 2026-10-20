#pragma once
#include "dsp/Amp5650.hpp"

namespace Amp5650 {

struct ChannelParameters {
    float pre = 5, post = 3;
    float bass = 5, mid = 5, treble = 5, presence = 5, resonance = 5;

    void apply(Amp5650Engine& amp) const {
        if (amp.channel == 1) { amp.preRed = pre; amp.postRed = post; }
        else { amp.preGreen = pre; amp.postGreen = post; }
        amp.bass = bass; amp.mid = mid; amp.treble = treble;
        amp.presence = presence; amp.resonance = resonance;
        amp.updateGains();
    }
};

struct LiveParameters {
    ChannelParameters channels[2];
    int channel = 1, crunch = 1, bright = 0;
    float gateThreshold = -55;
    int mic = 0;
    float micX = .25f, micZ = .15f;
    bool mic45 = false, cabEnabled = false;
    float lowCut = 75, highCut = 11500;
};

} // namespace Amp5650
