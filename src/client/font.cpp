#include "razlom/client/font.hpp"

#include <algorithm>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "embedded_golos_font.hpp"

namespace razlom::client {
namespace {

constexpr uint32_t kCodepoints[] = {
    // ASCII
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87,
    88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
    113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
    // Кириллица
    0x0401, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C,
    0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
    0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D,
    0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B,
    0x044C, 0x044D, 0x044E, 0x044F, 0x0451,
    // Символы
    0x2014, 0x2022, 0x00AB, 0x00BB, 0x2116, 0x00D7,
};

}  // namespace

std::vector<uint32_t> FontAtlas::decodeUtf8(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = '?';
        int extra = 0;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            ++i;
            continue;
        }
        ++i;
        for (int k = 0; k < extra && i < text.size(); ++k, ++i) {
            if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
                cp = '?';
                break;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        }
        out.push_back(cp);
    }
    return out;
}

bool FontAtlas::build(float pixelHeight) {
    pixelHeight_ = pixelHeight;
    pixels_.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
    glyphs_.clear();

    // белый тексель в левом верхнем углу — для прямоугольников и линий
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            uint8_t* px = &pixels_[(static_cast<size_t>(y) * kSize + x) * 4];
            px[0] = px[1] = px[2] = px[3] = 255;
        }
    }

    const auto* data = reinterpret_cast<const unsigned char*>(simul::ui::kEmbeddedGolosFontData);
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) return false;

    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    ascent_ = static_cast<float>(ascent) * scale;
    lineHeight_ = static_cast<float>(ascent - descent + lineGap) * scale;
    const int lineHeightPx = static_cast<int>(lineHeight_ + 0.5f) + 2;

    int penX = 2;
    int penY = 2;
    for (uint32_t cp : kCodepoints) {
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &advance, &lsb);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
        const int w = x1 - x0;
        const int h = y1 - y0;

        Glyph g;
        g.advance = static_cast<float>(advance) * scale;
        g.xoff = static_cast<float>(x0);
        g.yoff = static_cast<float>(y0);
        g.width = static_cast<float>(w);
        g.height = static_cast<float>(h);

        if (w > 0 && h > 0) {
            std::vector<uint8_t> bitmap(static_cast<size_t>(w) * h);
            stbtt_MakeCodepointBitmap(&info, bitmap.data(), w, h, w, scale, scale, static_cast<int>(cp));

            if (penX + w + 2 > kSize) {
                penX = 2;
                penY += lineHeightPx;
            }
            if (penY + h + 2 > kSize) return false;  // атлас переполнен

            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const uint8_t a = bitmap[static_cast<size_t>(y) * w + x];
                    if (a == 0) continue;
                    uint8_t* px = &pixels_[(static_cast<size_t>(penY + y) * kSize + (penX + x)) * 4];
                    px[0] = px[1] = px[2] = 255;
                    px[3] = a;
                }
            }
            g.u0 = static_cast<float>(penX) / kSize;
            g.v0 = static_cast<float>(penY) / kSize;
            g.u1 = static_cast<float>(penX + w) / kSize;
            g.v1 = static_cast<float>(penY + h) / kSize;
            penX += w + 2;
        }
        glyphs_[cp] = g;
    }

    ready_ = true;
    return true;
}

const Glyph* FontAtlas::glyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end()) return &it->second;
    it = glyphs_.find('?');
    if (it != glyphs_.end()) return &it->second;
    return nullptr;
}

float FontAtlas::measure(const std::string& utf8) const {
    float w = 0.0f;
    for (uint32_t cp : decodeUtf8(utf8)) {
        const Glyph* g = glyph(cp);
        if (g) w += g->advance;
    }
    return w;
}

}  // namespace razlom::client
