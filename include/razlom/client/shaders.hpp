#pragma once

// Исходники шейдеров клиента: GLSL ES 3.0 (телефон/Android, WebGL2) и HLSL (D3D11).
//
// ВАЖНО про uniform-блоки. На GL/GLES бэкенде sokol-gfx НЕ использует UBO:
// он достаёт каждый член блока через glGetUniformLocation(prog, "<имя>") и
// заливает его через glUniformXXX() (см. sokol_gfx.h, раздел "UNIFORM DATA LAYOUT").
// Поэтому в GLSL переменные обязаны быть объявлены как ОБЫЧНЫЕ uniform'ы:
//
//     uniform mat4 mvp;   // правильно
//     layout(std140) uniform vs_params { mat4 mvp; };  // неправильно: location == -1
//
// Для именованного блока glGetUniformLocation("mvp") возвращает -1, значение
// молча не применяется, матрица остаётся нулевой — и весь мир схлопывается в
// точку (небо и интерфейс при этом рисуются, т.к. uniform'ов не имеют).
// Описание блока в sg_shader_desc (имена, типы, порядок) при этом всё равно
// заполняется — sokol использует его как таблицу для заливки.
//
// HLSL-версии, наоборот, объявляют cbuffer (D3D11 работает с настоящими UBO).
// Проверки на это лежат в tests/test_ui.cpp (shader sanity).

namespace razlom::client::shaders {

// Имена членов uniform-блока в порядке WorldParams {mvp, fog, wave}. Ровно эти
// имена уходят в sg_shader_desc.uniform_blocks[0].glsl_uniforms[i].glsl_name,
// и по ним sokol ищет uniform в шейдере. Опечатка = gl_loc == -1 = молчаливая
// потеря значения, поэтому tests/test_shaders.cpp сверяет их с текстом GLSL.
inline constexpr const char* kWorldUniformNames[] = {"mvp", "fog", "wave"};
inline constexpr int kWorldUniformCount = 3;

    inline constexpr const char* kWorldVsGlsl = R"(
    // Обычные (не блочные) uniform'ы — требование GL-бэкенда sokol, см. шапку файла.
    uniform mat4 mvp;
    uniform vec4 fog;
    uniform vec4 wave;
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

    inline constexpr const char* kWorldFsGlsl = R"(
    in vec4 v_color;
    in float v_fog;
    in vec3 v_fogColor;
    out vec4 frag_color;
    void main() {
        frag_color = vec4(mix(v_color.rgb, v_fogColor, v_fog), v_color.a);
    }
    )";

    inline constexpr const char* kWaterVsGlsl = R"(
    // Обычные (не блочные) uniform'ы — требование GL-бэкенда sokol, см. шапку файла.
    uniform mat4 mvp;
    uniform vec4 fog;
    uniform vec4 wave;
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

    inline constexpr const char* kWaterFsGlsl = R"(
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
    inline constexpr const char* kUiVsGlsl = R"(
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

    inline constexpr const char* kUiFsGlsl = R"(
    uniform sampler2D u_tex;
    in vec2 v_uv;
    in vec4 v_color;
    out vec4 frag_color;
    void main() {
        frag_color = v_color * texture(u_tex, v_uv);
    }
    )";

    inline constexpr const char* kSkyVsGlsl = R"(
    in vec2 position;
    in vec4 color;
    out vec4 v_color;
    void main() {
        gl_Position = vec4(position, 1.0, 1.0);
        v_color = color;
    }
    )";

    inline constexpr const char* kSkyFsGlsl = R"(
    in vec4 v_color;
    out vec4 frag_color;
    void main() {
        frag_color = v_color;
    }
    )";

    // ---------------------------------------------------------------- HLSL (D3D11)

    inline constexpr const char* kWorldVsHlsl = R"(
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

    inline constexpr const char* kWorldFsHlsl = R"(
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

    inline constexpr const char* kWaterVsHlsl = R"(
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

    inline constexpr const char* kWaterFsHlsl = R"(
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

    inline constexpr const char* kUiVsHlsl = R"(
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

    inline constexpr const char* kUiFsHlsl = R"(
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

    inline constexpr const char* kSkyVsHlsl = R"(
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

    inline constexpr const char* kSkyFsHlsl = R"(
    struct fs_in {
        float4 pos : SV_Position;
        float4 color : COLOR0;
    };
    float4 main(fs_in inp) : SV_Target {
        return inp.color;
    }
    )";

}  // namespace razlom::client::shaders
