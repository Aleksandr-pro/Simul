// РАЗЛОМ — границы чанка. Коробка чанка зависит только от его индекса, поэтому
// она не хранится рядом с GPU-ресурсами: любая «очистка» буфера раньше обнуляла
// AABB, фрустум отсекал все чанки разом и на экране оставалось одно небо.
#pragma once

#include "razlom/core/math.hpp"
#include "razlom/core/world.hpp"

namespace razlom {

inline AABB chunkBounds(int chunkIndex) {
    int x = 0, y = 0, z = 0;
    World::chunkOrigin(chunkIndex, x, y, z);
    return AABB{Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                Vec3{static_cast<float>(x + kChunkSize), static_cast<float>(y + kChunkSize),
                     static_cast<float>(z + kChunkSize)}};
}

}  // namespace razlom
