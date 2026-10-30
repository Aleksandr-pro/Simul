#include "razlom/client/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "razlom/core/blocks.hpp"
#include "sokol_app.h"
#include "sokol_glue.h"

namespace razlom::client {
namespace {

struct WorldParams {
    float mvp[16];
    float fog[4];
    float wave[4];
};

constexpr uint8_t kFaceShade3D[6] = {205, 190, 255, 130, 225, 175};

constexpr int kCubeFaces[6][4][3] = {
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
    {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
};

void pushCube(std::vector<Vertex3D>& out, const Vec3& min, const Vec3& max, uint8_t r, uint8_t g, uint8_t b,
              uint8_t a, float sizeScale = 1.0f) {
    const Vec3 center = (min + max) * 0.5f;
    const Vec3 half = (max - min) * 0.5f * sizeScale;
    const Vec3 lo = center - half;
    const Vec3 hi = center + half;
    for (int face = 0; face < 6; ++face) {
        const int shade = kFaceShade3D[face];
        const uint8_t col[4] = {static_cast<uint8_t>(r * shade / 255), static_cast<uint8_t>(g * shade / 255),
                                static_cast<uint8_t>(b * shade / 255), a};
        Vertex3D quad[4];
        for (int i = 0; i < 4; ++i) {
            const int* c = kCubeFaces[face][i];
            quad[i].x = c[0] ? hi.x : lo.x;
            quad[i].y = c[1] ? hi.y : lo.y;
            quad[i].z = c[2] ? hi.z : lo.z;
            quad[i].r = col[0];
            quad[i].g = col[1];
            quad[i].b = col[2];
            quad[i].a = col[3];
        }
        for (int i : {0, 1, 2, 0, 2, 3}) out.push_back(quad[i]);
    }
}

const char* kWorldVsBody = R"(
layout(std140) uniform vs_params {
    mat4 mvp;
    vec4 fog;
    vec4 wave;
};
in vec3 position;
in vec4 color;
out vec4 v_color;
out float v_fog;
out vec3 v_fogColor;
void main() {
    vec4 clip = mvp * vec4(position, 1.0);
    gl_Position = clip;
    v_color = color;
    v_fog = clamp(1.0 - exp(-max(clip.w, 0.0) * fog.a), 0.0, 1.0);
    v_fogColor = fog.rgb;
}
)";

const char* kWorldFsBody = R"(
in vec4 v_color;
in float v_fog;
in vec3 v_fogColor;
out vec4 frag_color;
void main() {
    frag_color = vec4(mix(v_color.rgb, v_fogColor, v_fog), v_color.a);
}
)";

const char* kWaterVsBody = R"(
layout(std140) uniform vs_params {
    mat4 mvp;
    vec4 fog;
    vec4 wave;
};
in vec3 position;
in vec4 color;
out vec4 v_color;
out float v_fog;
out vec3 v_fogColor;
void main() {
    vec3 p = position;
    p.y += sin(wave.x * 2.1 + p.x * 0.9 + p.z * 0.7) * wave.y;
    vec4 clip = mvp * vec4(p, 1.0);
    gl_Position = clip;
    v_color = color;
    v_fog = clamp(1.0 - exp(-max(clip.w, 0.0) * fog.a), 0.0, 1.0);
    v_fogColor = fog.rgb;
}
)";

const char* kWaterFsBody = R"(
in vec4 v_color;
in float v_fog;
in vec3 v_fogColor;
out vec4 frag_color;
void main() {
    frag_color = vec4(mix(v_color.rgb, v_fogColor, v_fog), 0.72 * v_color.a);
}
)";

// Позиции интерфейса приходят уже в NDC (пересчёт на CPU): у шейдера нет
// ни uniform-блока, ни делений — меньше мест, где что-то может отвалиться.
const char* kUiVsBody = R"(
in vec2 position;
in vec2 uv;
in vec4 color;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    v_uv = uv;
    v_color = color;
}
)";

const char* kUiFsBody = R"(
uniform sampler2D u_tex;
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color * texture(u_tex, v_uv);
}
)";

const char* kSkyVsBody = R"(
in vec2 position;
in vec4 color;
out vec4 v_color;
void main() {
    gl_Position = vec4(position, 1.0, 1.0);
    v_color = color;
}
)";

const char* kSkyFsBody = R"(
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

// ---------------------------------------------------------------- HLSL (D3D11)

