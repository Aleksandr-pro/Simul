#include "simul/audio/synth.hpp"
#include <cmath>

namespace simul::audio {

namespace {
constexpr float kTwoPi = 6.28318530718f;
}

void NativeSoundSynth::init(int sample_rate) noexcept {
    sample_rate_ = sample_rate > 8000 ? sample_rate : 44100;
}

void NativeSoundSynth::shutdown() noexcept {}

void NativeSoundSynth::spawn_voice(
    WaveShape shape, float f0, float f1, float dur, float gain, float delay) noexcept {
    if (is_muted()) return;
    while (lock_.test_and_set(std::memory_order_acquire)) {}
    for (auto& v : voices_) {
        if (!v.active) {
            v.active = true;
            v.shape = shape;
            v.freq_start = f0;
            v.freq_end = f1;
            v.phase = 0.0f;
            v.elapsed = 0.0f;
            v.delay = delay;
            v.duration = dur;
            v.gain = gain;
            break;
        }
    }
    lock_.clear(std::memory_order_release);
}

void NativeSoundSynth::play_tap() noexcept {
    spawn_voice(WaveShape::Triangle, 540.0f, 220.0f, 0.042f, 0.10f, 0.0f);
}

void NativeSoundSynth::play_wave_breakthrough(std::uint8_t wave_index) noexcept {
    static constexpr float kRoots[5] = {330.0f, 392.0f, 493.88f, 587.33f, 783.99f};
    const float root = kRoots[wave_index < 5 ? wave_index : 4];
    spawn_voice(WaveShape::Sine, root, root, 0.22f, 0.11f, 0.0f);
    spawn_voice(WaveShape::Sine, root * 1.25f, root * 1.25f, 0.22f, 0.10f, 0.055f);
    spawn_voice(WaveShape::Sine, root * 1.50f, root * 1.50f, 0.26f, 0.10f, 0.110f);
}

void NativeSoundSynth::play_cash() noexcept {
    spawn_voice(WaveShape::Sine, 987.77f, 987.77f, 0.08f, 0.09f, 0.0f);
    spawn_voice(WaveShape::Sine, 1318.51f, 1318.51f, 0.20f, 0.09f, 0.06f);
}

void NativeSoundSynth::play_warning() noexcept {
    spawn_voice(WaveShape::Saw, 185.0f, 128.0f, 0.25f, 0.08f, 0.0f);
}

void NativeSoundSynth::render_frames(float* buffer, int num_frames, int num_channels) noexcept {
    const int total = num_frames * num_channels;
    for (int i = 0; i < total; ++i) buffer[i] = 0.0f;
    if (is_muted()) return;

    const float dt = 1.0f / static_cast<float>(sample_rate_);

    while (lock_.test_and_set(std::memory_order_acquire)) {}
    for (int f = 0; f < num_frames; ++f) {
        float mix = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            if (v.delay > 0.0f) {
                v.delay -= dt;
                continue;
            }
            v.elapsed += dt;
            if (v.elapsed >= v.duration) {
                v.active = false;
                continue;
            }
            const float t = v.elapsed / v.duration;
            const float freq = v.freq_start + (v.freq_end - v.freq_start) * t;
            v.phase += freq * dt;
            if (v.phase >= 1.0f) v.phase -= 1.0f;

            float sample = 0.0f;
            switch (v.shape) {
                case WaveShape::Sine:
                    sample = std::sin(v.phase * kTwoPi);
                    break;
                case WaveShape::Triangle:
                    sample = 1.0f - 4.0f * std::fabs(v.phase - 0.5f);
                    break;
                case WaveShape::Saw:
                    sample = 2.0f * v.phase - 1.0f;
                    break;
            }

            const float attack = t < 0.08f ? (t / 0.08f) : 1.0f;
            const float env = attack * (1.0f - t) * (1.0f - t);
            mix += sample * env * v.gain;
        }

        if (mix > 0.9f) mix = 0.9f;
        if (mix < -0.9f) mix = -0.9f;
        for (int ch = 0; ch < num_channels; ++ch) {
            buffer[f * num_channels + ch] = mix;
        }
    }
    lock_.clear(std::memory_order_release);
}

} // namespace simul::audio
