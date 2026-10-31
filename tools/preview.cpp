// РАЗЛОМ —CPU-превью клиента.
//
// В песочнице нет GPU, поэтому этот инструмент повторяет конвейер рендерера
// на CPU и складывает PNG-кадры: небо, чанки, воду, обломки, игроков и HUD.
// Так видно, что именно увидит игрок, и можно ловить ошибки раскладки/цвета.
//
// Сборка:  g++ -std=c++20 -O2 -Iinclude -Ithird_party/{sokol,stb,fonts} \
//              tools/preview.cpp src/core/*.cpp src/client/{font,hud}.cpp -o build/razlom_preview
// Запуск:  ./build/razlom_preview out_dir

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "razlom/client/font.hpp"
#include "razlom/client/hud.hpp"
#include "razlom/core/blocks.hpp"
#include "razlom/core/math.hpp"
#include "razlom/core/mesher.hpp"
#include "razlom/core/physics.hpp"
#include "razlom/core/session.hpp"
#include "razlom/core/world.hpp"


namespace {

using razlom::Vec3;

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;  // RGB8

    Image(int width, int height) : w(width), h(height), rgb(static_cast<size_t>(width) * height * 3, 0) {}

    void set(int x, int y, float r, float g, float b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
        p[0] = static_cast<uint8_t>(razlom::clampf(r, 0.0f, 255.0f));
        p[1] = static_cast<uint8_t>(razlom::clampf(g, 0.0f, 255.0f));
        p[2] = static_cast<uint8_t>(razlom::clampf(b, 0.0f, 255.0f));
    }
    void blend(int x, int y, float c0, float c1, float c2, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
        const float src[3] = {c0, c1, c2};
        for (int i = 0; i < 3; ++i) {
            const float dst = static_cast<float>(p[i]);
            p[i] = static_cast<uint8_t>(razlom::clampf(dst * (1.0f - a) + src[i] * a, 0.0f, 255.0f));
        }
    }
    void clearTo(float r, float g, float b) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) set(x, y, r, g, b);
        }
    }
    bool writePpm(const std::string& path) const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
        return true;
    }
};

struct ClipVertex {
    Vec3 world;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;  // экранные координаты (пиксели, z — 0..1)
    float invW = 1.0f;
    float r = 1, g = 1, b = 1, a = 1;
};

struct Viewport {
    int w = 0, h = 0;
    razlom::Mat4 vp;
    Vec3 eye;
};

ClipVertex project(const Viewport& vp, const Vec3& p, float r, float g, float b, float a) {
    const float* m = vp.vp.m;
    const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    ClipVertex out;
    out.world = p;
    out.invW = (std::fabs(w) > 1e-6f) ? 1.0f / w : 0.0f;
    out.sx = (x * out.invW * 0.5f + 0.5f) * static_cast<float>(vp.w);
    out.sy = (1.0f - (y * out.invW * 0.5f + 0.5f)) * static_cast<float>(vp.h);
    out.sz = z * out.invW * 0.5f + 0.5f;
    out.r = r;
    out.g = g;
    out.b = b;
    out.a = a;
    return out;
}

// Отсечение по ближней плоскости в clip-space: проще — по z/w.
bool nearClip(ClipVertex& a, ClipVertex& b, ClipVertex& c) {
    const float zn = 0.001f;
    if (a.sz < zn || b.sz < zn || c.sz < zn) {
        // грубо: если все три за плоскостью — выкидываем треугольник
        if (a.sz < zn && b.sz < zn && c.sz < zn) return false;
        return true;  // частичные случаи рисуем как есть (артефакты допустимы в превью)
    }
    return true;
}

