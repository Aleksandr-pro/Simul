// РАЗЛОМ — проверки исходников шейдеров. Работают без GPU и без sokol: просто
// смотрят на текст шейдеров и ловят ошибки, которые на CPU-превью не видны.
//
// Главная причина появления этих тестов — «мир не рисуется, а HUD рисуется».
// У GL-бэкенда sokol-gfx нет UBO: он берёт каждый член uniform-блока через
// glGetUniformLocation(prog, "<имя>") и заливает glUniformXXX(). Для блока
// `layout(std140) uniform vs_params { mat4 mvp; };` этот вызов возвращает -1,
// значение молча не применяется, mvp остаётся нулевой и вся геометрия
// схлопывается в точку. Интерфейс и небо uniform'ов не имеют — поэтому
// рисовались нормально, и баг было видно только на устройстве.
#include <string>

#include "razlom/client/shaders.hpp"
#include "test_util.hpp"

namespace {

bool has(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int countOf(const std::string& text, const char* needle) {
    int n = 0;
    const std::string s(needle);
    for (size_t pos = text.find(s); pos != std::string::npos; pos = text.find(s, pos + s.size())) ++n;
    return n;
}

// В GLSL у нас не должно оставаться именованных uniform-блоков.
void checkNoUniformBlock(const char* label, const char* src) {
    const std::string text(src);
    if (has(text, "uniform vs_params")) {
        std::printf("  %s: именованный uniform-блок в GLSL (sokol GL его не заливает)\n", label);
    }
    CHECK(!has(text, "uniform vs_params"));
    CHECK(!has(text, "layout(std140) uniform"));
}

// Члены блока в sg_shader_desc (mvp/fog/wave) должны существовать в GLSL как
// обычные uniform'ы с ровно теми же именами, иначе gl_loc == -1.
void checkWorldUniforms(const char* label, const char* src) {
    const std::string text(src);
    checkNoUniformBlock(label, src);
    CHECK(has(text, "uniform mat4 mvp;"));
    CHECK(has(text, "uniform vec4 fog;"));
    CHECK(has(text, "uniform vec4 wave;"));
    CHECK_EQ(countOf(text, "uniform mat4 mvp;"), 1);
    CHECK(has(text, "mvp * vec4("));
    CHECK(has(text, "void main"));
}

}  // namespace

using namespace razlom::client::shaders;

TEST(shader_glsl_world_uses_plain_uniforms) {
    checkWorldUniforms("world vs", kWorldVsGlsl);
    checkWorldUniforms("water vs", kWaterVsGlsl);
}

TEST(shader_glsl_ui_and_sky_have_no_blocks) {
    checkNoUniformBlock("ui vs", kUiVsGlsl);
    checkNoUniformBlock("ui fs", kUiFsGlsl);
    checkNoUniformBlock("sky vs", kSkyVsGlsl);
    checkNoUniformBlock("sky fs", kSkyFsGlsl);
}

// Атрибуты в шейдере обязаны совпадать с glsl_name из sg_shader_desc:
// position/color у мира и воды, position/uv/color у интерфейса.
TEST(shader_glsl_attributes_match_desc) {
    const std::string worldVs(kWorldVsGlsl);
    CHECK(has(worldVs, "in vec3 position;"));
    CHECK(has(worldVs, "in vec4 color;"));

    const std::string waterVs(kWaterVsGlsl);
    CHECK(has(waterVs, "in vec3 position;"));
    CHECK(has(waterVs, "in vec4 color;"));

    const std::string uiVs(kUiVsGlsl);
    CHECK(has(uiVs, "in vec2 position;"));
    CHECK(has(uiVs, "in vec2 uv;"));
    CHECK(has(uiVs, "in vec4 color;"));
    CHECK(has(std::string(kUiFsGlsl), "uniform sampler2D u_tex;"));

    const std::string skyVs(kSkyVsGlsl);
    CHECK(has(skyVs, "in vec2 position;"));
    CHECK(has(skyVs, "in vec4 color;"));
}

// Небо: горизонт и солнце — обычные vec4-юниформы во фрагментном шейдере.
// Имена обязаны в точности совпадать с sg_shader_desc в renderer.cpp.
TEST(shader_sky_uses_plain_uniforms) {
    const std::string fs(kSkyFsGlsl);
    checkNoUniformBlock("sky fs", kSkyFsGlsl);
    CHECK(has(fs, "uniform vec4 horizon;"));
    CHECK(has(fs, "uniform vec4 sun;"));
    CHECK_EQ(countOf(fs, "uniform vec4 horizon;"), 1);
    CHECK_EQ(countOf(fs, "uniform vec4 sun;"), 1);
    // Шейдер обязан рисовать градиент и солнце, а не одну заливку цветом вершины
    CHECK(has(fs, "mix("));
    CHECK(has(fs, "smoothstep("));
    CHECK(has(fs, "v_pos"));
    // Вершинный шейдер передаёт позицию как varying, иначе градиента не будет
    CHECK(has(std::string(kSkyVsGlsl), "out vec2 v_pos;"));
    CHECK(has(std::string(kSkyVsGlsl), "v_pos = position;"));
}

// У HLSL (D3D11) всё наоборот: там настоящий cbuffer, и он должен остаться.
// Имена из sg_shader_desc обязаны встречаться в GLSL как объявления uniform'ов:
// иначе sokol не найдёт location и молча оставит значение нулевым.
TEST(shader_uniform_names_match_shader_text) {
    CHECK_EQ(kWorldUniformCount, 3);
    const char* shadersWithWorldParams[] = {kWorldVsGlsl, kWaterVsGlsl};
    for (const char* src : shadersWithWorldParams) {
        const std::string text(src);
        for (int i = 0; i < kWorldUniformCount; ++i) {
            const std::string decl = std::string("uniform ") + (i == 0 ? "mat4 " : "vec4 ") +
                                     kWorldUniformNames[i] + ";";
            if (!has(text, decl.c_str())) std::printf("  нет объявления: %s\n", decl.c_str());
            CHECK(has(text, decl.c_str()));
        }
    }
    const std::string names = std::string(kWorldUniformNames[0]) + kWorldUniformNames[1] + kWorldUniformNames[2];
    CHECK_EQ(names, std::string("mvpfogwave"));
}

TEST(shader_hlsl_world_keeps_cbuffer) {
    const std::string worldVs(kWorldVsHlsl);
    CHECK(has(worldVs, "cbuffer vs_params : register(b0)"));
    CHECK(has(worldVs, "float4x4 mvp;"));
    CHECK(has(std::string(kWaterVsHlsl), "cbuffer vs_params : register(b0)"));
    CHECK(has(std::string(kWorldFsHlsl), "SV_Target"));
    // Небо в HLSL: те же два float4 в cbuffer фрагментного шейдера
    const std::string skyFs(kSkyFsHlsl);
    CHECK(has(skyFs, "cbuffer fs_params : register(b0)"));
    CHECK(has(skyFs, "float4 horizon;"));
    CHECK(has(skyFs, "float4 sun;"));
    CHECK(has(std::string(kSkyVsHlsl), "float2 uv : TEXCOORD0;"));
    CHECK(has(std::string(kUiFsHlsl), "SV_Target"));
}

// Ни один шейдер не должен остаться пустым или без точки входа.
TEST(shader_all_have_entry_point) {
    const char* all[][2] = {
        {"world vs glsl", kWorldVsGlsl}, {"world fs glsl", kWorldFsGlsl},
        {"water vs glsl", kWaterVsGlsl}, {"water fs glsl", kWaterFsGlsl},
        {"ui vs glsl", kUiVsGlsl},       {"ui fs glsl", kUiFsGlsl},
        {"sky vs glsl", kSkyVsGlsl},     {"sky fs glsl", kSkyFsGlsl},
        {"world vs hlsl", kWorldVsHlsl}, {"world fs hlsl", kWorldFsHlsl},
        {"water vs hlsl", kWaterVsHlsl}, {"water fs hlsl", kWaterFsHlsl},
        {"ui vs hlsl", kUiVsHlsl},       {"ui fs hlsl", kUiFsHlsl},
        {"sky vs hlsl", kSkyVsHlsl},     {"sky fs hlsl", kSkyFsHlsl},
    };
    for (const auto& s : all) {
        const std::string text(s[1]);
        CHECK(text.size() > 40);
        const bool glsl = has(std::string(s[0]), "glsl");
        CHECK(has(text, glsl ? "void main" : "main("));
        // Баланс скобок: дешёвая проверка, что строку не порвали при правках.
        int depth = 0;
        bool ok = true;
        for (char c : text) {
            if (c == '{') ++depth;
            if (c == '}') --depth;
            if (depth < 0) ok = false;
        }
        CHECK(ok);
        CHECK_EQ(depth, 0);
    }
}

int main() { return test::runAll(); }
