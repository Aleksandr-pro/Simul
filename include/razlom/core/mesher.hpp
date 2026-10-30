// РАЗЛОМ — построение геометрии чанка: грани, запечённый свет и ambient occlusion.
#pragma once

#include <cstdint>
#include <vector>

#include "razlom/core/world.hpp"

namespace razlom {

struct MeshVertex {
    float x, y, z;
    uint8_t r, g, b, a;
};

struct ChunkMesh {
    std::vector<MeshVertex> opaque;
    std::vector<MeshVertex> water;
    void clear() {
        opaque.clear();
        water.clear();
    }
};

// Строит меш чанка. Возвращает число граней (для тестов/бенчмарка).
int buildChunkMesh(const World& world, int chunkIndex, ChunkMesh& out);

}  // namespace razlom
