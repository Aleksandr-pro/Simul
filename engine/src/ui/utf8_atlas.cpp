#include "simul/ui/utf8_atlas.hpp"
#include "embedded_golos_font.hpp"
#include "stb_truetype.h"
#include <algorithm>
#include <cmath>

namespace simul::ui {

std::uint32_t Utf8FontAtlas::next_utf8_codepoint(std::string_view text, std::size_t& cursor) noexcept {
    if (cursor >= text.size()) return 0;
    const auto b0 = static_cast<unsigned char>(text[cursor++]);
    if (b0 < 0x80u) return b0;

    if ((b0 & 0xE0u) == 0xC0u && cursor < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[cursor++]);
        return ((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
    }
    if ((b0 & 0xF0u) == 0xE0u && cursor + 1 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[cursor++]);
        const auto b2 = static_cast<unsigned char>(text[cursor++]);
        return ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
    }
    if ((b0 & 0xF8u) == 0xF0u && cursor + 2 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[cursor++]);
        const auto b2 = static_cast<unsigned char>(text[cursor++]);
        const auto b3 = static_cast<unsigned char>(text[cursor++]);
        return ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
    }
    return 0xFFFDu;
}

bool Utf8FontAtlas::build_from_embedded_ttf(float base_pixel_height) {
    base_pixel_height_ = base_pixel_height > 12.0f ? base_pixel_height : 56.0f;

    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, kEmbeddedGolosFontData,
                        stbtt_GetFontOffsetForIndex(kEmbeddedGolosFontData, 0))) {
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, base_pixel_height_);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    ascent_px_ = static_cast<float>(ascent) * scale;
    line_height_px_ = static_cast<float>(ascent - descent + line_gap) * scale;

    std::vector<std::uint8_t> alpha_sheet(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight), 0u);

    // Белый блок 6x6 в (0,0) для векторных карточек, линий и частиц (с запасом под фильтрацию)
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            alpha_sheet[static_cast<std::size_t>(y * kAtlasWidth + x)] = 255u;
        }
    }

    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(320);
    for (std::uint32_t cp = 0x20u; cp <= 0x7Eu; ++cp) codepoints.push_back(cp);
    for (std::uint32_t cp = 0x0400u; cp <= 0x045Fu; ++cp) codepoints.push_back(cp);
    for (std::uint32_t cp : {0x00ABu, 0x00BBu, 0x00B7u, 0x00D7u, 0x2013u, 0x2014u,
                             0x2022u, 0x2026u, 0x20BDu, 0x2191u, 0x2192u, 0x2193u,
                             0x25B6u, 0x25CFu, 0x2605u, 0x26A1u, 0x2713u, 0x2715u}) {
        codepoints.push_back(cp);
    }

    int pen_x = 12;
    int pen_y = 4;
    int row_h = 12;
    glyphs_.clear();

    for (std::uint32_t cp : codepoints) {
        int advance_w = 0, left_side_bearing = 0;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(cp), &advance_w, &left_side_bearing);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
        const int gw = std::max(0, x1 - x0);
        const int gh = std::max(0, y1 - y0);

        if (pen_x + gw + 4 >= kAtlasWidth) {
            pen_x = 4;
            pen_y += row_h + 4;
            row_h = 0;
        }
        if (pen_y + gh + 4 >= kAtlasHeight) {
            break;
        }

        if (gw > 0 && gh > 0) {
            stbtt_MakeCodepointBitmap(
                &font,
                alpha_sheet.data() + pen_y * kAtlasWidth + pen_x,
                gw,
                gh,
                kAtlasWidth,
                scale,
                scale,
                static_cast<int>(cp));
        }

        BakedGlyph g{};
        g.u0 = static_cast<float>(pen_x) / static_cast<float>(kAtlasWidth);
        g.v0 = static_cast<float>(pen_y) / static_cast<float>(kAtlasHeight);
        g.u1 = static_cast<float>(pen_x + gw) / static_cast<float>(kAtlasWidth);
        g.v1 = static_cast<float>(pen_y + gh) / static_cast<float>(kAtlasHeight);
        g.xoff = static_cast<float>(x0);
        g.yoff = static_cast<float>(y0);
        g.width = static_cast<float>(gw);
        g.height = static_cast<float>(gh);
        g.xadvance = static_cast<float>(advance_w) * scale;
        glyphs_[cp] = g;

        pen_x += gw + 4;
        if (gh > row_h) row_h = gh;
    }

    rgba_pixels_.resize(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight * 4));
    for (std::size_t i = 0; i < alpha_sheet.size(); ++i) {
        rgba_pixels_[i * 4 + 0] = 255u;
        rgba_pixels_[i * 4 + 1] = 255u;
        rgba_pixels_[i * 4 + 2] = 255u;
        rgba_pixels_[i * 4 + 3] = alpha_sheet[i];
    }
    return true;
}

const BakedGlyph* Utf8FontAtlas::find_glyph(std::uint32_t codepoint) const noexcept {
    if (const auto it = glyphs_.find(codepoint); it != glyphs_.end()) {
        return &it->second;
    }
    if (const auto fb = glyphs_.find(static_cast<std::uint32_t>('?')); fb != glyphs_.end()) {
        return &fb->second;
    }
    return nullptr;
}

float Utf8FontAtlas::measure_text_width(std::string_view utf8_text, float font_size_px) const noexcept {
    const float ratio = font_size_px / base_pixel_height_;
    float width = 0.0f;
    std::size_t pos = 0;
    while (pos < utf8_text.size()) {
        const std::uint32_t cp = next_utf8_codepoint(utf8_text, pos);
        if (cp == 0) break;
        if (const auto* g = find_glyph(cp)) {
            width += g->xadvance * ratio;
        }
    }
    return width;
}

} // namespace simul::ui
