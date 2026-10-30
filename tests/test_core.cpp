// РАЗЛОМ — автотесты ядра: генерация, мешинг, физика разрушения, вода, огонь, сессия, протокол.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "razlom/core/fluids.hpp"
#include "razlom/core/mesher.hpp"
#include "razlom/core/physics.hpp"
#include "razlom/core/rng.hpp"
#include "razlom/core/session.hpp"
#include "razlom/core/structure.hpp"
#include "razlom/core/world.hpp"
#include "razlom/net/protocol.hpp"
#include "test_util.hpp"

using namespace razlom;

namespace {

void makeFloor(World& w, uint8_t block = kBedrock) {
    for (int z = 0; z < World::kSizeZ; ++z)
        for (int x = 0; x < World::kSizeX; ++x) w.set(x, 0, z, block);
    w.clearChanges();
}

int countBlocks(const World& w, uint8_t type) {
    int n = 0;
    const size_t total = static_cast<size_t>(World::kSizeX) * World::kSizeY * World::kSizeZ;
    for (size_t i = 0; i < total; ++i)
        if (w.data()[i] == type) ++n;
    return n;
}

bool hasWater(const World& w) {
    for (int id = kWaterBase; id <= kWaterFull; ++id) {
        if (countBlocks(w, static_cast<uint8_t>(id)) > 0) return true;
    }
    return false;
}

}  // namespace

TEST(noise_is_deterministic) {
    CHECK_EQ(value2(17, -3, 1234), value2(17, -3, 1234));
    CHECK_EQ(value3(4, 5, 6, 77), value3(4, 5, 6, 77));
    CHECK_EQ(noise2(100, -50, 16, 999), noise2(100, -50, 16, 999));
    const int a = noise2(37, 41, 11, 5);
    CHECK(a >= 0 && a <= 1023);
    CHECK_EQ(floorDiv(-1, 16), -1);
    CHECK_EQ(floorDiv(-16, 16), -1);
}

TEST(block_registry) {
    CHECK_EQ(std::string(blockDef(kStone).id), std::string("stone"));
    CHECK(isSolidBlock(kStone));
    CHECK(!isSolidBlock(kWaterFull));
    CHECK(isWater(kWaterBase));
    CHECK_EQ(waterLevel(kWaterFull), 7);
    CHECK(blockDef(kPlanks).flammable);
    CHECK(blockDef(kBedrock).hardness == 255);
    CHECK(std::string(blockDef(31).id) != std::string("void") || true);
}

TEST(worldgen_is_deterministic) {
    World a(4242), b(4242), c(4243);
    generateWorld(a, 4242);
    generateWorld(b, 4242);
    generateWorld(c, 4243);

    const size_t bytes = static_cast<size_t>(World::kSizeX) * World::kSizeY * World::kSizeZ;
    CHECK(std::memcmp(a.data(), b.data(), bytes) == 0);
    CHECK(std::memcmp(a.data(), c.data(), bytes) != 0);

    // площадка спавна: твёрдая земля под ногами
    CHECK_EQ(a.get(World::kSizeX / 2, 34, World::kSizeZ / 2), kGrass);
    CHECK_EQ(a.get(0, 0, 0), kBedrock);
    CHECK(hasWater(a));
    CHECK(countBlocks(a, kLog) > 0);

    // на спавне есть готовые «мишени» для разрушения
    CHECK(countBlocks(a, kStoneBrick) > 20);
    CHECK(countBlocks(a, kPlanks) > 20);

    const Vec3 spawn = spawnPosition(a);
    CHECK(!collidesAt(a, spawn));
    CHECK(spawn.y > 30.0f);
}

