// РАЗЛОМ — тесты «мир не может пропасть»: математика неба, слой облаков и
// сторож кадра. Всё без GPU: проверяем то, что определяет содержимое кадра.
#include <cmath>
#include <vector>

#include "razlom/client/sky.hpp"
#include "razlom/client/worldguard.hpp"
#include "razlom/core/chunks.hpp"
#include "razlom/core/clouds.hpp"
#include "razlom/core/math.hpp"
#include "razlom/core/mesher.hpp"
#include "razlom/core/physics.hpp"
#include "razlom/core/world.hpp"
#include "test_util.hpp"

using razlom::buildCloudMesh;
using razlom::MeshVertex;
using razlom::Vec3;
using razlom::client::computeSkyLook;
using razlom::client::kDefaultSunDirection;
using razlom::client::kSkyGazePitch;
using razlom::client::kWatchDelay;
using razlom::client::SkyLook;
using razlom::client::skyColorAt;
using razlom::client::tanHalfFov;
using razlom::client::WatchAction;
using razlom::client::WorldWatch;

namespace {

float luminance(const float rgb[3]) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; }

}  // namespace

// --- небо ------------------------------------------------------------------

// Главное требование: где бы ни смотрела камера, небо — это небо (голубой
// градиент), а не чёрный или «пустой» экран.
TEST(sky_is_never_black_or_blank) {
    const float pitches[] = {-1.5f, -0.9f, -0.4f, 0.0f, 0.4f, 0.9f, 1.5f};
    const float yaws[] = {-3.0f, -1.2f, 0.0f, 1.4f, 2.9f};
    for (float pitch : pitches) {
        for (float yaw : yaws) {
            const SkyLook look = computeSkyLook(yaw, pitch, 74.0f, 16.0f / 9.0f, kDefaultSunDirection);
            for (int gy = -4; gy <= 4; ++gy) {
                for (int gx = -4; gx <= 4; ++gx) {
                    float rgb[3];
                    skyColorAt(static_cast<float>(gx) / 5.0f, static_cast<float>(gy) / 5.0f, look, rgb);
                    CHECK(std::isfinite(rgb[0]));
                    CHECK(std::isfinite(rgb[1]));
                    CHECK(std::isfinite(rgb[2]));
                    CHECK(rgb[0] >= 0.0f);
                    CHECK(rgb[1] >= 0.0f);
                    CHECK(rgb[2] >= 0.0f);
                    CHECK(rgb[0] <= 1.0f);
                    CHECK(rgb[1] <= 1.0f);
                    CHECK(rgb[2] <= 1.0f);
                    // небо днём: светлое и синее (синий канал не меньше красного)
                    CHECK(rgb[2] >= rgb[0]);
                    CHECK(luminance(rgb) > 0.25f);
                }
            }
        }
    }
}

// Горизонт едет за наклоном камеры: смотрим вверх — горизонт уходит вниз.
TEST(sky_horizon_follows_pitch) {
    const float aspect = 16.0f / 9.0f;
    const SkyLook up = computeSkyLook(0.0f, 0.8f, 74.0f, aspect, kDefaultSunDirection);
    const SkyLook level = computeSkyLook(0.0f, 0.0f, 74.0f, aspect, kDefaultSunDirection);
    const SkyLook down = computeSkyLook(0.0f, -0.8f, 74.0f, aspect, kDefaultSunDirection);
    CHECK(up.horizonY < level.horizonY);
    CHECK(down.horizonY > level.horizonY);
    CHECK_NEAR(level.horizonY, 0.0f, 1e-4);

    // Широкий угол обзора отодвигает горизонт к краю экрана медленнее.
    const SkyLook wide = computeSkyLook(0.0f, 0.4f, 105.0f, aspect, kDefaultSunDirection);
    const SkyLook narrow = computeSkyLook(0.0f, 0.4f, 45.0f, aspect, kDefaultSunDirection);
    CHECK(wide.horizonY > narrow.horizonY);  // в NDC у широкого горизонта выше (ближе к нулю)
    CHECK(std::fabs(wide.horizonY) < std::fabs(narrow.horizonY));
}