void rasterTriangle(Image& img, std::vector<float>& depth, const ClipVertex& a, const ClipVertex& b,
                    const ClipVertex& c, bool blend, float alphaScale) {
    const float minXf = std::min({a.sx, b.sx, c.sx});
    const float maxXf = std::max({a.sx, b.sx, c.sx});
    const float minYf = std::min({a.sy, b.sy, c.sy});
    const float maxYf = std::max({a.sy, b.sy, c.sy});
    const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
    const int maxX = std::min(img.w - 1, static_cast<int>(std::ceil(maxXf)));
    const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
    const int maxY = std::min(img.h - 1, static_cast<int>(std::ceil(maxYf)));
    if (minX > maxX || minY > maxY) return;

    const float area = (b.sx - a.sx) * (c.sy - a.sy) - (c.sx - a.sx) * (b.sy - a.sy);
    if (std::fabs(area) < 1e-6f) return;
    const float invArea = 1.0f / area;

    for (int y = minY; y <= maxY; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            float w0 = ((b.sx - px) * (c.sy - py) - (c.sx - px) * (b.sy - py)) * invArea;
            float w1 = ((c.sx - px) * (a.sy - py) - (a.sx - px) * (c.sy - py)) * invArea;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f) continue;
            w0 = razlom::clampf(w0, 0.0f, 1.0f);
            w1 = razlom::clampf(w1, 0.0f, 1.0f);
            w2 = razlom::clampf(w2, 0.0f, 1.0f);
            const float z = a.sz * w0 + b.sz * w1 + c.sz * w2;
            const size_t idx = static_cast<size_t>(y) * img.w + x;
            // тест глубины всегда, запись — только для непрозрачной геометрии
            if (z >= depth[idx]) continue;
            if (!blend) depth[idx] = z;

            const float r = a.r * w0 + b.r * w1 + c.r * w2;
            const float g = a.g * w0 + b.g * w1 + c.g * w2;
            const float bl = a.b * w0 + b.b * w1 + c.b * w2;
            const float al = (a.a * w0 + b.a * w1 + c.a * w2) * alphaScale;
            if (blend || al < 0.999f) {
                img.blend(x, y, r, g, bl, al);
            } else {
                img.set(x, y, r, g, bl);
            }
        }
    }
}

void drawSky(Image& img) {
    // тот же треугольник, что в renderer.cpp: (-1,-1), (3,-1), (-1,3)
    const float bottom[3] = {66.0f, 120.0f, 176.0f};
    const float top[3] = {132.0f, 196.0f, 240.0f};
    for (int y = 0; y < img.h; ++y) {
        const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(img.h) * 2.0f;
        const float t = razlom::clampf((ndcY + 1.0f) / 4.0f, 0.0f, 1.0f);
        const float c[3] = {bottom[0] + (top[0] - bottom[0]) * t, bottom[1] + (top[1] - bottom[1]) * t,
                            bottom[2] + (top[2] - bottom[2]) * t};
        for (int x = 0; x < img.w; ++x) img.set(x, y, c[0], c[1], c[2]);
    }
}

struct Scene {
    razlom::World world{12345};
    std::vector<razlom::ChunkMesh> meshes;
    razlom::client::FontAtlas font;
};

void buildScene(Scene& scene, uint32_t seed) {
    scene.world = razlom::World(seed);
    razlom::generateWorld(scene.world, seed);
    scene.meshes.resize(razlom::World::kChunkCount);
    for (int i = 0; i < razlom::World::kChunkCount; ++i) {
        razlom::buildChunkMesh(scene.world, i, scene.meshes[static_cast<size_t>(i)]);
    }
}

void drawWorld(Image& img, std::vector<float>& depth, const Scene& scene, const Viewport& vp, bool water,
               float alphaScale) {
    for (const razlom::ChunkMesh& mesh : scene.meshes) {
        const std::vector<razlom::MeshVertex>& tris = water ? mesh.water : mesh.opaque;
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const razlom::MeshVertex& v0 = tris[i];
            const razlom::MeshVertex& v1 = tris[i + 1];
            const razlom::MeshVertex& v2 = tris[i + 2];
            ClipVertex a = project(vp, Vec3{v0.x, v0.y, v0.z}, v0.r, v0.g, v0.b, v0.a / 255.0f);
            ClipVertex b = project(vp, Vec3{v1.x, v1.y, v1.z}, v1.r, v1.g, v1.b, v1.a / 255.0f);
            ClipVertex c = project(vp, Vec3{v2.x, v2.y, v2.z}, v2.r, v2.g, v2.b, v2.a / 255.0f);
            if (a.invW <= 0.0f || b.invW <= 0.0f || c.invW <= 0.0f) continue;
            if (!nearClip(a, b, c)) continue;
            rasterTriangle(img, depth, a, b, c, !water, alphaScale);
        }
    }
}