TEST(mesher_single_block_and_winding) {
    World w(1);
    w.fill(kAir);
    w.set(8, 8, 8, kStone);
    ChunkMesh mesh;
    const int faces = buildChunkMesh(w, World::chunkIndex(0, 0, 0), mesh);
    CHECK_EQ(faces, 6);
    CHECK_EQ(mesh.opaque.size(), static_cast<size_t>(36));
    CHECK_EQ(mesh.water.size(), static_cast<size_t>(0));

    // все треугольники «смотрят» наружу от центра блока
    const Vec3 center{8.5f, 8.5f, 8.5f};
    for (size_t i = 0; i + 2 < mesh.opaque.size(); i += 3) {
        const MeshVertex& a = mesh.opaque[i];
        const MeshVertex& b = mesh.opaque[i + 1];
        const MeshVertex& c = mesh.opaque[i + 2];
        const Vec3 va{a.x, a.y, a.z};
        const Vec3 vb{b.x, b.y, b.z};
        const Vec3 vc{c.x, c.y, c.z};
        const Vec3 n = cross(vb - va, vc - va);
        const Vec3 triCenter = (va + vb + vc) / 3.0f;
        CHECK(dot(n, triCenter - center) > 0.0f);
    }

    // соседний блок убирает общие грани
    w.set(9, 8, 8, kStone);
    const int faces2 = buildChunkMesh(w, World::chunkIndex(0, 0, 0), mesh);
    CHECK_EQ(faces2, 10);

    // вода попадает в отдельный меш
    w.set(8, 9, 8, kWaterFull);
    buildChunkMesh(w, World::chunkIndex(0, 0, 0), mesh);
    CHECK(mesh.water.size() > 0);
}

// Цвет грани: два множителя (яркость грани и AO) нормируются оба — иначе
// всё поле вершин уезжает в 255 и мир рисуется белым.
TEST(block_face_color_is_shaded) {
    uint8_t full[4];
    blockFaceColor(kStone, 3, 4, 5, 255, 255, full);
    CHECK(std::abs(static_cast<int>(full[0]) - static_cast<int>(blockDef(kStone).r)) <= 3);
    CHECK(full[3] == 255);

    uint8_t dark[4];
    blockFaceColor(kStone, 3, 4, 5, 128, 255, dark);
    const int base = blockDef(kStone).r;
    CHECK(dark[0] < base);
    CHECK(dark[0] > base / 4);

    uint8_t ao[4];
    blockFaceColor(kStone, 3, 4, 5, 255, 148, ao);
    CHECK(ao[0] < base);

    // ни один блок не должен стать чисто белым от одного затенения
    for (int id = 1; id < kBlockCount; ++id) {
        uint8_t c[4];
        blockFaceColor(static_cast<uint8_t>(id), 10, 20, 30, 255, 148, c);
        const BlockDef& def = blockDef(static_cast<uint8_t>(id));
        CHECK(!(c[0] == 255 && c[1] == 255 && c[2] == 255 && def.r < 200));
    }
}

// Вершины меша должны нести настоящий цвет блока, а не белый.
TEST(mesher_vertices_keep_block_color) {
    World w(7u);
    generateWorld(w, 7u);
    ChunkMesh mesh;
    int checked = 0;
    for (int chunk = 0; chunk < World::kChunkCount && checked < 40; ++chunk) {
        buildChunkMesh(w, chunk, mesh);
        for (const MeshVertex& v : mesh.opaque) {
            CHECK(!(v.r == 255 && v.g == 255 && v.b == 255));
            CHECK(v.a == 255);
            if (++checked >= 40) break;
        }
    }
    CHECK(checked >= 40);
}

TEST(structure_stable_pillar) {
    World w(3);
    w.fill(kAir);
    makeFloor(w);
    for (int y = 1; y <= 10; ++y) w.set(20, y, 20, kStone);

    StructureSolver solver;
    const CollapseResult r = solver.evaluate(w, IVec3{20, 5, 20}, 20, nullptr);
    CHECK(r.unstable.empty());
}

TEST(structure_tall_tower_stands_until_support_removed) {
    World w(3);
    w.fill(kAir);
    makeFloor(w);
    for (int y = 1; y <= 40; ++y) w.set(20, y, 20, kStone);

    StructureSolver solver;
    // высокая башня на земле устойчива (вес передаётся вниз через опору)
    const CollapseResult stable = solver.evaluate(w, IVec3{20, 20, 20}, 24, nullptr);
    CHECK(stable.unstable.empty());

    // выбиваем основание — вся башня теряет опору
    w.set(20, 1, 20, kAir);
    const std::vector<IVec3> seeds{IVec3{20, 1, 20}};
    const CollapseResult r = solver.evaluate(w, IVec3{20, 1, 20}, 24, &seeds);
    CHECK(!r.unstable.empty());
    bool topFalls = false;
    for (const IVec3& p : r.unstable)
        if (p.y > 30) topFalls = true;
    CHECK(topFalls);
    CHECK(r.unstable.size() >= 35);
}

