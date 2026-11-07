#include "audio/SynthAudio.hpp"

#include "core/Math.hpp"
#include <cmath>
#include <cstdlib>

namespace oc {

SynthAudio::~SynthAudio() {
    shutdown();
}

Sound SynthAudio::buildNoiseBurst(float durationSec, float decayPower, float lowpassAlpha, float pitchScale) {
    constexpr int sampleRate = 44100;
    const int frames = std::max(64, static_cast<int>(durationSec * sampleRate));

    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    auto* data = static_cast<short*>(RL_MALLOC(static_cast<std::size_t>(frames) * sizeof(short)));

    Rng rng(0xABCD9876ULL + static_cast<std::uint64_t>(frames));
    float filtered = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        const float env = std::pow(1.0f - t, decayPower);
        const float white = rng.range(-1.0f, 1.0f);
        filtered = filtered + lowpassAlpha * (white - filtered);
        const float thump = std::sin(2.0f * kPi * (110.0f * pitchScale) * std::pow(1.0f - t * 0.7f, 1.5f) *
                                     (static_cast<float>(i) / sampleRate));
        const float sample = clampf((filtered * 0.82f + thump * 0.45f) * env, -1.0f, 1.0f);
        data[i] = static_cast<short>(sample * 28000.0f);
    }

    wave.data = data;
    Sound s = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return s;
}

Sound SynthAudio::buildToneArp(const std::vector<float>& freqsHz, float noteLenSec, float volume) {
    constexpr int sampleRate = 44100;
    const int noteFrames = std::max(64, static_cast<int>(noteLenSec * sampleRate));
    const int totalFrames = noteFrames * static_cast<int>(freqsHz.size());

    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(totalFrames);
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    auto* data = static_cast<short*>(RL_MALLOC(static_cast<std::size_t>(totalFrames) * sizeof(short)));

    float phase = 0.0f;
    for (std::size_t n = 0; n < freqsHz.size(); ++n) {
        const float f = freqsHz[n];
        for (int i = 0; i < noteFrames; ++i) {
            const float localT = static_cast<float>(i) / static_cast<float>(noteFrames);
            const float env = std::sin(std::min(1.0f, localT * 12.0f) * kHalfPi) * (1.0f - localT * 0.55f);
            phase += kTwoPi * f / static_cast<float>(sampleRate);
            const float s = (std::sin(phase) * 0.75f + std::sin(phase * 2.0f) * 0.25f) * env * volume;
            data[static_cast<int>(n) * noteFrames + i] = static_cast<short>(clampf(s, -1.0f, 1.0f) * 26000.0f);
        }
    }

    wave.data = data;
    Sound s = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return s;
}

Sound SynthAudio::buildDualHorn(float f1, float f2, float durationSec) {
    constexpr int sampleRate = 44100;
    const int frames = static_cast<int>(durationSec * sampleRate);

    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    auto* data = static_cast<short*>(RL_MALLOC(static_cast<std::size_t>(frames) * sizeof(short)));

    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float norm = static_cast<float>(i) / static_cast<float>(frames);
        const float env = (norm < 0.06f ? norm / 0.06f : (norm > 0.88f ? (1.0f - norm) / 0.12f : 1.0f));
        const float v = (std::sin(kTwoPi * f1 * t) + std::sin(kTwoPi * f2 * t) +
                         0.35f * std::sin(kTwoPi * f1 * 2.0f * t)) * 0.36f * env;
        data[i] = static_cast<short>(clampf(v, -1.0f, 1.0f) * 25000.0f);
    }

    wave.data = data;
    Sound s = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return s;
}

Sound SynthAudio::buildContinuousLoop(bool screechMode) {
    constexpr int sampleRate = 44100;
    const int frames = sampleRate; // 1.0s seamless buffer

    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    auto* data = static_cast<short*>(RL_MALLOC(static_cast<std::size_t>(frames) * sizeof(short)));

    Rng rng(777123ULL);
    float filt = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float sample = 0.0f;
        if (!screechMode) {
            // Warm V8-inspired low harmonic rumble (integer Hz for clean 1s loop seam)
            sample = 0.48f * std::sin(kTwoPi * 55.0f * t) +
                     0.30f * std::sin(kTwoPi * 110.0f * t) +
                     0.16f * std::sin(kTwoPi * 165.0f * t);
        } else {
            const float n = rng.range(-1.0f, 1.0f);
            filt = filt + 0.18f * (n - filt);
            sample = 0.55f * std::sin(kTwoPi * 920.0f * t + std::sin(kTwoPi * 24.0f * t) * 1.4f) +
                     0.45f * filt;
        }
        data[i] = static_cast<short>(clampf(sample, -1.0f, 1.0f) * 14000.0f);
    }

    wave.data = data;
    Sound s = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return s;
}