// --- 2D ---

void drawUi(Image& img, const std::vector<razlom::client::UiVertex>& verts, const razlom::client::FontAtlas& font) {
    const int aw = font.width();
    const int ah = font.height();
    const uint8_t* tex = font.pixels();
    for (size_t i = 0; i + 2 < verts.size(); i += 3) {
        const razlom::client::UiVertex& v0 = verts[i];
        const razlom::client::UiVertex& v1 = verts[i + 1];
        const razlom::client::UiVertex& v2 = verts[i + 2];
        const float minX = std::min({v0.x, v1.x, v2.x});
        const float maxX = std::max({v0.x, v1.x, v2.x});
        const float minY = std::min({v0.y, v1.y, v2.y});
        const float maxY = std::max({v0.y, v1.y, v2.y});
        const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
        const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(maxX)));
        const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
        const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(maxY)));
        if (x0 > x1 || y0 > y1) continue;

        const float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
        if (std::fabs(area) < 1e-6f) continue;
        const float invArea = 1.0f / area;

        for (int y = y0; y <= y1; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                float w0 = ((v1.x - px) * (v2.y - py) - (v2.x - px) * (v1.y - py)) * invArea;
                float w1 = ((v2.x - px) * (v0.y - py) - (v0.x - px) * (v2.y - py)) * invArea;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -0.002f || w1 < -0.002f || w2 < -0.002f) continue;
                w0 = razlom::clampf(w0, 0.0f, 1.0f);
                w1 = razlom::clampf(w1, 0.0f, 1.0f);
                w2 = razlom::clampf(w2, 0.0f, 1.0f);

                const float u = v0.u * w0 + v1.u * w1 + v2.u * w2;
                const float v = v0.v * w0 + v1.v * w1 + v2.v * w2;
                const int tx = razlom::clampi(static_cast<int>(u * static_cast<float>(aw)), 0, aw - 1);
                const int ty = razlom::clampi(static_cast<int>(v * static_cast<float>(ah)), 0, ah - 1);
                const uint8_t* tp = &tex[(static_cast<size_t>(ty) * aw + tx) * 4];
                const float tr = static_cast<float>(tp[0]) / 255.0f;
                const float tg = static_cast<float>(tp[1]) / 255.0f;
                const float tb = static_cast<float>(tp[2]) / 255.0f;
                const float ta = static_cast<float>(tp[3]) / 255.0f;

                auto channel = [&](uint8_t c0, uint8_t c1, uint8_t c2) {
                    return (c0 * w0 + c1 * w1 + c2 * w2) / 255.0f;
                };
                const float r = channel(v0.r, v1.r, v2.r) * tr;
                const float g = channel(v0.g, v1.g, v2.g) * tg;
                const float bl = channel(v0.b, v1.b, v2.b) * tb;
                const float al = channel(v0.a, v1.a, v2.a) * ta;
                if (al <= 0.002f) continue;
                img.blend(x, y, r * 255.0f, g * 255.0f, bl * 255.0f, al);
            }
        }
    }
}