const char* kWorldVsHlsl = R"(
cbuffer vs_params : register(b0) {
    float4x4 mvp;
    float4 fog;
    float4 wave;
};
struct vs_in {
    float3 position : POSITION;
    float4 color : COLOR0;
};
struct vs_out {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float fog : TEXCOORD0;
    float3 fogColor : TEXCOORD1;
};
vs_out main(vs_in inp) {
    vs_out o;
    float4 clip = mul(mvp, float4(inp.position, 1.0));
    o.pos = clip;
    o.color = inp.color;
    o.fog = saturate(1.0 - exp(-max(clip.w, 0.0) * fog.a));
    o.fogColor = fog.rgb;
    return o;
}
)";

const char* kWorldFsHlsl = R"(
struct fs_in {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float fog : TEXCOORD0;
    float3 fogColor : TEXCOORD1;
};
float4 main(fs_in inp) : SV_Target {
    return float4(lerp(inp.color.rgb, inp.fogColor, inp.fog), inp.color.a);
}
)";

const char* kWaterVsHlsl = R"(
cbuffer vs_params : register(b0) {
    float4x4 mvp;
    float4 fog;
    float4 wave;
};
struct vs_in {
    float3 position : POSITION;
    float4 color : COLOR0;
};
struct vs_out {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float fog : TEXCOORD0;
    float3 fogColor : TEXCOORD1;
};
vs_out main(vs_in inp) {
    vs_out o;
    float3 p = inp.position;
    p.y += sin(wave.x * 2.1 + p.x * 0.9 + p.z * 0.7) * wave.y;
    float4 clip = mul(mvp, float4(p, 1.0));
    o.pos = clip;
    o.color = inp.color;
    o.fog = saturate(1.0 - exp(-max(clip.w, 0.0) * fog.a));
    o.fogColor = fog.rgb;
    return o;
}
)";

const char* kWaterFsHlsl = R"(
struct fs_in {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float fog : TEXCOORD0;
    float3 fogColor : TEXCOORD1;
};
float4 main(fs_in inp) : SV_Target {
    return float4(lerp(inp.color.rgb, inp.fogColor, inp.fog), 0.72 * inp.color.a);
}
)";

const char* kUiVsHlsl = R"(
struct vs_in {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct vs_out {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
vs_out main(vs_in inp) {
    vs_out o;
    o.pos = float4(inp.position, 0.0, 1.0);
    o.uv = inp.uv;
    o.color = inp.color;
    return o;
}
)";

const char* kUiFsHlsl = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
struct fs_in {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
float4 main(fs_in inp) : SV_Target {
    return tex.Sample(smp, inp.uv) * inp.color;
}
)";

const char* kSkyVsHlsl = R"(
struct vs_in {
    float2 position : POSITION;
    float4 color : COLOR0;
};
struct vs_out {
    float4 pos : SV_Position;
    float4 color : COLOR0;
};
vs_out main(vs_in inp) {
    vs_out o;
    o.pos = float4(inp.position, 1.0, 1.0);
    o.color = inp.color;
    return o;
}
)";

const char* kSkyFsHlsl = R"(
struct fs_in {
    float4 pos : SV_Position;
    float4 color : COLOR0;
};
float4 main(fs_in inp) : SV_Target {
    return inp.color;
}
)";

sg_blend_state alphaBlend() {
    sg_blend_state b{};
    b.enabled = true;
    b.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    b.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.op_rgb = SG_BLENDOP_ADD;
    b.src_factor_alpha = SG_BLENDFACTOR_ONE;
    b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.op_alpha = SG_BLENDOP_ADD;
    return b;
}

AABB chunkBounds(int chunkIndex) {
    int x = 0, y = 0, z = 0;
    World::chunkOrigin(chunkIndex, x, y, z);
    return AABB{Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                Vec3{static_cast<float>(x + kChunkSize), static_cast<float>(y + kChunkSize),
                     static_cast<float>(z + kChunkSize)}};
}

}  // namespace