void SynthAudio::init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        ready_ = false;
        return;
    }
    ready_ = true;

    sounds_[static_cast<int>(AudioEventType::PistolShot)]   = buildNoiseBurst(0.16f, 2.6f, 0.42f, 1.3f);
    sounds_[static_cast<int>(AudioEventType::SmgShot)]      = buildNoiseBurst(0.11f, 3.1f, 0.48f, 1.6f);
    sounds_[static_cast<int>(AudioEventType::ShotgunShot)]  = buildNoiseBurst(0.32f, 2.0f, 0.28f, 0.75f);
    sounds_[static_cast<int>(AudioEventType::RifleShot)]    = buildNoiseBurst(0.20f, 2.4f, 0.38f, 1.1f);
    sounds_[static_cast<int>(AudioEventType::MeleeSwing)]   = buildNoiseBurst(0.10f, 3.5f, 0.18f, 0.9f);
    sounds_[static_cast<int>(AudioEventType::CarDoor)]      = buildNoiseBurst(0.14f, 3.2f, 0.22f, 0.7f);
    sounds_[static_cast<int>(AudioEventType::CarHorn)]      = buildDualHorn(415.0f, 523.25f, 0.34f);
    sounds_[static_cast<int>(AudioEventType::CarCrash)]     = buildNoiseBurst(0.36f, 1.9f, 0.24f, 0.65f);
    sounds_[static_cast<int>(AudioEventType::Explosion)]    = buildNoiseBurst(0.88f, 1.5f, 0.14f, 0.45f);
    sounds_[static_cast<int>(AudioEventType::CashPickup)]   = buildToneArp({880.0f, 1318.5f}, 0.075f, 0.55f);
    sounds_[static_cast<int>(AudioEventType::ShopBuy)]      = buildToneArp({659.25f, 880.0f, 1174.66f}, 0.075f, 0.6f);
    sounds_[static_cast<int>(AudioEventType::MissionStart)] = buildToneArp({440.0f, 554.37f, 659.25f}, 0.10f, 0.65f);
    sounds_[static_cast<int>(AudioEventType::MissionComplete)] = buildToneArp({523.25f, 659.25f, 783.99f, 1046.5f}, 0.12f, 0.72f);
    sounds_[static_cast<int>(AudioEventType::MissionFail)]  = buildToneArp({392.0f, 349.23f, 293.66f}, 0.15f, 0.65f);

    engineLoop_ = buildContinuousLoop(false);
    driftLoop_ = buildContinuousLoop(true);
    hasLoops_ = true;
}

void SynthAudio::shutdown() {
    if (!ready_) return;
    for (auto& kv : sounds_) {
        UnloadSound(kv.second);
    }
    sounds_.clear();
    if (hasLoops_) {
        UnloadSound(engineLoop_);
        UnloadSound(driftLoop_);
        hasLoops_ = false;
    }
    CloseAudioDevice();
    ready_ = false;
}

void SynthAudio::playEvents(const std::vector<AudioEvent>& events) {
    if (!ready_) return;
    for (const auto& ev : events) {
        auto it = sounds_.find(static_cast<int>(ev.type));
        if (it != sounds_.end()) {
            SetSoundVolume(it->second, ev.volume * 0.65f);
            PlaySound(it->second);
        }
    }
}

void SynthAudio::updateVehicleLoops(bool driving, float speedRatio, bool drifting, bool nitro) {
    if (!ready_ || !hasLoops_) return;

    if (driving) {
        if (!IsSoundPlaying(engineLoop_)) {
            PlaySound(engineLoop_);
        }
        const float pitch = 0.72f + clampf(speedRatio, 0.0f, 1.4f) * 1.15f + (nitro ? 0.25f : 0.0f);
        SetSoundPitch(engineLoop_, pitch);
        SetSoundVolume(engineLoop_, 0.22f + clampf(speedRatio, 0.0f, 1.0f) * 0.18f);
    } else if (IsSoundPlaying(engineLoop_)) {
        StopSound(engineLoop_);
    }

    if (driving && drifting) {
        if (!IsSoundPlaying(driftLoop_)) {
            PlaySound(driftLoop_);
        }
        SetSoundVolume(driftLoop_, 0.26f);
    } else if (IsSoundPlaying(driftLoop_)) {
        StopSound(driftLoop_);
    }
}

} // namespace oc
