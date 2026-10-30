// РАЗЛОМ — замеры производительности ядра (главный риск проекта: физика на телефонах).
#include <chrono>
#include <cstdio>

#include "razlom/core/fluids.hpp"
#include "razlom/core/mesher.hpp"
#include "razlom/core/structure.hpp"
#include "razlom/core/world.hpp"

using namespace razlom;

namespace {
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
}  // namespace

int main() {
    std::printf("РАЗЛОМ — бенчмарк ядра\n\n");

    World world(1337);
    auto t0 = Clock::now();
    generateWorld(world, 1337);
    std::printf("генерация мира 128x96x128:   %7.2f мс\n", msSince(t0));

    // Полный меш мира
    t0 = Clock::now();
    ChunkMesh mesh;
    size_t vertices = 0;
    int faces = 0;
    int nonEmpty = 0;
    for (int i = 0; i < World::kChunkCount; ++i) {
        faces += buildChunkMesh(world, i, mesh);
        vertices += mesh.opaque.size() + mesh.water.size();
        if (!mesh.opaque.empty()) ++nonEmpty;
    }
    const double meshMs = msSince(t0);
    std::printf("меш всего мира (384 чанка):  %7.2f мс  (%.2f мс/чанк)\n", meshMs, meshMs / World::kChunkCount);
    std::printf("  граней: %d, вершин: %zu, непустых чанков: %d, VRAM ~%.1f МБ\n", faces, vertices, nonEmpty,
                static_cast<double>(vertices) * 16.0 / (1024.0 * 1024.0));

    // Точечная перестройка одного чанка (типичная реакция на разрушение)
    t0 = Clock::now();
    constexpr int kRemesh = 200;
    for (int i = 0; i < kRemesh; ++i) buildChunkMesh(world, 137, mesh);
    std::printf("перестройка 1 чанка:         %7.3f мс\n", msSince(t0) / kRemesh);

    // Обрушение башни
    StructureSolver solver;
    const int bx = World::kSizeX / 2 + 10;
    const int bz = World::kSizeZ / 2 + 10;
    const int base = world.surfaceY(bx, bz);
    for (int y = base + 1; y <= base + 24; ++y)
        for (int dz = 0; dz < 3; ++dz)
            for (int dx = 0; dx < 3; ++dx) world.set(bx + dx, y, bz + dz, kStone);

    t0 = Clock::now();
    constexpr int kSolves = 50;
    size_t unstable = 0;
    for (int i = 0; i < kSolves; ++i) {
        const CollapseResult res = solver.evaluate(world, IVec3{bx, base + 5, bz}, 26, nullptr);
        unstable += res.unstable.size();
    }
    std::printf("полная проверка зоны 53x53:  %7.3f мс  (найдено %zu блоков суммарно)\n",
                msSince(t0) / kSolves, unstable);

    // Типичный игровой случай: выбили основание, ищем только потерявшую опору часть.
    std::vector<IVec3> seeds;
    for (int dz = 0; dz < 3; ++dz) {
        for (int dx = 0; dx < 3; ++dx) {
            world.set(bx + dx, base + 1, bz + dz, kAir);
            seeds.push_back(IVec3{bx + dx, base + 1, bz + dz});
        }
    }
    t0 = Clock::now();
    size_t seededUnstable = 0;
    for (int i = 0; i < kSolves; ++i) {
        const CollapseResult res = solver.evaluate(world, seeds[0], 26, &seeds);
        seededUnstable += res.unstable.size();
    }
    std::printf("обрушение после удара:      %7.3f мс  (в среднем %.1f блоков)\n",
                msSince(t0) / kSolves, static_cast<double>(seededUnstable) / kSolves);
    std::printf("  память решателя: %.1f МБ\n", static_cast<double>(solver.bufferBytes()) / (1024.0 * 1024.0));

    // Взрыв
    t0 = Clock::now();
    constexpr int kBoom = 100;
    for (int i = 0; i < kBoom; ++i) {
        explode(world, Vec3{static_cast<float>(bx), static_cast<float>(base + 2), static_cast<float>(bz)}, 4.0f,
                nullptr, nullptr);
    }
    std::printf("взрыв радиуса 4:             %7.3f мс\n", msSince(t0) / kBoom);

    std::printf("\nОриентир для мобильных: весь кадр при 60 FPS — 16.6 мс.\n");
    return 0;
}
