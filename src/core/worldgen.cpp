// РАЗЛОМ — детерминированная генерация мира.
// Только целочисленная арифметика в шуме: клиент и сервер строят одинаковый мир.
#include <cstdint>
#include <cstdlib>

#include "razlom/core/rng.hpp"
#include "razlom/core/math.hpp"
#include "razlom/core/world.hpp"

namespace razlom {
namespace {

constexpr int kWaterLevel = 28;
constexpr int kSpawnX = World::kSizeX / 2;
constexpr int kSpawnZ = World::kSizeZ / 2;
constexpr int kPlateauHeight = 34;

int isqrt(int v) {
    if (v <= 0) return 0;
    int r = 0;
    int bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

int terrainHeight(int x, int z, uint32_t seed) {
    int h = 20;
    h += noise2(x, z, 48, seed) * 26 / 1023;
    h += noise2(x, z, 17, seed ^ 0x51ed270bu) * 10 / 1023;
    h += noise2(x, z, 7, seed ^ 0x9e3779b9u) * 3 / 1023;

    const int dx = x - kSpawnX;
    const int dz = z - kSpawnZ;
    const int d = isqrt(dx * dx + dz * dz);
    if (d <= 15) {
        h = kPlateauHeight;
    } else if (d < 26) {
        h = kPlateauHeight + (h - kPlateauHeight) * (d - 15) / 11;
    }
    return clampi(h, 6, World::kSizeY - 12);
}

void put(World& w, int x, int y, int z, uint8_t b) {
    if (World::inBounds(x, y, z)) w.setRaw(World::index(x, y, z), b);
}

void box(World& w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t b) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) put(w, x, y, z, b);
}

void hollowBox(World& w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t wall, uint8_t inner) {
    box(w, x0, y0, z0, x1, y1, z1, wall);
    box(w, x0 + 1, y0 + 1, z0 + 1, x1 - 1, y1 - 1, z1 - 1, inner);
}

void tree(World& w, int x, int z, int groundY, Rng& rng) {
    const int trunk = 4 + static_cast<int>(rng.next() % 3u);
    for (int i = 1; i <= trunk; ++i) put(w, x, groundY + i, z, kLog);
    const int top = groundY + trunk;
    for (int dy = -2; dy <= 1; ++dy) {
        const int radius = dy <= -1 ? 2 : 1;
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dz == 0 && dy < 1) continue;
                if (std::abs(dx) == radius && std::abs(dz) == radius && (rng.next() & 1u)) continue;
                const int yy = top + dy;
                if (World::inBounds(x + dx, yy, z + dz) && w.get(x + dx, yy, z + dz) == kAir)
                    put(w, x + dx, yy, z + dz, kLeaves);
            }
    }
}

// Башня-«мишень»: стоит на двух деревянных опорах — выбей опору, и она упадёт.
void demoTower(World& w, int cx, int cz, int groundY) {
    box(w, cx - 1, groundY + 1, cz - 1, cx + 1, groundY + 1, cz + 1, kPlanks);
    box(w, cx - 1, groundY + 2, cz - 1, cx - 1, groundY + 2, cz - 1, kLog);
    box(w, cx + 1, groundY + 2, cz - 1, cx + 1, groundY + 2, cz - 1, kLog);

    hollowBox(w, cx - 1, groundY + 3, cz - 1, cx + 1, groundY + 11, cz + 1, kStoneBrick, kAir);
    // окна
    put(w, cx, groundY + 7, cz - 1, kAir);
    put(w, cx, groundY + 7, cz + 1, kAir);
    put(w, cx - 1, groundY + 7, cz, kAir);
    put(w, cx + 1, groundY + 7, cz, kAir);
    // деревянный настил и крыша
    box(w, cx - 2, groundY + 11, cz - 2, cx + 2, groundY + 11, cz + 2, kPlanks);
    box(w, cx - 2, groundY + 12, cz - 2, cx + 2, groundY + 12, cz + 2, kBrick);
    put(w, cx, groundY + 13, cz, kTnt);
}

