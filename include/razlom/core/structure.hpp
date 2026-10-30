// РАЗЛОМ — физика разрушения: несущие способности, обрушение конструкций.
//
// Модель: от «опор» (бедрок, земля, границы рассчитываемой зоны) распространяется
// остаточная несущая способность. Каждый блок съедает часть запаса пропорционально
// своему весу, а горизонтальные «консоли» — втрое больше (изгибающий момент).
// Блоки, до которых запас не дошёл или он стал отрицательным, обрушаются.
#pragma once

#include <cstdint>
#include <vector>

#include "razlom/core/math.hpp"
#include "razlom/core/world.hpp"

namespace razlom {

struct CollapseResult {
    std::vector<IVec3> unstable;
    int scanned = 0;
    bool truncated = false;
    void clear() {
        unstable.clear();
        scanned = 0;
        truncated = false;
    }
};

class StructureSolver {
public:
    static constexpr int kDefaultRadius = 26;
    static constexpr int kMaxUnstable = 420;

    // Оценивает устойчивость конструкции вокруг точки. Регион ограничен по X/Z
    // (риск «тяжёлой физики на телефонах» — ограничиваем зону разрушения).
    // seeds — точки, которые только что перестали быть опорой (разрушенные блоки):
    // обрушение ищется только в связанной с ними части конструкции.
    CollapseResult evaluate(const World& world, const IVec3& origin, int radius = kDefaultRadius,
                            const std::vector<IVec3>* seeds = nullptr);

    size_t bufferBytes() const;

private:
    void ensureBuffers(int radius);
    int localIndex(int lx, int ly, int lz) const { return lx + width_ * (lz + width_ * ly); }

    int radius_ = -1;
    int width_ = 0;
    std::vector<int16_t> capacity_;
    std::vector<uint8_t> reachable_;
    std::vector<std::vector<int>> buckets_;
    CollapseResult result_;
};

}  // namespace razlom
