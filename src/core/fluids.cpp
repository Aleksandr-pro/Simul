#include "razlom/core/fluids.hpp"

#include <algorithm>
#include <cmath>

#include "razlom/core/blocks.hpp"
#include "razlom/core/rng.hpp"

namespace razlom {

namespace {
constexpr int kTotalBlocks = World::kSizeX * World::kSizeY * World::kSizeZ;
constexpr int kNeighbors[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

inline bool waterCanEnter(uint8_t b) { return b == kAir || (isWater(b) && waterLevel(b) < 7); }
inline bool blocksWater(uint8_t b) { return isSolidBlock(b); }
}  // namespace

FluidSim::FluidSim() : queued_(static_cast<size_t>(kTotalBlocks), 0) {}

void FluidSim::clear() {
    queue_.clear();
    std::fill(queued_.begin(), queued_.end(), 0);
}

void FluidSim::push(int idx) {
    if (idx < 0 || idx >= kTotalBlocks) return;
    if (queued_[static_cast<size_t>(idx)]) return;
    queued_[static_cast<size_t>(idx)] = 1;
    queue_.push_back(idx);
}

void FluidSim::wake(int x, int y, int z) {
    if (!World::inBounds(x, y, z)) return;
    push(World::index(x, y, z));
}

void FluidSim::wakeAround(const World& world, int x, int y, int z, int radius) {
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx) {
                const int nx = x + dx, ny = y + dy, nz = z + dz;
                if (!World::inBounds(nx, ny, nz)) continue;
                if (isWater(world.get(nx, ny, nz))) push(World::index(nx, ny, nz));
            }
}

void FluidSim::wakeWorldColumn(const World& world, int x, int z) {
    for (int y = 0; y < World::kSizeY; ++y) {
        if (isWater(world.get(x, y, z))) push(World::index(x, y, z));
    }
}

void FluidSim::settle(World& world, int x, int y, int z) {
    const uint8_t self = world.get(x, y, z);
    if (!isWater(self)) return;
    const int level = waterLevel(self);

    const uint8_t below = world.getForPhysics(x, y - 1, z);
    if (y > 0 && waterCanEnter(below) && !blocksWater(below)) {
        if (isWater(below)) {
            if (waterLevel(below) < level) {
                world.set(x, y - 1, z, static_cast<uint8_t>(kWaterBase + level - 1));
                world.set(x, y, z, kAir);
                wakeAround(world, x, y - 1, z, 1);
                return;
            }
        } else {  // воздух снизу
            world.set(x, y - 1, z, static_cast<uint8_t>(kWaterBase + std::max(1, level - 1)));
            world.set(x, y, z, kAir);
            wakeAround(world, x, y - 1, z, 1);
            return;
        }
    }

    if (level <= 1) {
        // Одинокая вода уровня 1 высыхает, если ей некуда течь.
        bool enclosed = true;
        for (const auto& n : kNeighbors) {
            const uint8_t nb = world.getForPhysics(x + n[0], y + n[1], z + n[2]);
            if (nb == kAir) enclosed = false;
        }
        if (enclosed) {
            world.set(x, y, z, kAir);
            wakeAround(world, x, y, z, 1);
        }
        return;
    }

    // Растекание в стороны.
    for (int dir : {0, 1, 4, 5}) {
        const int nx = x + kNeighbors[dir][0];
        const int nz = z + kNeighbors[dir][2];
        if (!World::inBounds(nx, y, nz)) continue;
        const uint8_t nb = world.get(nx, y, nz);
        if (nb == kAir) {
            world.set(nx, y, nz, static_cast<uint8_t>(kWaterBase + std::min(6, level - 1)));
            wakeAround(world, nx, y, nz, 1);
        } else if (isWater(nb) && waterLevel(nb) + 1 < level) {
            world.set(nx, y, nz, static_cast<uint8_t>(kWaterBase + level - 1));
            wakeAround(world, nx, y, nz, 1);
        }
    }
}

int FluidSim::tick(World& world, int budget) {
    int changed = 0;
    int processed = 0;
    while (!queue_.empty() && processed < budget) {
        const int idx = queue_.front();
        queue_.pop_front();
        queued_[static_cast<size_t>(idx)] = 0;
        ++processed;

        const int x = World::xOf(idx);
        const int y = World::yOf(idx);
        const int z = World::zOf(idx);
        if (!isWater(world.get(x, y, z))) continue;

        const size_t before = world.changes().size();
        settle(world, x, y, z);
        if (world.changes().size() > before) ++changed;
    }
    return changed;
}

// ---------------------------------------------------------------- огонь

void FireSim::clear() {
    life_.clear();
    order_.clear();
}

void FireSim::ignite(World& world, int x, int y, int z, int life) {
    if (!World::inBounds(x, y, z)) return;
    if (!isFireBlock(world.get(x, y, z))) {
        const uint8_t b = world.get(x, y, z);
        if (isWater(b) || (isSolidBlock(b) && blockDef(b).strength > 200)) return;
        world.set(x, y, z, kFire);
    }
    const int idx = World::index(x, y, z);
    auto it = life_.find(idx);
    if (it == life_.end()) {
        life_.emplace(idx, life);
        order_.push_back(idx);
    } else if (it->second < life) {
        it->second = life;
    }
}

void FireSim::remove(World& world, int idx, int x, int y, int z) {
    if (world.get(x, y, z) == kFire) world.set(x, y, z, kAir);
    life_.erase(idx);
}

int FireSim::tick(World& world, uint32_t tickIndex, int budget) {
    int changed = 0;
    int processed = 0;
    const size_t count = order_.size();
    for (size_t i = 0; i < count && processed < budget; ++i) {
        const int idx = order_.front();
        order_.pop_front();
        auto it = life_.find(idx);
        if (it == life_.end()) continue;
        ++processed;

        const int x = World::xOf(idx);
        const int y = World::yOf(idx);
        const int z = World::zOf(idx);
        if (world.get(x, y, z) != kFire) {
            life_.erase(it);
            continue;
        }

        --it->second;
        bool extinguished = false;
        for (const auto& n : kNeighbors) {
            const uint8_t nb = world.getForPhysics(x + n[0], y + n[1], z + n[2]);
            if (isWater(nb)) extinguished = true;
        }
        if (extinguished) {
            world.set(x, y, z, kAir);
            life_.erase(idx);
            ++changed;
            continue;
        }

        if (it->second <= 0) {
            const uint32_t h = hash3(x, y, z, tickIndex);
            world.set(x, y, z, (h % 5u == 0u) ? kAsh : kAir);
            life_.erase(idx);
            ++changed;
            continue;
        }

        // Горючий сосед загорается (детерминированно по координатам и тику).
        const uint8_t nb = world.getForPhysics(x + kNeighbors[2][0], y + 1, z + kNeighbors[2][2]);
        uint8_t target = nb;
        int tx = x, ty = y + 1, tz = z;
        if (!blockDef(target).flammable) {
            const uint32_t h = hash3(x, y, z, tickIndex * 2654435761u);
            const int dir = static_cast<int>(h % 6u);
            tx = x + kNeighbors[dir][0];
            ty = y + kNeighbors[dir][1];
            tz = z + kNeighbors[dir][2];
            target = world.getForPhysics(tx, ty, tz);
        }
        if (blockDef(target).flammable) {
            bool wet = false;
            for (const auto& n : kNeighbors) {
                if (isWater(world.getForPhysics(tx + n[0], ty + n[1], tz + n[2]))) wet = true;
            }
            if (!wet && World::inBounds(tx, ty, tz)) {
                world.set(tx, ty, tz, kFire);
                const int nidx = World::index(tx, ty, tz);
                life_[nidx] = 12;
                order_.push_back(nidx);
                ++changed;
            }
        }
        order_.push_back(idx);
    }
    return changed;
}

// ---------------------------------------------------------------- взрыв

ExplosionResult explode(World& world, const Vec3& center, float radius, FireSim* fire, FluidSim* fluids) {
    ExplosionResult result;
    radius = clampf(radius, 0.8f, 8.0f);
    const int r = static_cast<int>(std::ceil(radius));
    const int cx = static_cast<int>(std::floor(center.x));
    const int cy = static_cast<int>(std::floor(center.y));
    const int cz = static_cast<int>(std::floor(center.z));
    const float r2 = radius * radius * 1.35f;

    for (int dy = -r; dy <= r; ++dy)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const int x = cx + dx, y = cy + dy, z = cz + dz;
                if (!World::inBounds(x, y, z)) continue;
                const float d2 = static_cast<float>(dx * dx + dy * dy + dz * dz);
                if (d2 > r2) continue;
                const uint8_t b = world.get(x, y, z);
                if (b == kAir) continue;
                const BlockDef& def = blockDef(b);
                // Ближе к центру — разрушает даже прочный камень.
                const float power = (1.0f - std::sqrt(d2) / (radius * 1.16f)) * 255.0f;
                if (def.hardness < 255 && power > static_cast<float>(def.hardness)) {
                    if (def.flammable && fire && (hash3(x, y, z, 7u) % 100u) < 55u) {
                        world.set(x, y, z, kAir);
                        fire->ignite(world, x, y, z, 10);
                        ++result.ignited;
                    } else {
                        world.set(x, y, z, kAir);
                        ++result.destroyed;
                    }
                    result.affected.push_back(IVec3{x, y, z});
                } else if (def.flammable && fire) {
                    fire->ignite(world, x, y, z, 16);
                    ++result.ignited;
                }
            }

    if (fluids) {
        for (const IVec3& p : result.affected) fluids->wakeAround(world, p.x, p.y, p.z, 2);
    }
    return result;
}

}  // namespace razlom