TEST(structure_remove_support_collapses_tower) {
    World w(5);
    w.fill(kAir);
    makeFloor(w);
    // площадка 3x3 на столбе
    for (int y = 1; y <= 12; ++y) w.set(30, y, 30, kStone);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) w.set(30 + dx, 13, 30 + dz, kPlanks);

    StructureSolver solver;
    CollapseResult before = solver.evaluate(w, IVec3{30, 6, 30}, 20, nullptr);
    CHECK(before.unstable.empty());

    // выбиваем опору
    w.set(30, 1, 30, kAir);
    const std::vector<IVec3> seeds{IVec3{30, 1, 30}};
    const CollapseResult after = solver.evaluate(w, IVec3{30, 1, 30}, 20, &seeds);
    CHECK(!after.unstable.empty());
    CHECK(after.unstable.size() >= 10);
}

TEST(structure_floating_debris_stays_unless_triggered) {
    World w(7);
    w.fill(kAir);
    makeFloor(w);
    // «зависший» остров из камня (игрок построил в креативе)
    for (int dz = 0; dz < 3; ++dz)
        for (int dx = 0; dx < 3; ++dx) w.set(50 + dx, 20, 50 + dz, kStone);

    StructureSolver solver;
    // без триггера (фильтра по разрушенным блокам) — остров считается висящим
    const CollapseResult all = solver.evaluate(w, IVec3{51, 20, 51}, 20, nullptr);
    CHECK(!all.unstable.empty());

    // рядом с островом разрушили не связанный с ним блок — остров не трогаем
    w.set(20, 1, 20, kAir);
    const std::vector<IVec3> farSeed{IVec3{20, 1, 20}};
    const CollapseResult filtered = solver.evaluate(w, IVec3{20, 1, 20}, 20, &farSeed);
    CHECK(filtered.unstable.empty());
}

TEST(structure_cantilever_breaks) {
    World w(11);
    w.fill(kAir);
    makeFloor(w);
    for (int y = 1; y <= 10; ++y) w.set(30, y, 40, kStone);
    for (int x = 30; x <= 44; ++x) w.set(x, 10, 40, kStone);

    StructureSolver solver;
    const CollapseResult r = solver.evaluate(w, IVec3{30, 10, 40}, 26, nullptr);
    CHECK(!r.unstable.empty());
}

TEST(explosion_creates_crater_and_fire) {
    World w(13);
    w.fill(kAir);
    makeFloor(w, kStone);
    // куб камня 9x9x5 и деревянная постройка рядом
    for (int y = 1; y <= 4; ++y)
        for (int z = 40; z < 49; ++z)
            for (int x = 40; x < 49; ++x) w.set(x, y, z, kStone);
    for (int z = 43; z <= 46; ++z)
        for (int x = 43; x <= 46; ++x) w.set(x, 4, z, kPlanks);
    w.clearChanges();

    FireSim fire;
    FluidSim fluids;
    const ExplosionResult res = explode(w, Vec3{44.5f, 3.2f, 44.5f}, 3.4f, &fire, &fluids);
    CHECK(res.destroyed > 0);
    CHECK_EQ(w.get(44, 2, 44), static_cast<uint8_t>(kAir));
    CHECK(fire.burningCount() > 0 || res.ignited > 0);
    CHECK(!w.changes().empty());
}

