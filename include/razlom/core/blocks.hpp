// РАЗЛОМ — реестр блоков: цвет, вес, прочность, горючесть.
// Один и тот же реестр используют клиент (рендер) и сервер (физика).
#pragma once

#include <cstdint>

namespace razlom {

enum BlockId : uint8_t {
    kAir = 0,
    kBedrock = 1,
    kStone = 2,
    kDirt = 3,
    kGrass = 4,
    kSand = 5,
    kGravel = 6,
    kLog = 7,
    kPlanks = 8,
    kLeaves = 9,
    kBrick = 10,
    kStoneBrick = 11,
    kGlass = 12,
    kTnt = 13,
    kAsh = 14,
    kWaterBase = 20,  // kWaterBase + 0..6 — уровни воды 1..7
    kWaterFull = kWaterBase + 6,
    kFire = 27,
    kBlockCount = 32,
};

struct BlockDef {
    const char* id;       // стабильный строковый id (для редактора режимов и сети)
    const char* name;     // подпись в интерфейсе (UTF-8)
    uint8_t r, g, b;      // базовый цвет
    uint8_t strength;     // несущая способность (условные единицы)
    uint8_t weight;       // вес блока
    uint8_t hardness;     // сопротивление взрыву/добыче
    bool solid;           // блокирует движение
    bool opaque;          // перекрывает грани соседей
    bool liquid;
    bool flammable;
    bool glow;
};

inline const BlockDef& blockDef(uint8_t id) {
    static const BlockDef kDefs[kBlockCount] = {
        /* 0  air       */ {"air", "Воздух", 0, 0, 0, 0, 0, 0, false, false, false, false, false},
        /* 1  bedrock   */ {"bedrock", "Бедрок", 58, 60, 70, 255, 0, 255, true, true, false, false, false},
        /* 2  stone     */ {"stone", "Камень", 132, 134, 146, 48, 3, 12, true, true, false, false, false},
        /* 3  dirt      */ {"dirt", "Земля", 121, 85, 58, 26, 2, 4, true, true, false, false, false},
        /* 4  grass     */ {"grass", "Трава", 104, 178, 76, 26, 2, 4, true, true, false, false, false},
        /* 5  sand      */ {"sand", "Песок", 233, 211, 146, 14, 2, 3, true, true, false, false, false},
        /* 6  gravel    */ {"gravel", "Гравий", 150, 145, 150, 18, 3, 4, true, true, false, false, false},
        /* 7  log       */ {"log", "Бревно", 108, 80, 48, 40, 2, 6, true, true, false, true, false},
        /* 8  planks    */ {"planks", "Доски", 186, 140, 86, 30, 1, 5, true, true, false, true, false},
        /* 9  leaves    */ {"leaves", "Листва", 58, 150, 74, 8, 1, 1, true, false, false, true, false},
        /* 10 brick     */ {"brick", "Кирпич", 176, 90, 72, 44, 3, 10, true, true, false, false, false},
        /* 11 stonebrk  */ {"stonebrick", "Кам. кирпич", 145, 145, 155, 52, 3, 14, true, true, false, false, false},
        /* 12 glass     */ {"glass", "Стекло", 190, 226, 240, 12, 1, 2, true, false, false, false, false},
        /* 13 tnt       */ {"tnt", "ТНТ", 214, 72, 58, 6, 1, 2, true, true, false, false, false},
        /* 14 ash       */ {"ash", "Пепел", 92, 86, 80, 6, 1, 1, true, true, false, false, false},
        {"void", "", 0, 0, 0, 0, 0, 0, false, false, false, false, false},
    };
    if (id < 15) return kDefs[id];
    if (id >= kWaterBase && id <= kWaterFull) {
        static const BlockDef kWater = {"water", "Вода", 52, 122, 208, 0, 0, 0, false, false, true, false, false};
        return kWater;
    }
    if (id == kFire) {
        static const BlockDef kFireDef = {"fire", "Огонь", 255, 150, 40, 0, 0, 0, false, false, false, false, true};
        return kFireDef;
    }
    static const BlockDef kUnknown = {"void", "?", 255, 0, 255, 0, 0, 0, false, false, false, false, false};
    return kUnknown;
}

inline uint8_t clampByte(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

inline bool isWater(uint8_t id) { return id >= kWaterBase && id <= kWaterFull; }
inline bool isFireBlock(uint8_t id) { return id == kFire; }
inline bool isSolidBlock(uint8_t id) { return blockDef(id).solid; }
inline bool isOpaqueBlock(uint8_t id) { return blockDef(id).opaque; }
inline int waterLevel(uint8_t id) { return isWater(id) ? (id - kWaterBase + 1) : 0; }

// Цвет блока с небольшим детерминированным разбросом по координатам (стилизация).
inline void blockFaceColor(uint8_t id, int x, int y, int z, uint8_t faceShade,
                           uint8_t ao, uint8_t out[4]) {
    const BlockDef& d = blockDef(id);
    int r = d.r, g = d.g, b = d.b;
    const uint32_t h = static_cast<uint32_t>(x * 73856093) ^ static_cast<uint32_t>(y * 19349663) ^
                       static_cast<uint32_t>(z * 83492791);
    const int jitter = static_cast<int>((h >> 7) & 7) - 3;
    r = clampByte(r + jitter);
    g = clampByte(g + jitter);
    b = clampByte(b + jitter);
    // Два множителя 0..255 (яркость грани и AO) — нормируем один раз на каждый.
    const int shadeNum = static_cast<int>(faceShade) * static_cast<int>(ao);
    const int base = r * shadeNum / (255 * 255);
    out[0] = clampByte(base);
    out[1] = clampByte(g * shadeNum / (255 * 255));
    out[2] = clampByte(b * shadeNum / (255 * 255));
    out[3] = 255;
}

}  // namespace razlom
