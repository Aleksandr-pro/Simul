// РАЗЛОМ — атлас шрифта (GolosText, кириллица + латиница) для 2D-интерфейса.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace razlom::client {

struct Glyph {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float width = 0.0f, height = 0.0f;
    float xoff = 0.0f, yoff = 0.0f;
    float advance = 0.0f;
};

class FontAtlas {
public:
    static constexpr int kSize = 512;
    static constexpr float kWhiteU = 2.0f / static_cast<float>(kSize);
    static constexpr float kWhiteV = 2.0f / static_cast<float>(kSize);

    bool build(float pixelHeight = 30.0f);
    bool ready() const { return ready_; }

    const Glyph* glyph(uint32_t codepoint) const;
    float measure(const std::string& utf8) const;
    float lineHeight() const { return lineHeight_; }
    float ascent() const { return ascent_; }
    float pixelHeight() const { return pixelHeight_; }

    const uint8_t* pixels() const { return pixels_.data(); }
    int width() const { return kSize; }
    int height() const { return kSize; }

    static std::vector<uint32_t> decodeUtf8(const std::string& text);

private:
    bool blitGlyph(int& penX, int& penY, int lineHeightPx, int w, int h, const uint8_t* bitmap, int atlasWidth);

    std::vector<uint8_t> pixels_;  // RGBA8
    std::unordered_map<uint32_t, Glyph> glyphs_;
    float pixelHeight_ = 30.0f;
    float lineHeight_ = 34.0f;
    float ascent_ = 24.0f;
    bool ready_ = false;
};

}  // namespace razlom::client
