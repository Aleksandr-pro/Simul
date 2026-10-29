#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace simul::ui {

struct BakedGlyph {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float xoff = 0.0f;
    float yoff = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float xadvance = 0.0f;
};

/**
 * Декодер UTF-8 и генератор High-DPI RGBA8 GPU-атласа шрифта (2048x2048, 56px) с поддержкой:
 * - ASCII (U+0020..U+007E),
 * - полной кириллицы (U+0400..U+045F, включая Ё/ё),
 * - типографики и валют (—, •, «, », …, ₽, $, ★, ✓, →).
 * Обеспечивает идеально чёткий текст на 3x/4x OLED-экранах смартфонов и Retina/4K мониторах ПК.
 */
class Utf8FontAtlas {
public:
    static constexpr int kAtlasWidth = 2048;
    static constexpr int kAtlasHeight = 2048;

    bool build_from_embedded_ttf(float base_pixel_height = 56.0f);

    const std::vector<std::uint8_t>& rgba_pixels() const noexcept { return rgba_pixels_; }
    float base_pixel_height() const noexcept { return base_pixel_height_; }
    float ascent_px() const noexcept { return ascent_px_; }
    float line_height_px() const noexcept { return line_height_px_; }

    /** Координаты белого текселя для заливки карточек, линий и частиц */
    float white_u() const noexcept { return 2.0f / static_cast<float>(kAtlasWidth); }
    float white_v() const noexcept { return 2.0f / static_cast<float>(kAtlasHeight); }

    const BakedGlyph* find_glyph(std::uint32_t codepoint) const noexcept;

    float measure_text_width(std::string_view utf8_text, float font_size_px) const noexcept;

    static std::uint32_t next_utf8_codepoint(std::string_view text, std::size_t& cursor) noexcept;

private:
    float base_pixel_height_ = 56.0f;
    float ascent_px_ = 44.0f;
    float line_height_px_ = 64.0f;
    std::vector<std::uint8_t> rgba_pixels_;
    std::unordered_map<std::uint32_t, BakedGlyph> glyphs_;
};

} // namespace simul::ui