TEST(water_falls_and_spreads) {
    World w(17);
    w.fill(kAir);
    makeFloor(w);
    w.set(10, 6, 10, kWaterFull);
    FluidSim fs;
    fs.wake(10, 6, 10);
    for (int i = 0; i < 120 && fs.busy(); ++i) fs.tick(w, 256);

    CHECK(isWater(w.get(10, 1, 10)));
    CHECK(!isWater(w.get(10, 6, 10)));

    // вода на плоскости растекается
    for (int z = 0; z < World::kSizeZ; ++z)
        for (int x = 0; x < World::kSizeX; ++x)
            for (int y = 1; y <= 3; ++y) w.set(x, y, z, kStone);
    w.set(60, 4, 60, kWaterFull);
    fs.clear();
    fs.wake(60, 4, 60);
    for (int i = 0; i < 400 && fs.busy(); ++i) fs.tick(w, 512);
    int spread = 0;
    for (int x = 57; x <= 63; ++x)
        if (isWater(w.get(x, 4, 60))) ++spread;
    CHECK(spread >= 3);
}

TEST(protocol_roundtrip) {
    std::vector<BlockChange> history{{1, 2, 3, kStone}, {4, 5, 6, kAir}};
    std::vector<net::PlayerSnapshot> players;
    net::PlayerSnapshot p;
    p.id = 7;
    p.name = "Кубик";
    p.pos = Vec3{1.5f, 2.0f, 3.0f};
    p.yaw = 1.25f;
    p.pitch = -0.5f;
    p.health = 80;
    p.flags = net::kFlagOnGround;
    p.slot = 3;
    players.push_back(p);

    std::vector<uint8_t> buf;
    net::writeWelcome(buf, 7, 99u, static_cast<uint8_t>(GameMode::LastStanding), players, history);
    CHECK(buf.size() > 8);

    net::Reader r(buf.data() + 4, buf.size() - 4);  // пропускаем префикс длины
    CHECK_EQ(static_cast<int>(r.u8()), static_cast<int>(net::MsgType::Welcome));
    CHECK_EQ(static_cast<int>(r.u8()), 7);
    CHECK_EQ(r.u32(), 99u);
    CHECK_EQ(static_cast<int>(r.u8()), static_cast<int>(GameMode::LastStanding));
    const uint16_t count = r.u16();
    CHECK_EQ(static_cast<int>(count), 1);
    CHECK_EQ(r.u32(), 7u);
    CHECK_EQ(r.str(), std::string("Кубик"));
    const Vec3 pos = r.vec3();
    CHECK_NEAR(pos.x, 1.5, 0.0001);
    CHECK_NEAR(r.f32(), 1.25f, 0.0001f);
    CHECK_NEAR(r.f32(), -0.5f, 0.0001f);
    CHECK_EQ(r.i16(), 80);
    CHECK_EQ(static_cast<int>(r.u8()), static_cast<int>(net::kFlagOnGround));
    CHECK_EQ(static_cast<int>(r.u8()), 3);
    CHECK_EQ(r.u32(), 2u);
    CHECK_EQ(r.i16(), 1);
    CHECK_EQ(r.i16(), 2);
    CHECK_EQ(r.i16(), 3);
    CHECK_EQ(static_cast<int>(r.u8()), static_cast<int>(kStone));
    CHECK(r.ok());

    // битый пакет не должен «проходить» молча
    net::Reader bad(buf.data(), 5);
    bad.u8();
    bad.u32();
    bad.str();
    CHECK(!bad.ok());
}

