#include "razlom/core/mesher.hpp"

#include <cstring>

#include "razlom/core/blocks.hpp"

namespace razlom {
namespace {

// Направления граней: 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z
constexpr int kDir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// Углы грани в порядке CCW (если смотреть снаружи).
constexpr int kCorners[6][4][3] = {
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},  // +X
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},  // -X
    {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},  // +Y
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},  // -Y
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},  // +Z
    {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},  // -Z
};

// Яркость грани (запечённый «солнечный» свет).
constexpr uint8_t kFaceShade[6] = {212, 226, 255, 138, 236, 198};

// Множители AO по количеству перекрытых углов.
constexpr uint8_t kAoShade[4] = {148, 190, 222, 255};

inline uint8_t solidOpaque(const World& w, int x, int y, int z) {
    return isOpaqueBlock(w.getForPhysics(x, y, z)) ? 1 : 0;
}

void pushQuad(std::vector<MeshVertex>& verts, const World& w, int x, int y, int z, int dir,
              uint8_t block, bool ao, bool isWaterMesh) {
    const int* c = kDir[dir];
    const int nrm[3] = {x + c[0], y + c[1], z + c[2]};

    // Касательные оси грани: для X-граней (Y,Z), для Y-граней (X,Z), для Z-граней (X,Y).
    const int ta = (dir < 2) ? 1 : 0;
    const int tb = (dir < 2) ? 2 : ((dir < 4) ? 2 : 1);

    MeshVertex quad[4];
    for (int i = 0; i < 4; ++i) {
        const int* corner = kCorners[dir][i];
        uint8_t shade = kFaceShade[dir];
        if (ao && !isWaterMesh) {
            const int sa = corner[ta] * 2 - 1;
            const int sb = corner[tb] * 2 - 1;
            int p1[3] = {nrm[0], nrm[1], nrm[2]};
            int p2[3] = {nrm[0], nrm[1], nrm[2]};
            int pd[3] = {nrm[0], nrm[1], nrm[2]};
            p1[ta] += sa;
            p2[tb] += sb;
            pd[ta] += sa;
            pd[tb] += sb;
            const int side1 = solidOpaque(w, p1[0], p1[1], p1[2]);
            const int side2 = solidOpaque(w, p2[0], p2[1], p2[2]);
            const int diag = solidOpaque(w, pd[0], pd[1], pd[2]);
            int occl = side1 + side2;
            if (occl == 2) {
                occl = 3;
            } else {
                occl += diag;
            }
            shade = static_cast<uint8_t>(static_cast<int>(shade) * kAoShade[occl] / 255);
        }

        uint8_t color[4];
        blockFaceColor(block, x, y, z, shade, 255, color);
        MeshVertex v{};
        v.x = static_cast<float>(x + corner[0]);
        v.y = static_cast<float>(y + corner[1]);
        v.z = static_cast<float>(z + corner[2]);
        v.r = color[0];
        v.g = color[1];
        v.b = color[2];
        v.a = 255;
        quad[i] = v;
    }
    // два треугольника: 0-1-2 и 0-2-3 (CCW снаружи)
    verts.push_back(quad[0]);
    verts.push_back(quad[1]);
    verts.push_back(quad[2]);
    verts.push_back(quad[0]);
    verts.push_back(quad[2]);
    verts.push_back(quad[3]);
}

}  // namespace

int buildChunkMesh(const World& world, int chunkIndex, ChunkMesh& out) {
    out.clear();
    int ox = 0, oy = 0, oz = 0;
    World::chunkOrigin(chunkIndex, ox, oy, oz);

    int faces = 0;
    for (int ly = 0; ly < kChunkSize; ++ly) {
        for (int lz = 0; lz < kChunkSize; ++lz) {
            for (int lx = 0; lx < kChunkSize; ++lx) {
                const int x = ox + lx, y = oy + ly, z = oz + lz;
                const uint8_t block = world.get(x, y, z);
                if (block == kAir) continue;

                if (isWater(block)) {
                    const int level = waterLevel(block);
                    // Верхняя грань только у поверхности воды.
                    const uint8_t above = world.getForPhysics(x, y + 1, z);
                    if (!isWater(above)) {
                        pushQuad(out.water, world, x, y, z, 2, block, false, true);
                        ++faces;
                    }
                    // Боковые грани — только там, где рядом воздух.
                    for (int dir : {0, 1, 4, 5}) {
                        const uint8_t nb = world.getForPhysics(x + kDir[dir][0], y + kDir[dir][1], z + kDir[dir][2]);
                        if (nb == kAir) {
                            pushQuad(out.water, world, x, y, z, dir, block, false, true);
                            ++faces;
                        }
                    }
                    (void)level;
                    continue;
                }

                for (int dir = 0; dir < 6; ++dir) {
                    const uint8_t nb = world.getForPhysics(x + kDir[dir][0], y + kDir[dir][1], z + kDir[dir][2]);
                    if (isOpaqueBlock(nb)) continue;
                    if (nb == block && (block == kGlass || block == kLeaves)) continue;
                    pushQuad(out.opaque, world, x, y, z, dir, block, true, false);
                    ++faces;
                }
            }
        }
    }
    return faces;
}

}  // namespace razlom
