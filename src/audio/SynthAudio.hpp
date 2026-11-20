#pragma once

#include "gameplay/Types.hpp"
#include <vector>

namespace oc {

class SynthAudio {
public:
    SynthAudio();
    ~SynthAudio();

    SynthAudio(const SynthAudio&) = delete;
    SynthAudio& operator=(const SynthAudio&) = delete;

    void init();
    void shutdown();

    void playEvents(const std::vector<AudioEvent>& events);
    void updateVehicleLoops(bool driving, float speedRatio, bool drifting, bool nitro);
    void updatePoliceSiren(float proximityIntensity);
    void updateWeatherAmbience(bool raining);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace oc
