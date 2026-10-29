#pragma once

#include <array>
#include <cstdint>
#include <cmath>

namespace simul::core {

/**
 * Детерминированный 32-битный ГПСЧ Xoshiro128** (бит-в-бит идентичен на ARM64 и x86_64).
 */
class SeededPrng {
public:
    using State = std::array<std::uint32_t, 4>;

    explicit constexpr SeededPrng(std::uint32_t seed = 20261029u) noexcept {
        std::uint32_t z_state = seed;
        auto splitmix = [&z_state]() constexpr noexcept -> std::uint32_t {
            z_state += 0x9e3779b9u;
            std::uint32_t z = z_state;
            z = (z ^ (z >> 16)) * 0x85ebca6bu;
            z = (z ^ (z >> 13)) * 0xc2b2ae35u;
            return z ^ (z >> 16);
        };
        s_[0] = splitmix();
        s_[1] = splitmix();
        s_[2] = splitmix();
        s_[3] = splitmix();
        if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0u) {
            s_[0] = 0x6d2b79f5u;
        }
    }

    constexpr std::uint32_t next_u32() noexcept {
        const std::uint32_t result = rotl(s_[1] * 5u, 7) * 9u;
        const std::uint32_t t = s_[1] << 9;

        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];

        s_[2] ^= t;
        s_[3] = rotl(s_[3], 11);
        return result;
    }

    /** Число float в полуинтервале [0.0f, 1.0f) */
    constexpr float next_f32() noexcept {
        return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f);
    }

    constexpr float range(float min_v, float max_v) noexcept {
        return min_v + (max_v - min_v) * next_f32();
    }

    constexpr int int_range(int min_v, int max_v) noexcept {
        if (max_v <= min_v) return min_v;
        const auto span = static_cast<std::uint32_t>(max_v - min_v + 1);
        return min_v + static_cast<int>((static_cast<std::uint64_t>(next_u32()) * span) >> 32);
    }

    float gaussian(float mean = 0.0f, float std_dev = 1.0f) noexcept {
        const float sum = next_f32() + next_f32() + next_f32() +
                          next_f32() + next_f32() + next_f32() - 3.0f;
        return mean + (sum * 1.41421356f) * std_dev;
    }

    constexpr bool chance(float probability) noexcept {
        if (probability <= 0.0f) return false;
        if (probability >= 1.0f) return true;
        return next_f32() < probability;
    }

    constexpr State state() const noexcept { return s_; }

    constexpr void set_state(const State& st) noexcept {
        s_ = st;
        if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0u) {
            s_[0] = 1u;
        }
    }

private:
    State s_{};

    static constexpr std::uint32_t rotl(std::uint32_t x, int k) noexcept {
        return (x << k) | (x >> (32 - k));
    }
};

} // namespace simul::core