razlom::client::HudModel baseModel(razlom::client::Screen screen) {
    razlom::client::HudModel model;
    model.screen = screen;
    model.roomLink = "razlom://192.168.1.44:47800/QK7M";
    model.roomCode = "QK7M";
    model.status = "Строим чанки…";
    model.loadingProgress = 0.62f;
    model.modeName = "Песочница";
    model.modeTagline = "строй, ломай, взрывай";
    model.creative = true;
    model.playerName = "Игрок";
    model.health = 82;
    model.selectedSlot = 2;
    model.fps = 61;
    model.ping = 18;
    model.playersOnline = 2;
    model.maxPlayers = 4;
    model.toast = "Мир готов — зовите друзей по ссылке";
    model.toastTimer = 2.0f;
    model.showTouch = false;
    // та же диагностическая строка, что клиент показывает в игре (Renderer::statsLine)
    model.stats = "GLES3 · 2340x1080 · 34 выз. · 27 чанков · 64,36,64";
    model.flying = true;
    model.stickDX = 0.35f;
    model.stickDY = 0.8f;
    razlom::client::HudPlayerRow self;
    self.name = "Игрок";
    self.health = 82;
    self.self = true;
    razlom::client::HudPlayerRow friend_;
    friend_.name = "Друг";
    friend_.health = 100;
    model.players = {self, friend_};
    model.log = {"Мир загружен: Песочница", "Друг присоединился"};
    model.tipsTimer = 14.0f;
    model.tips = {"Сломай опору башни — она упадёт", "ЛОМ/СТАВ — блоки, ТНТ — взрыв, ПОЛЁТ — летать",
                  "Esc — пауза и ссылка для друзей"};
    const uint8_t hotbar[9] = {razlom::kStone,  razlom::kBrick,  razlom::kPlanks, razlom::kLog, razlom::kSand,
                               razlom::kGlass, razlom::kStoneBrick, razlom::kDirt, razlom::kTnt};
    for (uint8_t block : hotbar) {
        razlom::client::HudSlot slot;
        slot.block = block;
        slot.label = razlom::blockDef(block).name;
        model.hotbar.push_back(slot);
    }
    return model;
}

