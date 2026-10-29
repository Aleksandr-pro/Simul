#pragma once

#include <algorithm>
#include <cmath>

namespace simul::core {

template <typename T>
constexpr T clamp_val(T v, T lo, T hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline constexpr float lerp_f32(float a, float b, float t) noexcept {
    const float ct = clamp_val(t, 0.0f, 1.0f);
    return a + (b - a) * ct;
}

inline float sigmoid_f32(float x) noexcept {
    if (x >= 18.0f) return 1.0f;
    if (x <= -18.0f) return 0.0f;
    return 1.0f / (1.0f + std::exp(-x));
}

/**
 * Пружинный интегратор для плавной 60/120 Гц анимации карточек, скролла и счётчиков.
 */
class SpringValue {
public:
    float value = 0.0f;
    float target = 0.0f;
    float velocity = 0.0f;
    float stiffness = 220.0f;
    float damping = 24.0f;

    constexpr SpringValue() = default;
    explicit constexpr SpringValue(float initial, float k = 220.0f, float d = 24.0f) noexcept
        : value(initial), target(initial), stiffness(k), damping(d) {}

    constexpr void set_target(float t) noexcept { target = t; }

    constexpr void snap(float v) noexcept {
        value = v;
        target = v;
        velocity = 0.0f;
    }

    bool is_settled(float eps = 0.002f) const noexcept {
        return std::fabs(target - value) < eps && std::fabs(velocity) < eps;
    }

    float step(float dt_sec) noexcept {
        if (is_settled()) {
            value = target;
            velocity = 0.0f;
            return value;
        }
        const float dt = clamp_val(dt_sec, 0.0005f, 0.033f);
        const float force = -stiffness * (value - target) - damping * velocity;
        velocity += force * dt;
        value += velocity * dt;
        return value;
    }
};

} // namespace simul::core
