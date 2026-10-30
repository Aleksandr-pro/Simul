// РАЗЛОМ — рендерер на sokol_gfx: чанки, вода, обломки, игроки, небо и 2D-интерфейс.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "razlom/core/math.hpp"
#include "razlom/core/mesher.hpp"
#include "razlom/core/session.hpp"
#include "razlom/core/world.hpp"
#include "sokol_gfx.h"

namespace razlom::client {

struct Camera {
    Vec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovDegrees = 72.0f;

    Vec3 forward() const {
        const float cp = std::cos(pitch);
        return Vec3{-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp}.normalized();
    }
    Vec3 right() const {
        const Vec3 f = forward();
        return cross(f, Vec3{0.0f, 1.0f, 0.0f}).normalized();
    }
    Vec3 up() const { return cross(right(), forward()); }
};

struct UiVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct Vertex3D {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct FrameData {
    const World* world = nullptr;
    Camera camera;
    const std::vector<net::DebrisSnapshot>* debris = nullptr;
    const std::vector<net::PlayerSnapshot>* players = nullptr;
    uint32_t selfPlayerId = 0;
    bool hasHighlight = false;
    IVec3 highlight;
    const std::vector<UiVertex>* ui = nullptr;
    float time = 0.0f;
    int maxRemesh = 6;
    bool showSky = true;
    Vec3 sunDirection{0.35f, 0.82f, 0.45f};
};

class Renderer {
public:
    bool init();
    void shutdown();

    // Строит (частично) меши чанков. Используется на экране загрузки и в кадре.
    int buildChunks(const World& world, int maxChunks);
    bool chunksReady(const World& world) const;
    int pendingChunks(const World& world) const;

    void resetWorld(World& world);
    void render(const FrameData& frame);
    // Загружает атлас шрифта. Возвращает false, если текстура не создалась —
    // тогда интерфейс рисуется без текста (панели и кнопки остаются видны).
    bool initFontTexture(const uint8_t* rgba, int size);
    bool fontReady() const { return fontOk_; }
    // Короткая строка для нижней панели: бэкенд, размер, число вызовов отрисовки.
    std::string statsLine() const;

    void setScreenSize(int width, int height) {
        width_ = width;
        height_ = height;
    }

    const std::string& lastError() const { return error_; }
    int drawCalls() const { return drawCalls_; }
    int visibleChunks() const { return visibleChunks_; }
    static constexpr int kFontAtlasSize = 512;
    // Фиксированные буферы кадра: создаются один раз, без пересоздания в кадре.
    static constexpr size_t kMaxUiVertices = 32768;
    static constexpr size_t kMaxDynamicVertices = 32768;

private:
    struct ChunkGpu {
        sg_buffer opaque{};
        sg_buffer water{};
        int opaqueVertices = 0;
        int waterVertices = 0;
        bool valid = false;
        AABB bounds;
    };

    bool initShaders();
    void initFontPlaceholder();
    bool initPipelines();
    void uploadChunk(int chunkIndex, const ChunkMesh& mesh);
    void buildDynamicGeometry(const FrameData& frame);
    void drawWorld(const FrameData& frame);
    Mat4 viewProjection(const FrameData& frame) const;
    std::string glslHeader() const;
    void setError(const std::string& text);

    int width_ = 1280;
    int height_ = 720;
    int drawCalls_ = 0;
    int visibleChunks_ = 0;
    int remeshedThisFrame_ = 0;
    std::string error_;

    std::vector<ChunkGpu> chunks_;
    ChunkMesh scratchMesh_;
    std::vector<Vertex3D> dynamicVertices_;
    std::vector<UiVertex> uiNdc_;

    sg_shader worldShader_{};
    sg_shader waterShader_{};
    sg_shader uiShader_{};
    sg_shader skyShader_{};
    sg_pipeline worldPipeline_{};
    sg_pipeline waterPipeline_{};
    sg_pipeline uiPipeline_{};
    sg_pipeline skyPipeline_{};

    sg_buffer skyBuffer_{};
    sg_buffer uiBuffer_{};
    size_t uiCapacity_ = 0;
    size_t skyCapacity_ = 0;
    sg_buffer dynamicBuffer_{};
    size_t dynamicCapacity_ = 0;
    int dynamicOpaqueVertices_ = 0;
    int dynamicAlphaVertices_ = 0;

    sg_image fontImage_{};
    sg_view fontView_{};
    sg_sampler fontSampler_{};
    // 1x1 белая текстура: страховка, чтобы интерфейс рисовался даже без атласа.
    sg_image whiteImage_{};
    sg_view whiteView_{};
    sg_view uiView() const { return fontView_.id != 0 ? fontView_ : whiteView_; }
    bool fontOk_ = false;
    bool ensureWhiteTexture();
    const char* backendName_ = "?";
};

}  // namespace razlom::client