void renderScreen(const Scene& scene, razlom::client::Hud& hud, razlom::client::Screen screen, int width,
                  int height, bool touch, const std::string& outPath, bool withWorld, int view = 0) {
    Image img(width, height);
    std::vector<float> depth(static_cast<size_t>(width) * height, 2.0f);

    if (withWorld) {
        drawSky(img);
        razlom::client::Camera cam;
        if (view == 1) {
            // обзор стартовой сцены со стороны: башня, дом, мост
            cam.position = Vec3{static_cast<float>(razlom::World::kSizeX / 2 + 20), 46.0f,
                                static_cast<float>(razlom::World::kSizeZ / 2 + 18)};
            cam.yaw = 0.72f;
            cam.pitch = -0.42f;
        } else if (view == 2) {
            // вид от спавна: туда же, куда смотрит игрок в игре
            cam.position = razlom::spawnPosition(scene.world) + Vec3{0.0f, razlom::kEyeHeight, 0.0f};
            cam.yaw = -0.98f;
            cam.pitch = -0.06f;
        } else {
            cam.position = razlom::spawnPosition(scene.world) + Vec3{0.0f, razlom::kEyeHeight, 0.0f};
            cam.yaw = 0.7f;
            cam.pitch = -0.18f;
        }
        cam.fovDegrees = 74.0f;
        const razlom::Mat4 view =
            razlom::Mat4::lookAt(cam.position, cam.position + cam.forward(), Vec3{0.0f, 1.0f, 0.0f});
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const razlom::Mat4 proj = razlom::Mat4::perspective(razlom::radians(cam.fovDegrees), aspect, 0.08f, 260.0f);
        Viewport vp;
        vp.w = width;
        vp.h = height;
        vp.vp = proj * view;
        vp.eye = cam.position;
        drawWorld(img, depth, scene, vp, false, 1.0f);
        // подсветка блока под прицелом
        for (const razlom::ChunkMesh& mesh : scene.meshes) {
            for (size_t i = 0; i + 2 < mesh.water.size(); i += 3) {
                const razlom::MeshVertex& v0 = mesh.water[i];
                ClipVertex a = project(vp, Vec3{v0.x, v0.y, v0.z}, 120, 180, 235, 1.0f);
                const razlom::MeshVertex& v1 = mesh.water[i + 1];
                ClipVertex b = project(vp, Vec3{v1.x, v1.y, v1.z}, 120, 180, 235, 1.0f);
                const razlom::MeshVertex& v2 = mesh.water[i + 2];
                ClipVertex c = project(vp, Vec3{v2.x, v2.y, v2.z}, 120, 180, 235, 1.0f);
                if (a.invW <= 0.0f || b.invW <= 0.0f || c.invW <= 0.0f) continue;
                rasterTriangle(img, depth, a, b, c, false, 0.72f);
            }
        }
    } else {
        img.clearTo(12.0f, 15.0f, 24.0f);
        drawSky(img);  // как в клиенте: небо есть всегда
    }

    razlom::client::HudInput input;
    input.width = width;
    input.height = height;
    input.mouseX = -100.0f;
    input.mouseY = -100.0f;
    input.touchMode = touch;
    razlom::client::HudModel model = baseModel(screen);
    model.showTouch = touch;
    if (screen == razlom::client::Screen::Join) model.joinAddress = "192.168.1.44:47800";
    if (screen == razlom::client::Screen::Catalog) {
        model.modeName = "Последний выживший";
    }
    razlom::client::HudResult result;
    hud.draw(model, input, result);
    drawUi(img, hud.vertices(), scene.font);

    if (!img.writePpm(outPath)) std::printf("не записал %s\n", outPath.c_str());
    std::printf("%s: %dx%d, ui-вершин %zu\n", outPath.c_str(), width, height, hud.vertices().size());
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : "preview";
    Scene scene;
    if (!scene.font.build(30.0f)) {
        std::printf("атлас шрифта не собрался\n");
        return 1;
    }
    buildScene(scene, 20261030u);
    int blocks = 0;
    for (int y = 0; y < razlom::World::kSizeY; ++y)
        for (int z = 0; z < razlom::World::kSizeZ; ++z)
            for (int x = 0; x < razlom::World::kSizeX; ++x)
                if (scene.world.get(x, y, z) != razlom::kAir) ++blocks;
    std::printf("мир: %d блоков, спавн y=%.1f\n", blocks, razlom::spawnPosition(scene.world).y);

    razlom::client::Hud hud;
    if (!hud.init(&scene.font)) {
        std::printf("HUD не инициализировался\n");
        return 1;
    }

    using razlom::client::Screen;
    renderScreen(scene, hud, Screen::Menu, 1280, 760, false, outDir + "/01-menu-desktop.ppm", false);
    renderScreen(scene, hud, Screen::Menu, 2340, 1080, true, outDir + "/02-menu-phone.ppm", false);
    renderScreen(scene, hud, Screen::Catalog, 2340, 1080, true, outDir + "/03-catalog-phone.ppm", false);
    renderScreen(scene, hud, Screen::Join, 1280, 760, false, outDir + "/04-join-desktop.ppm", false);
    renderScreen(scene, hud, Screen::Loading, 1280, 760, false, outDir + "/05-loading.ppm", false);
    renderScreen(scene, hud, Screen::Playing, 1280, 760, false, outDir + "/06-playing-desktop.ppm", true);
    renderScreen(scene, hud, Screen::Playing, 2340, 1080, true, outDir + "/07-playing-phone.ppm", true);
    renderScreen(scene, hud, Screen::Paused, 1280, 760, false, outDir + "/08-paused.ppm", true);
    renderScreen(scene, hud, Screen::Playing, 1600, 900, false, outDir + "/09-hero.ppm", true, 1);
    renderScreen(scene, hud, Screen::Playing, 1600, 900, false, outDir + "/10-spawn-view.ppm", true, 2);
    renderScreen(scene, hud, Screen::Playing, 2340, 1080, true, outDir + "/11-phone-touch.ppm", true, 2);
    return 0;
}