// Крайние значения (нулевой aspect, огромный наклон) не дают NaN/inf.
TEST(sky_survives_degenerate_input) {
    const SkyLook look = computeSkyLook(0.0f, 10.0f, 0.0f, 0.0f, Vec3{0.0f, 0.0f, 0.0f});
    CHECK(std::isfinite(look.horizonY));
    CHECK(std::isfinite(look.sunX));
    CHECK(std::isfinite(look.sunY));
    CHECK(std::isfinite(look.sunVisible));
    CHECK(std::fabs(look.horizonY) <= 6.0f);

    float rgb[3];
    skyColorAt(0.0f, 0.0f, look, rgb);
    CHECK(std::isfinite(rgb[0]) && std::isfinite(rgb[1]) && std::isfinite(rgb[2]));

    CHECK(tanHalfFov(1.0f) > 0.0f);      // fov снизу ограничен
    CHECK(tanHalfFov(1000.0f) < 3.0f);   // и сверху
    CHECK(tanHalfFov(74.0f) > tanHalfFov(60.0f));
}

// Солнце: смотрим прямо на него — диск в центре кадра; смотрим в сторону — нет.
TEST(sky_sun_position_and_visibility) {
    const Vec3 sun = kDefaultSunDirection.normalized();
    // yaw/pitch из направления на солнце
    float yaw = std::atan2(-sun.x, -sun.z);
    const float pitch = std::asin(razlom::clampf(sun.y, -1.0f, 1.0f));
    const SkyLook atSun = computeSkyLook(yaw, pitch, 74.0f, 16.0f / 9.0f, kDefaultSunDirection);
    CHECK(atSun.sunVisible > 0.9f);
    CHECK_NEAR(atSun.sunX, 0.0f, 0.02);
    CHECK_NEAR(atSun.sunY, 0.0f, 0.02);

    // Диск ярче неба рядом с ним
    float centre[3];
    float around[3];
    skyColorAt(0.0f, 0.0f, atSun, centre);
    skyColorAt(0.30f, 0.0f, atSun, around);
    CHECK(luminance(centre) > luminance(around));
    CHECK(luminance(centre) > 0.85f);

    // Смотрим в противоположную сторону — солнца нет
    const SkyLook away = computeSkyLook(yaw + 3.14159f, -pitch, 74.0f, 16.0f / 9.0f, kDefaultSunDirection);
    CHECK(away.sunVisible <= 0.001f);
    float awayCentre[3];
    skyColorAt(away.sunX, away.sunY, away, awayCentre);
    CHECK(luminance(awayCentre) < 0.85f);
}

// --- облака ----------------------------------------------------------------

TEST(clouds_exist_and_are_deterministic) {
    std::vector<MeshVertex> a;
    std::vector<MeshVertex> b;
    buildCloudMesh(a);
    buildCloudMesh(b);
    CHECK(!a.empty());
    CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].x == b[i].x);
        CHECK(a[i].y == b[i].y);
        CHECK(a[i].z == b[i].z);
        CHECK(a[i].r == b[i].r);
    }
    // каждая клетка — 6 граней по 2 треугольника
    CHECK_EQ(a.size() % 36u, 0u);
    const size_t cells = a.size() / 36u;
    CHECK(cells > 15u);   // облаков достаточно, чтобы небо не было пустым
    CHECK(cells < 200u);  // и не на весь мир
}

