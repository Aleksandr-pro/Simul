// РАЗЛОМ — движение игрока с коллизиями и воксельный рейкаст.
#pragma once

#include <cmath>

#include "razlom/core/math.hpp"
#include "razlom/core/world.hpp"

namespace razlom {

constexpr float kPlayerHalfWidth = 0.32f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kEyeHeight = 1.62f;
constexpr float kGravity = 26.0f;
constexpr float kJumpSpeed = 8.6f;
constexpr float kReachDistance = 7.0f;

struct MoveResult {
    Vec3 pos;
    bool onGround = false;
    bool hitCeiling = false;
    float fallDistance = 0.0f;
};

inline bool collidesAt(const World& world, const Vec3& feet) {
    const int x0 = static_cast<int>(std::floor(feet.x - kPlayerHalfWidth));
    const int x1 = static_cast<int>(std::floor(feet.x + kPlayerHalfWidth - 1e-4f));
    const int y0 = static_cast<int>(std::floor(feet.y));
    const int y1 = static_cast<int>(std::floor(feet.y + kPlayerHeight - 1e-4f));
    const int z0 = static_cast<int>(std::floor(feet.z - kPlayerHalfWidth));
    const int z1 = static_cast<int>(std::floor(feet.z + kPlayerHalfWidth - 1e-4f));
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                // За границей мира геометрии нет вообще: если считать её пустотой,
                // игрок уходит в бесконечность и «мир пропадает» (видно только небо).
                // Поэтому всё, что вне коробки мира, — твёрдое: пол, потолок, стены.
                if (!World::inBounds(x, y, z)) return true;
                if (isSolidBlock(world.get(x, y, z))) return true;
            }
    return false;
}

// Держим игрока внутри мира. Даже если он «телепортировался» правкой мира или
// сервер прислал позицию с края, камера не улетит за пределы геометрии.
inline Vec3 clampToWorld(const Vec3& pos) {
    const float marginXZ = kPlayerHalfWidth + 1e-3f;
    const float maxY = static_cast<float>(World::kSizeY) - kPlayerHeight - 1e-3f;
    Vec3 p = pos;
    p.x = clampf(p.x, marginXZ, static_cast<float>(World::kSizeX) - marginXZ);
    p.z = clampf(p.z, marginXZ, static_cast<float>(World::kSizeZ) - marginXZ);
    p.y = clampf(p.y, 0.0f, maxY);
    return p;
}

// Точка внутри твёрдого блока: камера в породе видна как «пустой мир», потому
// что все грани смотрят наружу и отсекаются. Такое положение надо чинить.
inline bool pointInsideSolid(const World& world, const Vec3& p) {
    const int x = static_cast<int>(std::floor(p.x));
    const int y = static_cast<int>(std::floor(p.y));
    const int z = static_cast<int>(std::floor(p.z));
    if (!World::inBounds(x, y, z)) return true;
    return isSolidBlock(world.get(x, y, z));
}

// Ищем ближайшее свободное место для игрока: вверх до 12 блоков, затем вниз.
// Возвращает false, если места нет вовсе (тогда вызывающий отправит на спавн).
inline bool findFreeSpot(const World& world, const Vec3& from, Vec3& out) {
    const float base = std::floor(from.y);
    for (int up = 1; up <= 12; ++up) {
        Vec3 p = from;
        p.y = base + static_cast<float>(up);
        if (!collidesAt(world, p)) {
            out = p;
            return true;
        }
    }
    for (int down = 1; down <= 12; ++down) {
        Vec3 p = from;
        p.y = base - static_cast<float>(down);
        if (p.y < 0.0f) break;
        if (!collidesAt(world, p)) {
            out = p;
            return true;
        }
    }
    return false;
}