// Домик: доски, стеклянные окна, кирпичная крыша.
void demoHouse(World& w, int x0, int z0, int groundY) {
    const int x1 = x0 + 7;
    const int z1 = z0 + 6;
    hollowBox(w, x0, groundY + 1, z0, x1, groundY + 4, z1, kPlanks, kAir);
    box(w, x0, groundY, z0, x1, groundY, z1, kStoneBrick);
    // окна
    for (int x = x0 + 2; x <= x1 - 2; x += 3) {
        put(w, x, groundY + 2, z0, kGlass);
        put(w, x, groundY + 3, z0, kGlass);
        put(w, x, groundY + 2, z1, kGlass);
        put(w, x, groundY + 3, z1, kGlass);
    }
    put(w, x0, groundY + 2, z0 + 3, kGlass);
    put(w, x1, groundY + 2, z0 + 3, kGlass);
    // крыша со скатом
    for (int i = 0; i <= 3; ++i) {
        box(w, x0 - 1 + i, groundY + 5 + i, z0 - 1 + i, x1 + 1 - i, groundY + 5 + i, z1 + 1 - i, kBrick);
    }
    // дверной проём
    put(w, x0 + 3, groundY + 1, z1, kAir);
    put(w, x0 + 3, groundY + 2, z1, kAir);
}

// Мостик между башней и домом — ещё одна «хрупкая» конструкция.
void demoBridge(World& w, int x0, int x1, int y, int z) {
    for (int x = x0; x <= x1; ++x) {
        put(w, x, y, z, kPlanks);
        if ((x - x0) % 3 == 0) put(w, x, y - 1, z, kLog);
    }
    for (int x = x0; x <= x1; ++x) {
        if ((x - x0) % 4 == 0) put(w, x, y + 1, z, kPlanks);
    }
}

}  // namespace

void generateWorld(World& world, uint32_t seed) {
    world.setSeed(seed);
    world.fill(kAir);

    Rng rng(seed ^ 0x7f4a7c15u);

    // --- рельеф, вода, пещеры ---
    for (int z = 0; z < World::kSizeZ; ++z) {
        for (int x = 0; x < World::kSizeX; ++x) {
            const int h = terrainHeight(x, z, seed);
            const bool beach = h <= kWaterLevel + 1;

            for (int y = 0; y <= h; ++y) {
                uint8_t block = kStone;
                if (y == 0) block = kBedrock;
                else if (y >= h - 3) block = beach ? kSand : kDirt;
                if (y == h) block = beach ? kSand : kGrass;

                // пещеры (не трогаем верхние 4 блока и бедрок)
                if (y > 3 && y <= h - 4) {
                    const int cave = noise3(x, y, z, 11, seed ^ 0xcafe1234u);
                    if (cave > 716) block = kAir;
                }
                if (block == kAir) continue;
                put(world, x, y, z, block);
            }
            for (int y = h + 1; y <= kWaterLevel; ++y) put(world, x, y, z, kWaterFull);
        }
    }

    // --- деревья ---
    for (int z = 2; z < World::kSizeZ - 2; ++z) {
        for (int x = 2; x < World::kSizeX - 2; ++x) {
            const int dx = x - kSpawnX;
            const int dz = z - kSpawnZ;
            if (dx * dx + dz * dz < 24 * 24) continue;  // не застраиваем площадку спавна
            if (hash2(x, z, seed ^ 0xa5a5a5a5u) % 149u != 0u) continue;
            const int h = world.surfaceY(x, z);
            if (world.get(x, h, z) != kGrass) continue;
            tree(world, x, z, h, rng);
        }
    }

    // --- стартовая площадка со «сценой разрушения» ---
    const int groundY = kPlateauHeight;
    for (int z = kSpawnZ - 14; z <= kSpawnZ + 14; ++z)
        for (int x = kSpawnX - 14; x <= kSpawnX + 14; ++x) {
            for (int y = groundY + 1; y < World::kSizeY; ++y) put(world, x, y, z, kAir);
            put(world, x, groundY, z, kGrass);
            put(world, x, groundY - 1, z, kDirt);
        }

    demoTower(world, kSpawnX + 9, kSpawnZ - 6, groundY);
    demoHouse(world, kSpawnX - 14, kSpawnZ - 4, groundY);
    demoBridge(world, kSpawnX - 6, kSpawnX + 7, groundY + 4, kSpawnZ - 6);

    // факельная «дорожка» из досок к башне
    for (int x = kSpawnX; x <= kSpawnX + 6; ++x) put(world, x, groundY + 1, kSpawnZ, kPlanks);
    put(world, kSpawnX + 6, groundY + 2, kSpawnZ, kLog);
    put(world, kSpawnX + 6, groundY + 3, kSpawnZ, kFire);

    world.recomputeMaxSolidY();
    world.markAllChunksDirty();
    world.clearChanges();
}

}  // namespace razlom
