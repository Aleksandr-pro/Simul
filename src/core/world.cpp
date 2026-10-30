#include "razlom/core/world.hpp"

namespace razlom {

World::World(uint32_t seed) : seed_(seed) {
    blocks_.assign(static_cast<size_t>(kSizeX) * kSizeY * kSizeZ, kAir);
    dirtyFlags_.assign(static_cast<size_t>(kChunkCount), 0);
    changes_.reserve(4096);
    dirtyList_.reserve(kChunkCount);
}

}  // namespace razlom