// Пошаговое движение с раздельным разрешением по осям (AABB vs воксели).
inline MoveResult movePlayer(const World& world, const Vec3& pos, const Vec3& velocity, float dt, bool flying) {
    MoveResult res;
    res.pos = pos;
    Vec3 vel = velocity;

    const int steps = clampi(static_cast<int>(std::ceil(vel.length() * dt / 0.2f)), 1, 12);
    const float sub = dt / static_cast<float>(steps);

    for (int step = 0; step < steps; ++step) {
        // X
        Vec3 tryPos = res.pos;
        tryPos.x += vel.x * sub;
        if (!collidesAt(world, tryPos)) {
            res.pos.x = tryPos.x;
        } else {
            vel.x = 0.0f;
        }
        // Z
        tryPos = res.pos;
        tryPos.z += vel.z * sub;
        if (!collidesAt(world, tryPos)) {
            res.pos.z = tryPos.z;
        } else {
            vel.z = 0.0f;
        }
        // Y
        tryPos = res.pos;
        tryPos.y += vel.y * sub;
        if (!collidesAt(world, tryPos)) {
            res.pos.y = tryPos.y;
            if (vel.y < 0.0f) res.fallDistance += -vel.y * sub;
        } else {
            if (vel.y < 0.0f) {
                res.onGround = true;
                if (!flying) res.pos.y = std::floor(res.pos.y + 0.001f) + 0.0f;
            } else {
                res.hitCeiling = true;
            }
            vel.y = 0.0f;
        }
    }

    if (flying) res.onGround = false;
    return res;
}

// Рейкаст по вокселям (Amanatides & Woo).
struct RayHit {
    bool hit = false;
    IVec3 block;      // блок, в который попали
    IVec3 previous;   // соседний блок перед попаданием (куда ставить)
    IVec3 normal;
    float distance = 0.0f;
};

inline bool solidForRay(uint8_t b) { return b != kAir && !isWater(b) && b != kFire; }

inline RayHit raycastVoxel(const World& world, const Vec3& origin, const Vec3& dir, float maxDistance) {
    RayHit hit;
    Vec3 d = dir.normalized();
    if (d.lengthSq() < 1e-8f) return hit;

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = d.x > 0 ? 1 : (d.x < 0 ? -1 : 0);
    const int stepY = d.y > 0 ? 1 : (d.y < 0 ? -1 : 0);
    const int stepZ = d.z > 0 ? 1 : (d.z < 0 ? -1 : 0);

    const float tDeltaX = std::fabs(d.x) > 1e-8f ? std::fabs(1.0f / d.x) : 1e30f;
    const float tDeltaY = std::fabs(d.y) > 1e-8f ? std::fabs(1.0f / d.y) : 1e30f;
    const float tDeltaZ = std::fabs(d.z) > 1e-8f ? std::fabs(1.0f / d.z) : 1e30f;

    const float fracX = origin.x - static_cast<float>(x);
    const float fracY = origin.y - static_cast<float>(y);
    const float fracZ = origin.z - static_cast<float>(z);

    float tMaxX = stepX > 0 ? (1.0f - fracX) * tDeltaX : (stepX < 0 ? fracX * tDeltaX : 1e30f);
    float tMaxY = stepY > 0 ? (1.0f - fracY) * tDeltaY : (stepY < 0 ? fracY * tDeltaY : 1e30f);
    float tMaxZ = stepZ > 0 ? (1.0f - fracZ) * tDeltaZ : (stepZ < 0 ? fracZ * tDeltaZ : 1e30f);

    int prevX = x, prevY = y, prevZ = z;
    float t = 0.0f;
    for (int i = 0; i < 512; ++i) {
        if (World::inBounds(x, y, z) && solidForRay(world.get(x, y, z))) {
            hit.hit = true;
            hit.block = IVec3{x, y, z};
            hit.previous = IVec3{prevX, prevY, prevZ};
            hit.distance = t;
            hit.normal = IVec3{prevX - x, prevY - y, prevZ - z};
            return hit;
        }
        prevX = x;
        prevY = y;
        prevZ = z;
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
        }
        if (t > maxDistance) break;
    }
    return hit;
}

inline Vec3 spawnPosition(const World& world) {
    const int x = World::kSizeX / 2;
    const int z = World::kSizeZ / 2;
    const int y = world.surfaceY(x, z);
    return {static_cast<float>(x) + 0.5f, static_cast<float>(y + 1) + 0.02f, static_cast<float>(z) + 0.5f};
}

}  // namespace razlom