TEST(clouds_lie_above_the_world_and_cover_it) {
    std::vector<MeshVertex> mesh;
    buildCloudMesh(mesh);
    CHECK(!mesh.empty());
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    bool bright = true;
    for (const MeshVertex& v : mesh) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
        minZ = std::min(minZ, v.z);
        maxZ = std::max(maxZ, v.z);
        if (v.r < 180 || v.g < 180 || v.b < 180) bright = false;
    }
    // облака выше самой высокой точки рельефа и внутри мира
    CHECK(minY >= 70.0f);
    CHECK(maxY <= 95.0f);
    CHECK(minX >= -1.0f);
    CHECK(maxX <= 129.0f);
    CHECK(minZ >= -1.0f);
    CHECK(maxZ <= 129.0f);
    // и заметно шире одной клетки: слой закрывает небо, а не висит точкой
    CHECK(maxX - minX > 40.0f);
    CHECK(maxZ - minZ > 40.0f);
    // все вершины светлые — облака не выглядят серыми пятнами
    CHECK(bright);
}

// --- отсечение чанков ------------------------------------------------------

// Коробка чанка вычисляется из индекса и всегда непустая. Раньше она хранилась
// рядом с GPU-буфером, и любая очистка (resetWorld) обнуляла AABB — фрустум
// отсекал все чанки сразу, и на экране оставалось одно небо.
TEST(chunk_bounds_are_real_boxes) {
    for (int i = 0; i < razlom::World::kChunkCount; ++i) {
        const razlom::AABB box = razlom::chunkBounds(i);
        const razlom::Vec3 size = box.max - box.min;
        CHECK_NEAR(size.x, static_cast<float>(razlom::kChunkSize), 1e-4);
        CHECK_NEAR(size.y, static_cast<float>(razlom::kChunkSize), 1e-4);
        CHECK_NEAR(size.z, static_cast<float>(razlom::kChunkSize), 1e-4);
        CHECK(box.min.x >= 0.0f);
        CHECK(box.max.x <= static_cast<float>(razlom::World::kSizeX) + 1e-3f);
        CHECK(box.max.y <= static_cast<float>(razlom::World::kSizeY) + 1e-3f);
        CHECK(box.max.z <= static_cast<float>(razlom::World::kSizeZ) + 1e-3f);
    }
}

// Вид от спавна обязан видеть десятки чанков: проверка всего пути «камера →
// проекция → фрустум → коробки чанков», без которого мир не рисуется.
TEST(spawn_view_sees_many_chunks) {
    razlom::World world(20261031u);
    razlom::generateWorld(world, 20261031u);
    const razlom::Vec3 eye = razlom::spawnPosition(world) + razlom::Vec3{0.0f, razlom::kEyeHeight, 0.0f};
    const razlom::Vec3 forward = razlom::client::cameraForward(-0.98f, -0.06f);
    const razlom::Mat4 view = razlom::Mat4::lookAt(eye, eye + forward, razlom::Vec3{0.0f, 1.0f, 0.0f});
    const razlom::Mat4 proj = razlom::Mat4::perspective(razlom::radians(74.0f), 16.0f / 9.0f, 0.08f, 260.0f);
    const razlom::Frustum frustum = razlom::Frustum::fromViewProjection(proj * view);

    int visible = 0;
    for (int i = 0; i < razlom::World::kChunkCount; ++i) {
        if (frustum.intersects(razlom::chunkBounds(i))) ++visible;
    }
    CHECK(visible > 30);
    CHECK(visible < razlom::World::kChunkCount);  // и всё-таки что-то отсекается
}

// Ровно та регрессия: обнулённая коробка (то, что делал старый resetWorld)
// не попадает во фрустум ни одна — то есть весь мир исчезает за один кадр.
TEST(zeroed_chunk_bounds_would_hide_the_world) {
    razlom::World world(7u);
    razlom::generateWorld(world, 7u);
    const razlom::Vec3 eye = razlom::spawnPosition(world) + razlom::Vec3{0.0f, razlom::kEyeHeight, 0.0f};
    const razlom::Vec3 forward = razlom::client::cameraForward(-0.98f, -0.06f);
    const razlom::Mat4 view = razlom::Mat4::lookAt(eye, eye + forward, razlom::Vec3{0.0f, 1.0f, 0.0f});
    const razlom::Mat4 proj = razlom::Mat4::perspective(razlom::radians(74.0f), 16.0f / 9.0f, 0.08f, 260.0f);
    const razlom::Frustum frustum = razlom::Frustum::fromViewProjection(proj * view);

    const razlom::AABB zeroed{};
    CHECK(!frustum.intersects(zeroed));
}

