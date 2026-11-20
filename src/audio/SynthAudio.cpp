#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#include <miniaudio.h>

#include "audio/SynthAudio.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace oc {

namespace {

constexpr float kSampleRate = 44100.0f;
constexpr float kTwoPiAudio = 6.28318530718f;

struct OneShotVoice {
    std::atomic<bool> active{false};
    int kind = 0; // 0=noise burst, 1=tone, 2=horn
    float freqA = 440.0f;
    float freqB = 550.0f;
    float phase = 0.0f;
    float phaseB = 0.0f;
    float time = 0.0f;
    float duration = 0.12f;
    float volume = 0.25f;
    float lpAlpha = 0.25f;
    float lpState = 0.0f;
};

} // namespace

struct SynthAudio::Impl {
    ma_device device{};
    bool deviceStarted = false;

    std::atomic<bool> driving{false};
    std::atomic<float> speedRatio{0.0f};
    std::atomic<bool> drifting{false};
    std::atomic<bool> nitro{false};
    std::atomic<float> sirenGain{0.0f};
    std::atomic<bool> raining{false};

    float enginePhase = 0.0f;
    float sirenPhase = 0.0f;
    float sirenLfo = 0.0f;
    float driftLp = 0.0f;
    float rainLp = 0.0f;
    std::uint32_t rng = 0x13579BDFu;

    std::array<OneShotVoice, 16> voices{};
    int nextVoice = 0;

    float nextNoise() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(rng)) * (1.0f / 2147483648.0f);
    }

    static void dataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
        auto* self = static_cast<Impl*>(pDevice->pUserData);
        auto* out = static_cast<float*>(pOutput);
        const float dt = 1.0f / kSampleRate;

        const bool drv = self->driving.load(std::memory_order_relaxed);
        const float spd = std::clamp(self->speedRatio.load(std::memory_order_relaxed), 0.0f, 1.5f);
        const bool drf = self->drifting.load(std::memory_order_relaxed);
        const bool nit = self->nitro.load(std::memory_order_relaxed);
        const float sir = std::clamp(self->sirenGain.load(std::memory_order_relaxed), 0.0f, 1.0f);
        const bool rn = self->raining.load(std::memory_order_relaxed);

        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float sample = 0.0f;
            const float n = self->nextNoise();

            // 1. Multi-harmonic V6/V8 engine synth loop
            if (drv) {
                const float engHz = 44.0f + spd * 112.0f + (nit ? 36.0f : 0.0f);
                self->enginePhase += kTwoPiAudio * engHz * dt;
                if (self->enginePhase > kTwoPiAudio) self->enginePhase -= kTwoPiAudio;
                const float eng = 0.52f * std::sin(self->enginePhase) +
                                  0.28f * std::sin(self->enginePhase * 2.0f) +
                                  0.16f * std::sin(self->enginePhase * 3.0f) +
                                  (nit ? 0.14f * n : 0.03f * n);
                sample += eng * (0.09f + spd * 0.08f);
            }

            // 2. Tire drift screech
            if (drv && drf) {
                self->driftLp += (n - self->driftLp) * 0.18f;
                const float highBand = n - self->driftLp;
                sample += highBand * 0.11f;
            }

            // 3. Korean Seoul Police Siren (wail/hi-lo)
            if (sir > 0.01f) {
                self->sirenLfo += dt * 1.35f;
                const float sweep = 0.5f + 0.5f * std::sin(self->sirenLfo * kTwoPiAudio);
                const float sHz = 660.0f + sweep * 420.0f;
                self->sirenPhase += kTwoPiAudio * sHz * dt;
                if (self->sirenPhase > kTwoPiAudio) self->sirenPhase -= kTwoPiAudio;
                sample += std::sin(self->sirenPhase) * 0.085f * sir;
            }

            // 4. Rain ambience
            if (rn) {
                self->rainLp += (n - self->rainLp) * 0.09f;
                sample += self->rainLp * 0.075f;
            }

            // 5. Polyphonic one-shot SFX voices
            for (auto& v : self->voices) {
                if (!v.active.load(std::memory_order_acquire)) continue;
                v.time += dt;
                if (v.time >= v.duration) {
                    v.active.store(false, std::memory_order_release);
                    continue;
                }
                const float env = std::pow(1.0f - (v.time / v.duration), 1.8f);
                if (v.kind == 0) {
                    v.lpState += (n - v.lpState) * v.lpAlpha;
                    sample += v.lpState * env * v.volume;
                } else if (v.kind == 1) {
                    v.phase += kTwoPiAudio * v.freqA * dt;
                    sample += std::sin(v.phase) * env * v.volume;
                } else {
                    v.phase += kTwoPiAudio * v.freqA * dt;
                    v.phaseB += kTwoPiAudio * v.freqB * dt;
                    sample += 0.5f * (std::sin(v.phase) + std::sin(v.phaseB)) * env * v.volume;
                }
            }

            sample = std::tanh(sample * 1.15f);
            out[i * 2 + 0] = sample;
            out[i * 2 + 1] = sample;
        }
    }

    void trigger(int kind, float fA, float fB, float dur, float vol, float lp = 0.35f) {
        auto& v = voices[static_cast<std::size_t>(nextVoice % static_cast<int>(voices.size()))];
        nextVoice = (nextVoice + 1) % static_cast<int>(voices.size());
        v.active.store(false, std::memory_order_relaxed);
        v.kind = kind;
        v.freqA = fA;
        v.freqB = fB;
        v.phase = 0.0f;
        v.phaseB = 0.0f;
        v.time = 0.0f;
        v.duration = dur;
        v.volume = vol;
        v.lpAlpha = lp;
        v.lpState = 0.0f;
        v.active.store(true, std::memory_order_release);
    }
};

