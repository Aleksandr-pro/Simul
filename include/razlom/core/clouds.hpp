// РАЗЛОМ — слой облаков. Одна и та же геометрия строится и в клиенте
// (Renderer), и в превью-рендерере, и в тестах: облака — это настоящий меш мира,
// поэтому даже при взгляде в небо на экране всегда есть геометрия.
#pragma once

#include <cstdint>
#include <vector>

#include "razlom/core/mesher.hpp"

namespace razlom {

inline constexpr float kCloudBaseY = 80.0f;
inline constexpr float kCloudCell = 8.0f;
inline constexpr int kCloudGrid = 16;  // 16x16 клеток по 8 блоков = весь мир
// У облаков нижняя грань не должна выглядеть серой: свет рассеивается.
inline constexpr uint8_t kCloudFaceShade[6] = {238, 232, 255, 224, 250, 246};

namespace detail {

inline uint32_t cloudHash(int a, int b, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(a) * 73856093u ^ static_cast<uint32_t>(b) * 19349663u ^ salt;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline void pushCloudCube(std::vector<MeshVertex>& out, const Vec3& lo, const Vec3& hi) {
    constexpr int faces[6][4][3] = {
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
    };
    for (int face = 0; face < 6; ++face) {
        const uint8_t shade = kCloudFaceShade[face];
        MeshVertex quad[4];
        for (int i = 0; i < 4; ++i) {
            const int* c = faces[face][i];
            quad[i].x = c[0] ? hi.x : lo.x;
            quad[i].y = c[1] ? hi.y : lo.y;
            quad[i].z = c[2] ? hi.z : lo.z;
            quad[i].r = static_cast<uint8_t>(240u * shade / 255u);
            quad[i].g = static_cast<uint8_t>(246u * shade / 255u);
            quad[i].b = 255;
            quad[i].a = 255;
        }
        for (int i : {0, 1, 2, 0, 2, 3}) out.push_back(quad[i]);
    }
}

}  // namespace detail

// Крупные пятна (2x2 клетки) с рваными краями — силуэт облака, а не россыпь
// отдельных кубиков. Полностью детерминировано: одинаково на всех платформах.
inline void buildCloudMesh(std::vector<MeshVertex>& out) {
    out.clear();
    for (int cz = 0; cz < kCloudGrid; ++cz) {
        for (int cx = 0; cx < kCloudGrid; ++cx) {
            const uint32_t blob = detail::cloudHash(cx / 2, cz / 2, 977u);
            const uint32_t cell = detail::cloudHash(cx, cz, 20261031u);
            if (blob % 100u >= 58u || cell % 100u >= 86u) continue;
            const float x0 = static_cast<float>(cx) * kCloudCell;
            const float z0 = static_cast<float>(cz) * kCloudCell;
            const float top = kCloudBaseY + 3.0f + static_cast<float>((cell >> 9) % 4u) * 0.5f;
            detail::pushCloudCube(out, Vec3{x0 - 0.4f, kCloudBaseY, z0 - 0.4f},
                                  Vec3{x0 + kCloudCell + 0.4f, top, z0 + kCloudCell + 0.4f});
        }
    }
}

}  // namespace razlom
