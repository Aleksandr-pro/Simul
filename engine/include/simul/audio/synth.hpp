#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace simul::audio {

/**
 * Процедурный нативный PCM-синтезатор звука (работает напрямую в потоке WASAPI / AAudio / ALSA).
 * 0 КБ внешних звуковых ассетов, задержка < 10 мс.
 */
class NativeSoundSynth {
public:
    void init(int sample_rate = 44100) noexcept;
    void shutdown() noexcept;

    void set_muted(bool m) noexcept { muted_.store(m, std::memory_order_relaxed); }
    bool is_muted() const noexcept { return muted_.load(std::memory_order_relaxed); }

    void play_tap() noexcept;
    void play_wave_breakthrough(std::uint8_t wave_index) noexcept;
    void play_cash() noexcept;
    void play_warning() noexcept;

    /** Вызывается из аудио-потока ОС (sokol_audio stream_userdata_cb) */
    void render_frames(float* buffer, int num_frames, int num_channels) noexcept;

private:
    enum class WaveShape : std::uint8_t { Sine = 0, Triangle = 1, Saw = 2 };

    struct Voice {
        bool active = false;
        WaveShape shape = WaveShape::Sine;
        float freq_start = 440.0f;
        float freq_end = 440.0f;
        float phase = 0.0f;
        float elapsed = 0.0f;
        float delay = 0.0f;
        float duration = 0.1f;
        float gain = 0.1f;
    };

    void spawn_voice(WaveShape shape, float f0, float f1, float dur, float gain, float delay = 0.0f) noexcept;

    int sample_rate_ = 44100;
    std::atomic<bool> muted_{false};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<Voice, 16> voices_{};
};

} // namespace simul::audio