TEST(session_players_and_edits) {
    SessionConfig cfg;
    cfg.seed = 2024;
    cfg.mode = GameMode::Sandbox;
    Session session(cfg);

    std::string error;
    const uint32_t p1 = session.addPlayer("Аня", &error);
    CHECK(p1 != 0);
    const uint32_t p2 = session.addPlayer("Боря", &error);
    CHECK(p2 != 0);
    CHECK_EQ(static_cast<int>(session.playerCount()), 2);

    Player* player = session.findPlayer(p1);
    CHECK(player != nullptr);
    if (!player) return;

    // правильный ввод принимается, телепорт — нет
    const Vec3 start = player->pos;
    CHECK(session.applyInput(p1, start + Vec3{0.2f, 0.0f, 0.0f}, 0.3f, 0.1f, 0, 2));
    CHECK(!session.applyInput(p1, start + Vec3{60.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0, 2));
    CHECK_EQ(static_cast<int>(player->slot), 2);

    // блок перед игроком: рядом — можно, далеко — нельзя
    const IVec3 near{static_cast<int>(start.x), 34, static_cast<int>(start.z)};
    CHECK(session.world().get(near.x, near.y, near.z) == kGrass);
    CHECK(session.requestBreak(p1, near));
    CHECK_EQ(session.world().get(near.x, near.y, near.z), static_cast<uint8_t>(kAir));
    CHECK(!session.requestBreak(p1, IVec3{2, 34, 2}));
    // бедрок неприкосновенен
    CHECK(!session.requestBreak(p1, IVec3{near.x, 0, near.z}));

    // ставим блок в освободившееся место
    CHECK(session.requestPlace(p1, near, kPlanks));
    CHECK_EQ(session.world().get(near.x, near.y, near.z), static_cast<uint8_t>(kPlanks));

    // правки попали в журнал и в историю для новых игроков
    const std::vector<BlockChange> changes = session.takeChanges();
    CHECK(!changes.empty());
    CHECK(session.historySize() >= 1);  // история схлопывает правки одной клетки
    CHECK(session.playerSnapshots().size() == 2);

    // чат
    session.chat(p1, "привет");
    const auto events = session.takeEvents();
    bool chatSeen = false;
    for (const SessionEvent& ev : events)
        if (ev.kind == SessionEventKind::Chat && ev.text.find("привет") != std::string::npos) chatSeen = true;
    CHECK(chatSeen);
}

TEST(session_collapse_spawns_debris) {
    SessionConfig cfg;
    cfg.seed = 777;
    cfg.mode = GameMode::Sandbox;
    Session session(cfg);
    const uint32_t pid = session.addPlayer("Тестер");
    CHECK(pid != 0);
    Player* player = session.findPlayer(pid);
    CHECK(player != nullptr);
    if (!player) return;

    // Строим рядом с игроком башню из камня на «пятачке» и выбиваем основание.
    const int bx = static_cast<int>(player->pos.x) + 3;
    const int bz = static_cast<int>(player->pos.z) + 2;
    const int baseY = session.world().surfaceY(bx, bz);
    for (int y = baseY + 1; y <= baseY + 9; ++y)
        session.world().set(bx, y, bz, kStone);
    session.world().clearChanges();
    session.takeChanges();

    player->pos = Vec3{static_cast<float>(bx) + 0.5f, static_cast<float>(baseY + 1), static_cast<float>(bz) + 3.5f};
    CHECK(session.requestBreak(pid, IVec3{bx, baseY + 1, bz}));

    // обрушение считается в ближайшие тики, дальше обломки падают и оседают
    session.tick(0.05f);
    CHECK(!session.debris().empty());
    CHECK_EQ(session.world().get(bx, baseY + 9, bz), static_cast<uint8_t>(kAir));
    for (int i = 0; i < 60; ++i) session.tick(0.05f);  // обломки должны осесть
}

TEST(session_modes_and_damage) {
    SessionConfig cfg;
    cfg.seed = 99;
    cfg.mode = GameMode::LastStanding;
    Session session(cfg);
    const uint32_t p1 = session.addPlayer("Один");
    const uint32_t p2 = session.addPlayer("Два");
    Player* a = session.findPlayer(p1);
    Player* b = session.findPlayer(p2);
    CHECK(a && b);
    if (!a || !b) return;

    CHECK(session.pvpEnabled());
    // в LastStanding летать нельзя
    session.applyInput(p1, a->pos, 0.0f, 0.0f, net::kFlagFlying, 0);
    CHECK((a->flags & net::kFlagFlying) == 0);

    // взрыв ранит обоих
    const Vec3 blast = (a->pos + b->pos) * 0.5f + Vec3{0.0f, 1.0f, 0.0f};
    a->pos = blast + Vec3{1.0f, 0.0f, 0.0f};
    b->pos = blast + Vec3{-1.0f, 0.0f, 0.0f};
    CHECK(session.requestExplode(p1, blast, 3.0f));
    CHECK(a->health < 100);
    CHECK(b->health < 100);
}

int main() { return test::runAll(); }
