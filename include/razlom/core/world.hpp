// РАЗЛОМ — воксельный мир: плоский массив блоков, отслеживание изменений и «грязных» чанков.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "razlom/core/blocks.hpp"

namespace razlom {

constexpr int kChunkSize = 16;

struct BlockChange {
    int16_t x = 0, y = 0, z = 0;
    uint8_t block = kAir;
};

class World {
public:
    static constexpr int kSizeX = 128;
    static constexpr int kSizeY = 96;
    static constexpr int kSizeZ = 128;
    static constexpr int kChunksX = kSizeX / kChunkSize;
    static constexpr int kChunksY = kSizeY / kChunkSize;
    static constexpr int kChunksZ = kSizeZ / kChunkSize;
    static constexpr int kChunkCount = kChunksX * kChunksY * kChunksZ;

    explicit World(uint32_t seed = 1);

    uint32_t seed() const { return seed_; }
    void setSeed(uint32_t seed) { seed_ = seed; }

    static bool inBounds(int x, int y, int z) {
        return x >= 0 && x < kSizeX && y >= 0 && y < kSizeY && z >= 0 && z < kSizeZ;
    }
    static int index(int x, int y, int z) { return x + kSizeX * (z + kSizeZ * y); }
    static int xOf(int idx) { return idx % kSizeX; }
    static int zOf(int idx) { return (idx / kSizeX) % kSizeZ; }
    static int yOf(int idx) { return idx / (kSizeX * kSizeZ); }

    uint8_t get(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return kAir;
        return blocks_[static_cast<size_t>(index(x, y, z))];
    }
    // Для физики: ниже нуля — «бедрок» (мир стоит на опоре), выше/за краем — воздух.
    uint8_t getForPhysics(int x, int y, int z) const {
        if (y < 0) return kBedrock;
        if (!inBounds(x, y, z)) return kAir;
        return blocks_[static_cast<size_t>(index(x, y, z))];
    }
    uint8_t getRaw(int idx) const { return blocks_[static_cast<size_t>(idx)]; }

    // Верхняя граница непустых блоков: физике не нужно сканировать пустоту выше.
    int maxSolidY() const { return maxSolidY_; }
    void recomputeMaxSolidY() {
        maxSolidY_ = 0;
        for (int y = kSizeY - 1; y >= 0; --y) {
            for (int z = 0; z < kSizeZ && maxSolidY_ == 0; ++z) {
                for (int x = 0; x < kSizeX; ++x) {
                    if (blocks_[static_cast<size_t>(index(x, y, z))] != kAir) {
                        maxSolidY_ = y;
                        break;
                    }
                }
            }
            if (maxSolidY_ != 0) break;
        }
    }

    bool isSolid(int x, int y, int z) const { return isSolidBlock(get(x, y, z)); }
    bool isOpaque(int x, int y, int z) const { return isOpaqueBlock(get(x, y, z)); }

    // Установка блока с отслеживанием изменения (для репликации и мешинга).
    bool set(int x, int y, int z, uint8_t block) {
        if (!inBounds(x, y, z)) return false;
        const int idx = index(x, y, z);
        if (blocks_[static_cast<size_t>(idx)] == block) return false;
        blocks_[static_cast<size_t>(idx)] = block;
        if (block != kAir && y > maxSolidY_) maxSolidY_ = y;
        markChunkDirtyAt(x, y, z);
        changes_.push_back(BlockChange{static_cast<int16_t>(x), static_cast<int16_t>(y),
                                       static_cast<int16_t>(z), block});
        return true;
    }

    // Установка без записи в журнал изменений (применение серверных правок на клиенте).
    bool applyRemote(int x, int y, int z, uint8_t block) {
        if (!inBounds(x, y, z)) return false;
        const int idx = index(x, y, z);
        if (blocks_[static_cast<size_t>(idx)] == block) return false;
        blocks_[static_cast<size_t>(idx)] = block;
        if (block != kAir && y > maxSolidY_) maxSolidY_ = y;
        markChunkDirtyAt(x, y, z);
        return true;
    }

    void setRaw(int idx, uint8_t block) { blocks_[static_cast<size_t>(idx)] = block; }

    std::vector<BlockChange>& changes() { return changes_; }
    const std::vector<BlockChange>& changes() const { return changes_; }
    std::vector<BlockChange> takeChanges() {
        std::vector<BlockChange> out;
        out.swap(changes_);
        return out;
    }
    void clearChanges() { changes_.clear(); }