// --- сторож кадра ----------------------------------------------------------

TEST(watch_is_silent_while_the_world_is_visible) {
    WorldWatch w;
    for (int i = 0; i < 100; ++i) {
        CHECK(w.tick(0.016f, true, true, 12, 0.0f) == WatchAction::None);
    }
    CHECK_EQ(w.recoveries, 0);
}

// Взгляд в небо — легальный пустой кадр: сторожа не будим, камеру не двигаем.
TEST(watch_ignores_looking_at_the_sky) {
    WorldWatch w;
    for (int i = 0; i < 200; ++i) {
        CHECK(w.tick(0.016f, true, true, 0, kSkyGazePitch + 0.1f) == WatchAction::None);
    }
    CHECK_EQ(w.recoveries, 0);
    CHECK_EQ(w.timer, 0.0f);
}

TEST(watch_waits_before_acting) {
    WorldWatch w;
    CHECK(w.tick(kWatchDelay * 0.5f, true, true, 0, 0.0f) == WatchAction::None);
    CHECK(w.tick(kWatchDelay * 0.4f, true, true, 0, 0.0f) == WatchAction::None);
    CHECK(w.tick(kWatchDelay * 0.2f, true, true, 0, 0.0f) == WatchAction::Rebuild);
}

TEST(watch_escalates_then_logs) {
    WorldWatch w;
    auto step = [&w]() {
        // один «пустой» период: пустой кадр дольше порога
        w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f);
        return w.recoveries;
    };
    CHECK(step() == 1);
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::RebuildAndRespawn);
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::ResetWorldAndRespawn);
    // дальше — без новых телепортов, только редкие записи в журнал (каждый 5-й шаг)
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::None);  // 4
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::Log);   // 5
    for (int i = 0; i < 4; ++i) {                                                  // 6..9
        CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::None);
    }
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::Log);   // 10
}

// Мир вернулся — счётчик восстановлений обнуляется, следующая пропажа снова
// начинается с мягкого шага (пересборки), а не с телепорта.
TEST(watch_resets_when_the_world_is_back) {
    WorldWatch w;
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::Rebuild);
    CHECK(w.tick(0.016f, true, true, 5, 0.0f) == WatchAction::None);
    CHECK_EQ(w.recoveries, 0);
    CHECK(w.tick(kWatchDelay + 0.01f, true, true, 0, 0.0f) == WatchAction::Rebuild);
}

// В меню и на загрузке сторож молчит: там мир и не должен быть в кадре.
TEST(watch_is_quiet_outside_the_game) {
    WorldWatch w;
    for (int i = 0; i < 50; ++i) {
        CHECK(w.tick(0.05f, false, true, 0, 0.0f) == WatchAction::None);
        CHECK(w.tick(0.05f, true, false, 0, 0.0f) == WatchAction::None);
    }
    CHECK_EQ(w.recoveries, 0);
}

// Переход в игру после паузы не должен сразу считаться сбоем.
TEST(watch_does_not_fire_on_the_first_paused_frame) {
    WorldWatch w;
    CHECK(w.tick(0.016f, false, true, 0, 0.0f) == WatchAction::None);
    CHECK(w.tick(kWatchDelay * 0.5f, true, true, 0, 0.0f) == WatchAction::None);
    CHECK(w.timer > 0.0f);
}

int main() { return test::runAll(); }
