#pragma once

#include "gameplay/Types.hpp"
#include <raylib.h>
#include <unordered_map>
#include <vector>

namespace oc {

class SynthAudio {
public:
    SynthAudio() = default;
    ~SynthAudio();

    SynthAudio(const SynthAudio&) = delete;
    SynthAudio& operator=(const SynthAudio&) = delete;

    void init();
    void shutdown();

    void playEvents(const std::vector<AudioEvent>& events);
    void updateVehicleLoops(bool driving, float speedRatio, bool drifting, bool nitro);

private:
    Sound buildNoiseBurst(float durationSec, float decayPower, float lowpassAlpha, float pitchScale);
    Sound buildToneArp(const std::vector<float>& freqsHz, float noteLenSec, float volume);
    Sound buildDualHorn(float f1, float f2, float durationSec);
    Sound buildContinuousLoop(bool screechMode);

    bool ready_ = false;
    std::unordered_map<int, Sound> sounds_;
    Sound engineLoop_{};
    Sound driftLoop_{};
    bool hasLoops_ = false;
};

} // namespace oc