    // --- чанки ---
    static int chunkIndex(int cx, int cy, int cz) { return cx + kChunksX * (cz + kChunksZ * cy); }
    static void chunkCoords(int idx, int& cx, int& cy, int& cz) {
        cx = idx % kChunksX;
        cz = (idx / kChunksX) % kChunksZ;
        cy = idx / (kChunksX * kChunksZ);
    }
    static void chunkOrigin(int idx, int& x, int& y, int& z) {
        int cx, cy, cz;
        chunkCoords(idx, cx, cy, cz);
        x = cx * kChunkSize;
        y = cy * kChunkSize;
        z = cz * kChunkSize;
    }

    void markChunkDirtyAt(int x, int y, int z) {
        int cx = x / kChunkSize, cy = y / kChunkSize, cz = z / kChunkSize;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    // соседние чанки нужны, если блок на границе (для корректного AO/граней)
                    const bool borderX = (dx == 0) || (dx < 0 && x % kChunkSize == 0) ||
                                         (dx > 0 && x % kChunkSize == kChunkSize - 1);
                    const bool borderY = (dy == 0) || (dy < 0 && y % kChunkSize == 0) ||
                                         (dy > 0 && y % kChunkSize == kChunkSize - 1);
                    const bool borderZ = (dz == 0) || (dz < 0 && z % kChunkSize == 0) ||
                                         (dz > 0 && z % kChunkSize == kChunkSize - 1);
                    if (!borderX || !borderY || !borderZ) continue;
                    const int nx = cx + dx, ny = cy + dy, nz = cz + dz;
                    if (nx < 0 || ny < 0 || nz < 0 || nx >= kChunksX || ny >= kChunksY || nz >= kChunksZ) continue;
                    markChunkDirty(chunkIndex(nx, ny, nz));
                }
            }
        }
    }

    void markChunkDirty(int chunkIdx) {
        if (chunkIdx < 0 || chunkIdx >= kChunkCount) return;
        if (!dirtyFlags_[static_cast<size_t>(chunkIdx)]) {
            dirtyFlags_[static_cast<size_t>(chunkIdx)] = 1;
            dirtyList_.push_back(chunkIdx);
        }
    }
    void markAllChunksDirty() {
        dirtyFlags_.assign(static_cast<size_t>(kChunkCount), 1);
        dirtyList_.clear();
        for (int i = 0; i < kChunkCount; ++i) dirtyList_.push_back(i);
    }
    std::vector<int>& dirtyChunks() { return dirtyList_; }
    const std::vector<int>& dirtyChunks() const { return dirtyList_; }
    size_t dirtyChunkCount() const { return dirtyList_.size(); }
    // Забирает не больше maxCount грязных чанков (постепенный ремешинг без фризов).
    std::vector<int> takeDirtyChunks(int maxCount) {
        std::vector<int> out;
        const int n = static_cast<int>(dirtyList_.size()) < maxCount ? static_cast<int>(dirtyList_.size()) : maxCount;
        out.assign(dirtyList_.begin(), dirtyList_.begin() + n);
        for (int i = 0; i < n; ++i) dirtyFlags_[static_cast<size_t>(dirtyList_[static_cast<size_t>(i)])] = 0;
        dirtyList_.erase(dirtyList_.begin(), dirtyList_.begin() + n);
        return out;
    }
    void clearDirtyList() {
        for (int idx : dirtyList_) dirtyFlags_[static_cast<size_t>(idx)] = 0;
        dirtyList_.clear();
    }
    bool isChunkDirty(int idx) const { return dirtyFlags_[static_cast<size_t>(idx)] != 0; }

    // Верхняя граница поверхности в колонке (для спавна и простых проверок).
    int surfaceY(int x, int z) const {
        for (int y = kSizeY - 1; y >= 0; --y) {
            const uint8_t b = get(x, y, z);
            if (b != kAir && !isWater(b)) return y;
        }
        return 0;
    }

    void fill(uint8_t block) { blocks_.assign(blocks_.size(), block); }
    const uint8_t* data() const { return blocks_.data(); }
    uint8_t* data() { return blocks_.data(); }
    size_t memoryBytes() const { return blocks_.size() + dirtyFlags_.size() + changes_.capacity() * sizeof(BlockChange); }

private:
    uint32_t seed_ = 1;
    std::vector<uint8_t> blocks_;
    std::vector<uint8_t> dirtyFlags_;
    std::vector<int> dirtyList_;
    std::vector<BlockChange> changes_;
    int maxSolidY_ = 0;
};

// Детерминированная генерация мира (одинакова на всех платформах).
void generateWorld(World& world, uint32_t seed);

}  // namespace razlom