bool Renderer::init() {
    chunks_.assign(World::kChunkCount, ChunkGpu{});
    for (int i = 0; i < World::kChunkCount; ++i) chunks_[static_cast<size_t>(i)].bounds = chunkBounds(i);
    dynamicVertices_.reserve(16384);

    if (!initShaders()) return false;
    if (!initPipelines()) return false;

    // небо: полноэкранный треугольник с градиентом
    {
        struct SkyVertex {
            float x, y;
            uint8_t r, g, b, a;
        };
        const SkyVertex sky[3] = {
            {-1.0f, -1.0f, 66, 120, 176, 255},
            {3.0f, -1.0f, 66, 120, 176, 255},
            {-1.0f, 3.0f, 132, 196, 240, 255},
        };
        sg_buffer_desc desc{};
        desc.usage.vertex_buffer = true;
        desc.size = sizeof(sky);
        desc.data = sg_range{sky, sizeof(sky)};
        desc.label = "razlom-sky";
        skyBuffer_ = sg_make_buffer(desc);
        skyCapacity_ = sizeof(sky);
    }

    // Буферы интерфейса и динамической геометрии создаются один раз:
    // пересоздание ресурсов в кадре — лишний риск на мобильных драйверах.
    {
        sg_buffer_desc desc{};
        desc.usage.vertex_buffer = true;
        desc.usage.write_transient = true;
        desc.size = kMaxUiVertices * sizeof(UiVertex);
        desc.label = "razlom-ui";
        uiBuffer_ = sg_make_buffer(desc);
        uiCapacity_ = desc.size;

        sg_buffer_desc ddesc{};
        ddesc.usage.vertex_buffer = true;
        ddesc.usage.write_transient = true;
        ddesc.size = kMaxDynamicVertices * sizeof(Vertex3D);
        ddesc.label = "razlom-dynamic";
        dynamicBuffer_ = sg_make_buffer(ddesc);
        dynamicCapacity_ = ddesc.size;
    }

    ensureWhiteTexture();
    switch (sg_query_backend()) {
        case SG_BACKEND_GLCORE: backendName_ = "OpenGL"; break;
        case SG_BACKEND_GLES3: backendName_ = "GLES3"; break;
        case SG_BACKEND_D3D11: backendName_ = "D3D11"; break;
        case SG_BACKEND_METAL_IOS:
        case SG_BACKEND_METAL_MACOS:
        case SG_BACKEND_METAL_SIMULATOR: backendName_ = "Metal"; break;
        default: backendName_ = "GPU"; break;
    }
    return true;
}

bool Renderer::ensureWhiteTexture() {
    if (whiteView_.id != 0) return true;
    const uint8_t white[4] = {255, 255, 255, 255};
    sg_image_desc desc{};
    desc.width = 1;
    desc.height = 1;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.num_mipmaps = 1;
    desc.data = sg_range{white, sizeof(white)};
    desc.label = "razlom-1x1-white";
    whiteImage_ = sg_make_image(desc);
    if (whiteImage_.id == 0) return false;
    sg_view_desc vd{};
    vd.texture.image = whiteImage_;
    vd.label = "razlom-1x1-white-view";
    whiteView_ = sg_make_view(vd);
    return whiteView_.id != 0;
}

std::string Renderer::statsLine() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s · %dx%d · %d выз. · %d чанков", backendName_, width_, height_, drawCalls_,
                  visibleChunks_);
    return std::string(buf);
}

std::string Renderer::glslHeader() const {
    if (sg_query_backend() == SG_BACKEND_GLES3) return "#version 300 es\nprecision highp float;\n";
    return "#version 330 core\n";
}

void Renderer::setError(const std::string& text) { error_ = text; }

