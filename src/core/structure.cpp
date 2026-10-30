#include "razlom/core/structure.hpp"

#include <algorithm>
#include <cstring>

#include "razlom/core/blocks.hpp"

namespace razlom {
namespace {
constexpr int16_t kUnreached = -30000;
constexpr int kBucketCount = 300;  // запас несущей способности 0..255

inline bool structuralBlock(uint8_t b) {
    return b != kAir && !isWater(b) && b != kFire;
}
}  // namespace

void StructureSolver::ensureBuffers(int radius) {
    if (radius_ == radius) return;
    radius_ = radius;
    width_ = radius * 2 + 1;
    capacity_.assign(static_cast<size_t>(width_) * width_ * World::kSizeY, kUnreached);
    reachable_.assign(capacity_.size(), 0);
    buckets_.assign(kBucketCount, {});
}

size_t StructureSolver::bufferBytes() const {
    return capacity_.capacity() * sizeof(int16_t) + buckets_.capacity() * sizeof(std::vector<int>);
}

CollapseResult StructureSolver::evaluate(const World& world, const IVec3& origin, int radius,
                                        const std::vector<IVec3>* seeds) {
    radius = clampi(radius, 4, 48);
    ensureBuffers(radius);
    result_.clear();

    const int x0 = clampi(origin.x - radius, 0, World::kSizeX - 1);
    const int x1 = clampi(origin.x + radius, 0, World::kSizeX - 1);
    const int z0 = clampi(origin.z - radius, 0, World::kSizeZ - 1);
    const int z1 = clampi(origin.z + radius, 0, World::kSizeZ - 1);
    const int sizeX = x1 - x0 + 1;
    const int sizeZ = z1 - z0 + 1;
    const int maxY = clampi(world.maxSolidY(), 0, World::kSizeY - 1);

    std::fill(capacity_.begin(), capacity_.end(), kUnreached);
    for (auto& bucket : buckets_) bucket.clear();

    // --- опоры: бедрок, нижний слой и границы зоны (за ними мир считаем устойчивым) ---
    auto seed = [&](int lx, int lz, int y, int cap) {
        const int li = localIndex(lx, y, lz);
        if (cap > capacity_[static_cast<size_t>(li)]) {
            capacity_[static_cast<size_t>(li)] = static_cast<int16_t>(cap);
            buckets_[static_cast<size_t>(clampi(cap, 0, kBucketCount - 1))].push_back(li);
        }
    };

    for (int lz = 0; lz < sizeZ; ++lz) {
        for (int lx = 0; lx < sizeX; ++lx) {
            const int x = x0 + lx;
            const int z = z0 + lz;
            const bool border = (x == 0 || z == 0 || x == World::kSizeX - 1 || z == World::kSizeZ - 1 ||
                                 lx == 0 || lz == 0 || lx == sizeX - 1 || lz == sizeZ - 1);
            for (int y = 0; y <= maxY; ++y) {
                const uint8_t b = world.get(x, y, z);
                if (!structuralBlock(b)) continue;
                ++result_.scanned;
                const int strength = blockDef(b).strength;
                if (y == 0 || b == kBedrock) {
                    seed(lx, lz, y, strength > 0 ? strength : 200);
                } else if (border) {
                    seed(lx, lz, y, strength);
                }
            }
        }
    }

    // --- распространение запаса прочности (bucket-queue по убыванию) ---
    static constexpr int kNeighbors[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    for (int capLevel = kBucketCount - 1; capLevel >= 0; --capLevel) {
        auto& bucket = buckets_[static_cast<size_t>(capLevel)];
        for (size_t bi = 0; bi < bucket.size(); ++bi) {
            const int li = bucket[bi];
            const int cap = capacity_[static_cast<size_t>(li)];
            if (cap != capLevel) continue;  // устаревшая запись

            const int ly = li / (width_ * width_);
            const int rem = li % (width_ * width_);
            const int lz = rem / width_;
            const int lx = rem % width_;
            const int x = x0 + lx;
            const int z = z0 + lz;

            for (const auto& n : kNeighbors) {
                const int nx = x + n[0], ny = ly + n[1], nz = z + n[2];
                if (ny < 0 || ny >= World::kSizeY) continue;
                const int nlx = lx + n[0], nlz = lz + n[2];
                if (nlx < 0 || nlz < 0 || nlx >= width_ || nlz >= width_ ||
                    nx < x0 || nx > x1 || nz < z0 || nz > z1) {
                    continue;  // за границей зоны — не считаем
                }
                const uint8_t nb = world.get(nx, ny, nz);
                if (!structuralBlock(nb)) continue;
                const BlockDef& d = blockDef(nb);
                // Вверх — блок «стоит» на нижнем, запас не тратится.
                // Горизонталь и подвес — консоли, они тратят запас на вес блока.
                int cost = 0;
                if (n[1] == 0) cost = d.weight * 3;        // консоль/перекрытие
                else if (n[1] < 0) cost = d.weight * 5;    // свисание
                int newCap = cap - cost;
                if (newCap > d.strength) newCap = d.strength;
                if (newCap <= 0) continue;
                const int nli = localIndex(nlx, ny, nlz);
                if (newCap > capacity_[static_cast<size_t>(nli)]) {
                    capacity_[static_cast<size_t>(nli)] = static_cast<int16_t>(newCap);
                    buckets_[static_cast<size_t>(clampi(newCap, 0, kBucketCount - 1))].push_back(nli);
                }
            }
        }
        bucket.clear();
    }

    // --- обрушение ищем только среди блоков, потерявших опору, начиная от разрушенных cells ---
    if (seeds != nullptr && !seeds->empty()) {
        std::fill(reachable_.begin(), reachable_.end(), 0);
        std::vector<int> frontier;
        auto tryAdd = [&](int lx, int ly, int lz) {
            if (ly < 0 || ly >= World::kSizeY) return;
            if (lx < 0 || lz < 0 || lx >= width_ || lz >= width_) return;
            const int li = localIndex(lx, ly, lz);
            if (reachable_[static_cast<size_t>(li)]) return;
            if (capacity_[static_cast<size_t>(li)] > 0) return;  // ещё держится
            if (!structuralBlock(world.get(x0 + lx, ly, z0 + lz))) return;
            reachable_[static_cast<size_t>(li)] = 1;
            frontier.push_back(li);
        };
        for (const IVec3& seedPos : *seeds) {
            for (const auto& n : kNeighbors) {
                const int nx = seedPos.x + n[0], ny = seedPos.y + n[1], nz = seedPos.z + n[2];
                if (nx < x0 || nx > x1 || nz < z0 || nz > z1) continue;
                tryAdd(nx - x0, ny, nz - z0);
            }
        }
        while (!frontier.empty()) {
            const int li = frontier.back();
            frontier.pop_back();
            const int ly = li / (width_ * width_);
            const int rem = li % (width_ * width_);
            const int lz = rem / width_;
            const int lx = rem % width_;
            if (ly == 0) continue;  // опоры не падают
            if (static_cast<int>(result_.unstable.size()) >= kMaxUnstable) {
                result_.truncated = true;
                break;
            }
            result_.unstable.push_back(IVec3{x0 + lx, ly, z0 + lz});
            for (const auto& n : kNeighbors) tryAdd(lx + n[0], ly + n[1], lz + n[2]);
        }
        return result_;
    }

    // --- полная проверка зоны (без указания разрушенных блоков) ---
    for (int ly = 0; ly <= maxY; ++ly) {
        for (int lz = 0; lz < sizeZ; ++lz) {
            for (int lx = 0; lx < sizeX; ++lx) {
                const int x = x0 + lx;
                const int z = z0 + lz;
                const uint8_t b = world.get(x, ly, z);
                if (!structuralBlock(b)) continue;
                const int li = localIndex(lx, ly, lz);
                const int cap = capacity_[static_cast<size_t>(li)];
                if (cap > 0) continue;
                if (ly == 0 || b == kBedrock) continue;  // опоры не падают
                if (static_cast<int>(result_.unstable.size()) >= kMaxUnstable) {
                    result_.truncated = true;
                    return result_;
                }
                result_.unstable.push_back(IVec3{x, ly, z});
            }
        }
    }
    return result_;
}

}  // namespace razlom
