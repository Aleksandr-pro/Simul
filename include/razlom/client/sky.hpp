// РАЗЛОМ — математика неба: базис камеры, линия горизонта, положение солнца и
// цвет неба в точке экрана. Одна реализация на клиент, превью-рендерер и тесты,
// чтобы картинка неба в игре и в проверках не могла разойтись.
#pragma once

#include <cmath>

#include "razlom/core/math.hpp"

namespace razlom::client {

// Направления камеры по углам обзора. Ими пользуются и Camera, и расчёт неба.
inline Vec3 cameraForward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return Vec3{-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp}.normalized();
}

inline Vec3 cameraRight(float yaw, float pitch) {
    return cross(cameraForward(yaw, pitch), Vec3{0.0f, 1.0f, 0.0f}).normalized();
}

inline Vec3 cameraUp(float yaw, float pitch) {
    return cross(cameraRight(yaw, pitch), cameraForward(yaw, pitch));
}

// Параметры неба на кадр. horizonY — линия горизонта в NDC (от -1 до 1 по
// вертикали), sun* — положение диска солнца в NDC и его видимость.
struct SkyLook {
    float horizonY = 0.0f;
    float sunX = 4.0f;
    float sunY = 4.0f;
    float sunVisible = 0.0f;
};

// Солнце мира: одно направление на клиент, превью и тесты.
inline const Vec3 kDefaultSunDirection{0.35f, 0.82f, 0.45f};

inline constexpr float kSkySoftness = 0.85f;
inline constexpr float kSunRadiusNdc = 0.085f;
inline constexpr float kSkyGazePitch = 0.55f;  // с этого наклона взгляд считается «в небо»

inline float tanHalfFov(float fovDegrees) {
    const float fov = clampf(fovDegrees, 35.0f, 110.0f);
    return std::tan(fov * 0.5f * 3.14159265358979f / 180.0f);
}

inline SkyLook computeSkyLook(float yaw, float pitch, float fovDegrees, float aspect, const Vec3& sunDir) {
    SkyLook look;
    const float tanHalf = std::max(tanHalfFov(fovDegrees), 0.05f);
    const float safeAspect = std::max(aspect, 0.2f);
    const float safePitch = clampf(pitch, -1.55f, 1.55f);

    // Линия горизонта: смотрим вверх — горизонт уходит вниз по экрану.
    look.horizonY = clampf(-std::tan(safePitch) / tanHalf, -6.0f, 6.0f);

    const Vec3 dir = sunDir.normalized();
    const float z = dot(dir, cameraForward(yaw, pitch));
    if (z > 0.05f) {
        look.sunX = (dot(dir, cameraRight(yaw, pitch)) / z) / (safeAspect * tanHalf);
        look.sunY = (dot(dir, cameraUp(yaw, pitch)) / z) / tanHalf;
        look.sunVisible = clampf(1.15f - z * 0.15f, 0.0f, 1.0f);
    }
    return look;
}

inline float skySmoothStep(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x < edge0 ? 0.0f : 1.0f;
    const float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Цвет неба (0..1 на канал) — точное повторение фрагментного шейдера неба.
inline void skyColorAt(float ndcX, float ndcY, const SkyLook& look, float outRgb[3]) {
    constexpr float zenith[3] = {0.20f, 0.47f, 0.85f};
    constexpr float midSky[3] = {0.45f, 0.71f, 0.95f};
    constexpr float horizonColor[3] = {0.81f, 0.90f, 0.99f};

    const float soft = std::max(kSkySoftness, 0.05f);
    const float h = ndcY - look.horizonY;
    const float up = clampf(h / soft, 0.0f, 1.0f);
    const float down = clampf(-h / (soft * 0.75f), 0.0f, 1.0f);
    const float deep = clampf((up - 0.30f) / 0.70f, 0.0f, 1.0f);

    float col[3];
    for (int c = 0; c < 3; ++c) {
        const float a = horizonColor[c] + (midSky[c] - horizonColor[c]) * up;
        const float b = a + (zenith[c] - a) * deep;
        col[c] = b + (horizonColor[c] * 0.90f - b) * (down * 0.65f);
    }

    if (look.sunVisible > 0.001f) {
        const float dx = ndcX - look.sunX;
        const float dy = ndcY - look.sunY;
        const float d = std::sqrt(dx * dx + dy * dy);
        const float radius = std::max(kSunRadiusNdc, 0.01f);
        const float disc = 1.0f - skySmoothStep(radius * 0.55f, radius, d);
        const float glow = std::exp(-d * 2.4f) * 0.32f;
        const float add = (disc * 0.85f + glow) * look.sunVisible;
        col[0] += 1.00f * add;
        col[1] += 0.95f * add;
        col[2] += 0.78f * add;
    }

    outRgb[0] = clampf(col[0], 0.0f, 1.0f);
    outRgb[1] = clampf(col[1], 0.0f, 1.0f);
    outRgb[2] = clampf(col[2], 0.0f, 1.0f);
}

}  // namespace razlom::client