bool Renderer::initShaders() {
    const bool hlsl = sg_query_backend() == SG_BACKEND_D3D11;
    const std::string header = glslHeader();
    auto glsl = [&](const char* body) { return header + body; };

    auto makeShader = [](const char* vs, const char* fs, const char* label, bool worldUniforms, bool uiUniforms,
                         bool textured) {
        sg_shader_desc desc{};
        desc.vertex_func.source = vs;
        desc.fragment_func.source = fs;
        desc.vertex_func.d3d11_target = "vs_4_0";
        desc.fragment_func.d3d11_target = "ps_4_0";
        desc.label = label;

        int attr = 0;
        if (worldUniforms || (!uiUniforms && !textured)) {
            desc.attrs[attr].glsl_name = "position";
            desc.attrs[attr].hlsl_sem_name = "POSITION";
            desc.attrs[attr].hlsl_sem_index = 0;
            ++attr;
            desc.attrs[attr].glsl_name = "color";
            desc.attrs[attr].hlsl_sem_name = "COLOR";
            desc.attrs[attr].hlsl_sem_index = 0;
            ++attr;
        } else if (uiUniforms) {
            desc.attrs[attr].glsl_name = "position";
            desc.attrs[attr].hlsl_sem_name = "POSITION";
            desc.attrs[attr].hlsl_sem_index = 0;
            ++attr;
            desc.attrs[attr].glsl_name = "uv";
            desc.attrs[attr].hlsl_sem_name = "TEXCOORD";
            desc.attrs[attr].hlsl_sem_index = 0;
            ++attr;
            desc.attrs[attr].glsl_name = "color";
            desc.attrs[attr].hlsl_sem_name = "COLOR";
            desc.attrs[attr].hlsl_sem_index = 0;
            ++attr;
        }

        if (worldUniforms) {
            sg_shader_uniform_block& ub = desc.uniform_blocks[0];
            ub.stage = SG_SHADERSTAGE_VERTEX;
            ub.size = sizeof(WorldParams);
            ub.layout = SG_UNIFORMLAYOUT_STD140;
            ub.hlsl_register_b_n = 0;
            ub.glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
            ub.glsl_uniforms[0].array_count = 0;
            ub.glsl_uniforms[0].glsl_name = "mvp";
            ub.glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
            ub.glsl_uniforms[1].array_count = 0;
            ub.glsl_uniforms[1].glsl_name = "fog";
            ub.glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
            ub.glsl_uniforms[2].array_count = 0;
            ub.glsl_uniforms[2].glsl_name = "wave";
        }

        if (textured) {
            desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
            desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
            desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
            desc.views[0].texture.hlsl_register_t_n = 0;
            desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
            desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
            desc.samplers[0].hlsl_register_s_n = 0;
            desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
            desc.texture_sampler_pairs[0].view_slot = 0;
            desc.texture_sampler_pairs[0].sampler_slot = 0;
            desc.texture_sampler_pairs[0].glsl_name = "u_tex";
        }
        return desc;
    };

    const std::string worldVs = hlsl ? std::string(kWorldVsHlsl) : glsl(kWorldVsBody);
    const std::string worldFs = hlsl ? std::string(kWorldFsHlsl) : glsl(kWorldFsBody);
    const std::string waterVs = hlsl ? std::string(kWaterVsHlsl) : glsl(kWaterVsBody);
    const std::string waterFs = hlsl ? std::string(kWaterFsHlsl) : glsl(kWaterFsBody);
    const std::string uiVs = hlsl ? std::string(kUiVsHlsl) : glsl(kUiVsBody);
    const std::string uiFs = hlsl ? std::string(kUiFsHlsl) : glsl(kUiFsBody);
    const std::string skyVs = hlsl ? std::string(kSkyVsHlsl) : glsl(kSkyVsBody);
    const std::string skyFs = hlsl ? std::string(kSkyFsHlsl) : glsl(kSkyFsBody);

    sg_shader_desc worldDesc = makeShader(worldVs.c_str(), worldFs.c_str(), "razlom-world", true, false, false);
    worldShader_ = sg_make_shader(worldDesc);
    sg_shader_desc waterDesc = makeShader(waterVs.c_str(), waterFs.c_str(), "razlom-water", true, false, false);
    waterShader_ = sg_make_shader(waterDesc);
    sg_shader_desc uiDesc = makeShader(uiVs.c_str(), uiFs.c_str(), "razlom-ui", false, true, true);
    uiShader_ = sg_make_shader(uiDesc);
    sg_shader_desc skyDesc = makeShader(skyVs.c_str(), skyFs.c_str(), "razlom-sky", false, false, false);
    skyShader_ = sg_make_shader(skyDesc);

    if (sg_query_shader_state(worldShader_) != SG_RESOURCESTATE_VALID ||
        sg_query_shader_state(uiShader_) != SG_RESOURCESTATE_VALID) {
        setError("Не удалось скомпилировать шейдеры");
        return false;
    }
    return true;
}