SynthAudio::SynthAudio() : impl_(new Impl()) {}
SynthAudio::~SynthAudio() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

void SynthAudio::init() {
    if (!impl_ || impl_->deviceStarted) return;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = static_cast<ma_uint32>(kSampleRate);
    cfg.dataCallback = Impl::dataCallback;
    cfg.pUserData = impl_;

    if (ma_device_init(nullptr, &cfg, &impl_->device) == MA_SUCCESS) {
        if (ma_device_start(&impl_->device) == MA_SUCCESS) {
            impl_->deviceStarted = true;
        } else {
            ma_device_uninit(&impl_->device);
        }
    }
    // Gracefully runs silent if headless Linux VM has no audio output device!
}

void SynthAudio::shutdown() {
    if (!impl_ || !impl_->deviceStarted) return;
    ma_device_uninit(&impl_->device);
    impl_->deviceStarted = false;
}

void SynthAudio::playEvents(const std::vector<AudioEvent>& events) {
    if (!impl_ || !impl_->deviceStarted) return;
    for (const auto& ev : events) {
        const float v = std::clamp(ev.volume, 0.1f, 1.0f);
        switch (ev.type) {
            case AudioEventType::PistolShot:      impl_->trigger(0, 0, 0, 0.14f, 0.36f * v, 0.45f); break;
            case AudioEventType::SuppressedShot:  impl_->trigger(0, 0, 0, 0.08f, 0.18f * v, 0.25f); break;
            case AudioEventType::HeavyPistolShot: impl_->trigger(0, 0, 0, 0.20f, 0.45f * v, 0.30f); break;
            case AudioEventType::SmgShot:         impl_->trigger(0, 0, 0, 0.09f, 0.28f * v, 0.52f); break;
            case AudioEventType::ShotgunShot:     impl_->trigger(0, 0, 0, 0.28f, 0.52f * v, 0.22f); break;
            case AudioEventType::RifleShot:       impl_->trigger(0, 0, 0, 0.16f, 0.38f * v, 0.38f); break;
            case AudioEventType::SniperShot:      impl_->trigger(0, 0, 0, 0.34f, 0.55f * v, 0.26f); break;
            case AudioEventType::MinigunShot:     impl_->trigger(0, 0, 0, 0.07f, 0.26f * v, 0.48f); break;
            case AudioEventType::MeleeSwing:      impl_->trigger(0, 0, 0, 0.11f, 0.20f * v, 0.18f); break;
            case AudioEventType::CarDoor:         impl_->trigger(0, 0, 0, 0.12f, 0.28f * v, 0.12f); break;
            case AudioEventType::CarHorn:         impl_->trigger(2, 392.0f, 494.0f, 0.32f, 0.26f * v); break;
            case AudioEventType::CarCrash:        impl_->trigger(0, 0, 0, 0.36f, 0.48f * v, 0.16f); break;
            case AudioEventType::Explosion:       impl_->trigger(0, 0, 0, 0.72f, 0.65f * v, 0.09f); break;
            case AudioEventType::CashPickup:      impl_->trigger(1, 880.0f, 0, 0.14f, 0.22f * v); break;
            case AudioEventType::ShopBuy:         impl_->trigger(2, 523.0f, 784.0f, 0.22f, 0.24f * v); break;
            case AudioEventType::MissionStart:    impl_->trigger(2, 440.0f, 659.0f, 0.35f, 0.26f * v); break;
            case AudioEventType::MissionComplete: impl_->trigger(2, 587.0f, 880.0f, 0.45f, 0.30f * v); break;
            case AudioEventType::MissionFail:     impl_->trigger(1, 185.0f, 0, 0.40f, 0.28f * v); break;
            case AudioEventType::Jump:            impl_->trigger(1, 240.0f, 0, 0.09f, 0.14f * v); break;
            case AudioEventType::NpcScream:       impl_->trigger(1, 740.0f, 0, 0.22f, 0.14f * v); break;
            case AudioEventType::PoliceDispatch:  impl_->trigger(2, 660.0f, 880.0f, 0.28f, 0.22f * v); break;
        }
    }
}

void SynthAudio::updateVehicleLoops(bool driving, float speedRatio, bool drifting, bool nitro) {
    if (!impl_) return;
    impl_->driving.store(driving, std::memory_order_relaxed);
    impl_->speedRatio.store(speedRatio, std::memory_order_relaxed);
    impl_->drifting.store(drifting, std::memory_order_relaxed);
    impl_->nitro.store(nitro, std::memory_order_relaxed);
}

void SynthAudio::updatePoliceSiren(float proximityIntensity) {
    if (!impl_) return;
    impl_->sirenGain.store(proximityIntensity, std::memory_order_relaxed);
}

void SynthAudio::updateWeatherAmbience(bool raining) {
    if (!impl_) return;
    impl_->raining.store(raining, std::memory_order_relaxed);
}

} // namespace oc
