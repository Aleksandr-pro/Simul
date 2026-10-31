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
    out vec2 v_pos;
    out vec4 v_color;
    void main() {
        v_pos = position;
        v_color = color;
        gl_Position = vec4(position, 1.0, 1.0);
    }
    )";

    // Небо рисуется первым кадром всегда: даже если мир по какой-то причине
    // не дал ни одного чанка, игрок видит небо с солнцем, а не пустую заливку.
    inline constexpr const char* kSkyFsGlsl = R"(
    uniform vec4 horizon;  // x — NDC-высота горизонта, y — мягкость, z/w — резерв
    uniform vec4 sun;      // xy — центр диска в NDC, z — радиус, w — видимость 0..1

    in vec2 v_pos;
    in vec4 v_color;
    out vec4 frag_color;

    void main() {
        const vec3 zenith = vec3(0.20, 0.47, 0.85);
        const vec3 midSky = vec3(0.45, 0.71, 0.95);
        vec3 horizonColor = vec3(0.81, 0.90, 0.99) * mix(vec3(1.0), v_color.rgb, 0.15);

        float soft = max(horizon.y, 0.05);
        float h = v_pos.y - horizon.x;
        float up = clamp(h / soft, 0.0, 1.0);
        float down = clamp(-h / (soft * 0.75), 0.0, 1.0);
        vec3 col = mix(horizonColor, midSky, up);
        col = mix(col, zenith, clamp((up - 0.30) / 0.70, 0.0, 1.0));
        col = mix(col, horizonColor * 0.90, down * 0.65);

        if (sun.w > 0.001) {
            float d = length(v_pos - sun.xy);
            float radius = max(sun.z, 0.01);
            float disc = 1.0 - smoothstep(radius * 0.55, radius, d);
            float glow = exp(-d * 2.4) * 0.32;
            col += vec3(1.0, 0.95, 0.78) * (disc * 0.85 + glow) * sun.w;
        }
        frag_color = vec4(col, 1.0);
    }
    )";

    // Запасное небо: если драйвер не собрал градиент (редкие мобильные GL),
    // рисуем заливку цветом вершин — экран не остаётся пустым в любом случае.
    inline constexpr const char* kSkyFlatFsGlsl = R"(
    in vec2 v_pos;
    in vec4 v_color;
    out vec4 frag_color;
    void main() {
        // Лёгкий вертикальный градиент из тех же цветов, что у вершин.
        float t = clamp(v_pos.y * 0.25 + 0.5, 0.0, 1.0);
        frag_color = vec4(v_color.rgb * (0.86 + 0.14 * t), 1.0);
        if (dot(v_color.rgb, vec3(1.0)) < 0.01) {
            frag_color = vec4(0.45, 0.71, 0.95, 1.0);  // страховка от чёрного неба
        }
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
        float2 uv : TEXCOORD0;
        float4 color : COLOR0;
    };
    vs_out main(vs_in inp) {
        vs_out o;
        o.pos = float4(inp.position, 1.0, 1.0);
        o.uv = inp.position;
        o.color = inp.color;
        return o;
    }
    )";

    inline constexpr const char* kSkyFsHlsl = R"(
    cbuffer fs_params : register(b0) {
        float4 horizon;
        float4 sun;
    };
    struct fs_in {
        float4 pos : SV_Position;
        float2 uv : TEXCOORD0;
        float4 color : COLOR0;
    };
    float4 main(fs_in inp) : SV_Target {
        const float3 zenith = float3(0.20, 0.47, 0.85);
        const float3 midSky = float3(0.45, 0.71, 0.95);
        float3 horizonColor = float3(0.81, 0.90, 0.99) * lerp(float3(1.0, 1.0, 1.0), inp.color.rgb, 0.15);
        float soft = max(horizon.y, 0.05);
        float h = inp.uv.y - horizon.x;
        float up = saturate(h / soft);
        float down = saturate(-h / (soft * 0.75));
        float3 col = lerp(horizonColor, midSky, up);
        col = lerp(col, zenith, saturate((up - 0.30) / 0.70));
        col = lerp(col, horizonColor * 0.90, down * 0.65);
        if (sun.w > 0.001) {
            float d = length(inp.uv - sun.xy);
            float radius = max(sun.z, 0.01);
            float disc = 1.0 - smoothstep(radius * 0.55, radius, d);
            float glow = exp(-d * 2.4) * 0.32;
            col += float3(1.0, 0.95, 0.78) * (disc * 0.85 + glow) * sun.w;
        }
        return float4(col, 1.0);
    }
    )";

    inline constexpr const char* kSkyFlatFsHlsl = R"(
    struct fs_in {
        float4 pos : SV_Position;
        float2 uv : TEXCOORD0;
        float4 color : COLOR0;
    };
    float4 main(fs_in inp) : SV_Target {
        float t = saturate(inp.uv.y * 0.25 + 0.5);
        float3 col = inp.color.rgb * (0.86 + 0.14 * t);
        if (dot(inp.color.rgb, float3(1.0, 1.0, 1.0)) < 0.01) col = float3(0.45, 0.71, 0.95);
        return float4(col, 1.0);
    }
    )";

}  // namespace razlom::client::shaders