bool Renderer::initPipelines() {
    sg_pipeline_desc desc{};
    desc.shader = worldShader_;
    desc.layout.buffers[0].stride = sizeof(Vertex3D);
    desc.layout.attrs[0].buffer_index = 0;
    desc.layout.attrs[0].offset = 0;
    desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[1].buffer_index = 0;
    desc.layout.attrs[1].offset = 12;
    desc.layout.attrs[1].format = SG_VERTEXFORMAT_UBYTE4N;
    desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    desc.cull_mode = SG_CULLMODE_BACK;
    desc.face_winding = SG_FACEWINDING_CCW;
    desc.depth.pixel_format = static_cast<sg_pixel_format>(sapp_depth_format());
    desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    desc.depth.write_enabled = true;
    desc.colors[0].pixel_format = static_cast<sg_pixel_format>(sapp_color_format());
    desc.color_count = 1;
    desc.sample_count = sapp_sample_count();
    worldPipeline_ = sg_make_pipeline(desc);

    desc.shader = waterShader_;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.depth.write_enabled = false;
    desc.colors[0].blend = alphaBlend();
    waterPipeline_ = sg_make_pipeline(desc);

    desc = sg_pipeline_desc{};
    desc.shader = uiShader_;
    desc.layout.buffers[0].stride = sizeof(UiVertex);
    desc.layout.attrs[0].buffer_index = 0;
    desc.layout.attrs[0].offset = 0;
    desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[1].buffer_index = 0;
    desc.layout.attrs[1].offset = 8;
    desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[2].buffer_index = 0;
    desc.layout.attrs[2].offset = 16;
    desc.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.depth.pixel_format = static_cast<sg_pixel_format>(sapp_depth_format());
    desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    desc.depth.write_enabled = false;
    desc.colors[0].pixel_format = static_cast<sg_pixel_format>(sapp_color_format());
    desc.colors[0].blend = alphaBlend();
    desc.color_count = 1;
    desc.sample_count = sapp_sample_count();
    uiPipeline_ = sg_make_pipeline(desc);

    desc = sg_pipeline_desc{};
    desc.shader = skyShader_;
    desc.layout.buffers[0].stride = 12;
    desc.layout.attrs[0].buffer_index = 0;
    desc.layout.attrs[0].offset = 0;
    desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[1].buffer_index = 0;
    desc.layout.attrs[1].offset = 8;
    desc.layout.attrs[1].format = SG_VERTEXFORMAT_UBYTE4N;
    desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.depth.pixel_format = static_cast<sg_pixel_format>(sapp_depth_format());
    desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    desc.depth.write_enabled = false;
    desc.colors[0].pixel_format = static_cast<sg_pixel_format>(sapp_color_format());
    desc.color_count = 1;
    desc.sample_count = sapp_sample_count();
    skyPipeline_ = sg_make_pipeline(desc);

    if (sg_query_pipeline_state(worldPipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(uiPipeline_) != SG_RESOURCESTATE_VALID) {
        setError("Не удалось создать графические конвейеры");
        return false;
    }
    return true;
}

bool Renderer::initFontTexture(const uint8_t* rgba, int size) {
    if (!rgba || size <= 0) return false;
    if (fontView_.id != 0) sg_destroy_view(fontView_);
    if (fontImage_.id != 0) sg_destroy_image(fontImage_);
    if (whiteView_.id != 0) sg_destroy_view(whiteView_);
    if (whiteImage_.id != 0) sg_destroy_image(whiteImage_);
    fontView_ = {};
    fontImage_ = {};
    fontOk_ = false;

    if (fontSampler_.id == 0) {
        sg_sampler_desc sd{};
        sd.min_filter = SG_FILTER_LINEAR;
        sd.mag_filter = SG_FILTER_LINEAR;
        sd.mipmap_filter = SG_FILTER_NEAREST;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
        sd.label = "razlom-font-sampler";
        fontSampler_ = sg_make_sampler(sd);
    }

    sg_image_desc desc{};
    desc.width = size;
    desc.height = size;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.num_mipmaps = 1;
    desc.data = sg_range{rgba, static_cast<size_t>(size) * static_cast<size_t>(size) * 4};
    desc.label = "razlom-font";
    fontImage_ = sg_make_image(desc);
    if (fontImage_.id == 0) {
        setError("Не удалось загрузить атлас шрифта");
        return false;
    }

    sg_view_desc vd{};
    vd.texture.image = fontImage_;
    vd.label = "razlom-font-view";
    fontView_ = sg_make_view(vd);
    fontOk_ = fontView_.id != 0 && fontSampler_.id != 0;
    if (!fontOk_) setError("Не удалось создать представление атласа шрифта");
    return fontOk_;
}

void Renderer::shutdown() {
    for (ChunkGpu& c : chunks_) {
        if (c.valid) {
            if (c.opaque.id != 0) sg_destroy_buffer(c.opaque);
            if (c.water.id != 0) sg_destroy_buffer(c.water);
            c.valid = false;
        }
    }
    chunks_.clear();
    if (skyBuffer_.id != 0) sg_destroy_buffer(skyBuffer_);
    if (uiBuffer_.id != 0) sg_destroy_buffer(uiBuffer_);
    if (dynamicBuffer_.id != 0) sg_destroy_buffer(dynamicBuffer_);
    if (fontView_.id != 0) sg_destroy_view(fontView_);
    if (fontImage_.id != 0) sg_destroy_image(fontImage_);
    if (whiteView_.id != 0) sg_destroy_view(whiteView_);
    if (whiteImage_.id != 0) sg_destroy_image(whiteImage_);
    if (fontSampler_.id != 0) sg_destroy_sampler(fontSampler_);
    if (worldPipeline_.id != 0) sg_destroy_pipeline(worldPipeline_);
    if (waterPipeline_.id != 0) sg_destroy_pipeline(waterPipeline_);
    if (uiPipeline_.id != 0) sg_destroy_pipeline(uiPipeline_);
    if (skyPipeline_.id != 0) sg_destroy_pipeline(skyPipeline_);
    if (worldShader_.id != 0) sg_destroy_shader(worldShader_);
    if (waterShader_.id != 0) sg_destroy_shader(waterShader_);
    if (uiShader_.id != 0) sg_destroy_shader(uiShader_);
    if (skyShader_.id != 0) sg_destroy_shader(skyShader_);
    skyBuffer_ = {};
    uiBuffer_ = {};
    dynamicBuffer_ = {};
    fontImage_ = {};
    fontView_ = {};
    fontSampler_ = {};
    whiteImage_ = {};
    whiteView_ = {};
    worldPipeline_ = {};
    waterPipeline_ = {};
    uiPipeline_ = {};
    skyPipeline_ = {};
    worldShader_ = {};
    waterShader_ = {};
    uiShader_ = {};
    skyShader_ = {};
}

void Renderer::resetWorld(World& world) {
    for (ChunkGpu& c : chunks_) {
        if (c.valid) {
            if (c.opaque.id != 0) sg_destroy_buffer(c.opaque);
            if (c.water.id != 0) sg_destroy_buffer(c.water);
        }
        c = ChunkGpu{};
    }
    world.markAllChunksDirty();
}

void Renderer::uploadChunk(int chunkIndex, const ChunkMesh& mesh) {
    ChunkGpu& c = chunks_[static_cast<size_t>(chunkIndex)];
    if (c.valid) {
        if (c.opaque.id != 0) sg_destroy_buffer(c.opaque);
        if (c.water.id != 0) sg_destroy_buffer(c.water);
        c.opaque = {};
        c.water = {};
        c.valid = false;
    }

    if (!mesh.opaque.empty()) {
        sg_buffer_desc desc{};
        desc.usage.vertex_buffer = true;
        desc.size = mesh.opaque.size() * sizeof(Vertex3D);
        desc.data = sg_range{mesh.opaque.data(), desc.size};
        desc.label = "razlom-chunk";
        c.opaque = sg_make_buffer(desc);
        c.opaqueVertices = static_cast<int>(mesh.opaque.size());
    } else {
        c.opaqueVertices = 0;
    }

    if (!mesh.water.empty()) {
        sg_buffer_desc desc{};
        desc.usage.vertex_buffer = true;
        desc.size = mesh.water.size() * sizeof(Vertex3D);
        desc.data = sg_range{mesh.water.data(), desc.size};
        desc.label = "razlom-water";
        c.water = sg_make_buffer(desc);
        c.waterVertices = static_cast<int>(mesh.water.size());
    } else {
        c.waterVertices = 0;
    }
    c.valid = true;
}

int Renderer::buildChunks(const World& world, int maxChunks) {
    if (maxChunks <= 0) return 0;
    const std::vector<int> dirty = const_cast<World&>(world).takeDirtyChunks(maxChunks);
    for (int chunkIndex : dirty) {
        buildChunkMesh(world, chunkIndex, scratchMesh_);
        uploadChunk(chunkIndex, scratchMesh_);
    }
    return static_cast<int>(dirty.size());
}

bool Renderer::chunksReady(const World& world) const { return world.dirtyChunkCount() == 0; }

int Renderer::pendingChunks(const World& world) const { return static_cast<int>(world.dirtyChunkCount()); }

void Renderer::buildDynamicGeometry(const FrameData& frame) {
    dynamicVertices_.clear();
    dynamicOpaqueVertices_ = 0;
    dynamicVertices_.reserve(4096);

    if (frame.debris) {
        for (const net::DebrisSnapshot& d : *frame.debris) {
            const BlockDef& def = blockDef(d.block);
            const Vec3 min{d.pos.x - 0.45f, d.pos.y - 0.45f, d.pos.z - 0.45f};
            const Vec3 max{d.pos.x + 0.45f, d.pos.y + 0.45f, d.pos.z + 0.45f};
            pushCube(dynamicVertices_, min, max, def.r, def.g, def.b, 255);
        }
    }

    if (frame.players) {
        for (const net::PlayerSnapshot& p : *frame.players) {
            if (p.id == frame.selfPlayerId) continue;
            const bool dead = (p.flags & net::kFlagDead) != 0;
            const uint8_t alpha = dead ? 90 : 255;
            const Vec3 feet{p.pos.x - 0.32f, p.pos.y, p.pos.z - 0.32f};
            const Vec3 bodyTop{p.pos.x + 0.32f, p.pos.y + 1.35f, p.pos.z + 0.32f};
            pushCube(dynamicVertices_, feet, bodyTop, 72, 160, 220, alpha);

            const Vec3 headMin{p.pos.x - 0.26f, p.pos.y + 1.35f, p.pos.z - 0.26f};
            const Vec3 headMax{p.pos.x + 0.26f, p.pos.y + 1.82f, p.pos.z + 0.26f};
            pushCube(dynamicVertices_, headMin, headMax, 236, 196, 140, alpha);
        }
    }

    dynamicOpaqueVertices_ = static_cast<int>(dynamicVertices_.size());

    if (frame.hasHighlight) {
        const Vec3 min{static_cast<float>(frame.highlight.x) - 0.01f,
                       static_cast<float>(frame.highlight.y) - 0.01f,
                       static_cast<float>(frame.highlight.z) - 0.01f};
        const Vec3 max{static_cast<float>(frame.highlight.x + 1) + 0.01f,
                       static_cast<float>(frame.highlight.y + 1) + 0.01f,
                       static_cast<float>(frame.highlight.z + 1) + 0.01f};
        pushCube(dynamicVertices_, min, max, 255, 255, 255, 70, 1.02f);
    }

    // не выходим за фиксированную ёмкость буфера
    if (dynamicVertices_.size() > kMaxDynamicVertices) dynamicVertices_.resize(kMaxDynamicVertices);
    if (dynamicOpaqueVertices_ > static_cast<int>(dynamicVertices_.size())) {
        dynamicOpaqueVertices_ = static_cast<int>(dynamicVertices_.size());
    }
}

Mat4 Renderer::viewProjection(const FrameData& frame) const {
    const Camera& cam = frame.camera;
    const Mat4 view = Mat4::lookAt(cam.position, cam.position + cam.forward(), Vec3{0.0f, 1.0f, 0.0f});
    const float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
    const Mat4 proj = Mat4::perspective(radians(cam.fovDegrees), aspect, 0.08f, 260.0f);
    return proj * view;
}

void Renderer::render(const FrameData& frame) {
    drawCalls_ = 0;
    visibleChunks_ = 0;
    remeshedThisFrame_ = 0;

    if (frame.world) remeshedThisFrame_ = buildChunks(*frame.world, frame.maxRemesh);
    buildDynamicGeometry(frame);

    const std::vector<UiVertex> emptyUi;
    const std::vector<UiVertex>& ui = frame.ui ? *frame.ui : emptyUi;
    const size_t dynamicBytes = std::min(dynamicVertices_.size(), kMaxDynamicVertices) * sizeof(Vertex3D);

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.16f, 0.22f, 0.30f, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    // --- 1. небо ---
    if (frame.showSky && skyBuffer_.id != 0 && skyPipeline_.id != 0) {
        sg_apply_pipeline(skyPipeline_);
        sg_bindings bind{};
        bind.vertex_buffers[0] = skyBuffer_;
        sg_apply_bindings(&bind);
        sg_draw(0, 3, 1);
        ++drawCalls_;
    }

    const Mat4 vp = viewProjection(frame);

    // --- 2. чанки ---
    if (frame.world) {
        WorldParams params{};
        for (int i = 0; i < 16; ++i) params.mvp[i] = vp.m[i];
        params.fog[0] = 0.52f;
        params.fog[1] = 0.72f;
        params.fog[2] = 0.86f;
        params.fog[3] = 0.0125f;
        params.wave[0] = frame.time;
        params.wave[1] = 0.035f;

        sg_apply_pipeline(worldPipeline_);
        sg_apply_uniforms(0, sg_range{&params, sizeof(params)});

        const Frustum frustum = Frustum::fromViewProjection(vp);
        sg_bindings bind{};

        for (int i = 0; i < World::kChunkCount; ++i) {
            const ChunkGpu& c = chunks_[static_cast<size_t>(i)];
            if (!c.valid || c.opaqueVertices == 0) continue;
            if (!frustum.intersects(c.bounds)) continue;
            ++visibleChunks_;
            bind.vertex_buffers[0] = c.opaque;
            sg_apply_bindings(&bind);
            sg_draw(0, c.opaqueVertices, 1);
            ++drawCalls_;
        }

        // --- 3. вода ---
        sg_apply_pipeline(waterPipeline_);
        sg_apply_uniforms(0, sg_range{&params, sizeof(params)});
        for (int i = 0; i < World::kChunkCount; ++i) {
            const ChunkGpu& c = chunks_[static_cast<size_t>(i)];
            if (!c.valid || c.waterVertices == 0) continue;
            if (!frustum.intersects(c.bounds)) continue;
            bind.vertex_buffers[0] = c.water;
            sg_apply_bindings(&bind);
            sg_draw(0, c.waterVertices, 1);
            ++drawCalls_;
        }
    }

    // --- 4. обломки и игроки ---
    if (dynamicOpaqueVertices_ > 0 && dynamicBuffer_.id != 0 && dynamicBytes > 0) {
        // transient-буфер по документации sokol пишется внутри пасса, до привязки
        sg_write_buffer_desc wd{};
        wd.src.data = sg_range{dynamicVertices_.data(), dynamicBytes};
        wd.dst.buffer = dynamicBuffer_;
        wd.dst.offset = 0;
        sg_write_buffer_transient(&wd);
        WorldParams params{};
        for (int i = 0; i < 16; ++i) params.mvp[i] = vp.m[i];
        params.fog[0] = 0.52f;
        params.fog[1] = 0.72f;
        params.fog[2] = 0.86f;
        params.fog[3] = 0.0125f;
        sg_apply_pipeline(worldPipeline_);
        sg_apply_uniforms(0, sg_range{&params, sizeof(params)});
        sg_bindings bind{};
        bind.vertex_buffers[0] = dynamicBuffer_;
        sg_apply_bindings(&bind);
        sg_draw(0, dynamicOpaqueVertices_, 1);
        ++drawCalls_;
    }

    // --- 5. подсветка выбранного блока (полупрозрачная) ---
    if (frame.hasHighlight && dynamicBuffer_.id != 0 &&
        static_cast<int>(dynamicVertices_.size()) > dynamicOpaqueVertices_) {
        WorldParams params{};
        for (int i = 0; i < 16; ++i) params.mvp[i] = vp.m[i];
        params.fog[3] = 0.0f;
        sg_apply_pipeline(waterPipeline_);
        sg_apply_uniforms(0, sg_range{&params, sizeof(params)});
        sg_bindings bind{};
        bind.vertex_buffers[0] = dynamicBuffer_;
        sg_apply_bindings(&bind);
        sg_draw(dynamicOpaqueVertices_, static_cast<int>(dynamicVertices_.size()) - dynamicOpaqueVertices_, 1);
        ++drawCalls_;
    }

    // --- 6. интерфейс ---
    if (!ui.empty() && uiBuffer_.id != 0 && uiView().id != 0) {
        // пиксели -> NDC на CPU, не больше ёмкости буфера
        const size_t uiCount = std::min(ui.size(), kMaxUiVertices);
        uiNdc_.resize(uiCount);
        const float sx = 2.0f / static_cast<float>(width_ > 0 ? width_ : 1);
        const float sy = 2.0f / static_cast<float>(height_ > 0 ? height_ : 1);
        for (size_t i = 0; i < uiCount; ++i) {
            UiVertex v = ui[i];
            v.x = v.x * sx - 1.0f;
            v.y = 1.0f - v.y * sy;
            uiNdc_[i] = v;
        }

        sg_write_buffer_desc wd{};
        wd.src.data = sg_range{uiNdc_.data(), uiNdc_.size() * sizeof(UiVertex)};
        wd.dst.buffer = uiBuffer_;
        wd.dst.offset = 0;
        sg_write_buffer_transient(&wd);

        sg_apply_pipeline(uiPipeline_);
        sg_bindings bind{};
        bind.vertex_buffers[0] = uiBuffer_;
        bind.views[0] = uiView();
        bind.samplers[0] = fontSampler_;
        sg_apply_bindings(&bind);
        sg_draw(0, static_cast<int>(uiNdc_.size()), 1);
        ++drawCalls_;
    }

    sg_end_pass();
    sg_commit();
}

}  // namespace razlom::client
