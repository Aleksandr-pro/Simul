// РАЗЛОМ — детерминированный целочисленный шум.
// Одинаковый на x86/ARM/Android: генерация мира на клиенте и сервере даёт один и тот же мир.
#pragma once

#include <cstdint>

namespace razlom {

inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = seed ^ 0x9e3779b9u;
    h = hashU32(h ^ static_cast<uint32_t>(x) * 0x85ebca6bu);
    h = hashU32(h ^ static_cast<uint32_t>(y) * 0xc2b2ae35u);
    return h;
}

inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed ^ 0x27d4eb2fu;
    h = hashU32(h ^ static_cast<uint32_t>(x) * 0x9e3779b1u);
    h = hashU32(h ^ static_cast<uint32_t>(y) * 0x85ebca6bu);
    h = hashU32(h ^ static_cast<uint32_t>(z) * 0xc2b2ae35u);
    return h;
}

// Значение 0..1023 по целым координатам.
inline int value2(int x, int z, uint32_t seed) {
    return static_cast<int>(hash2(x, z, seed) & 1023u);
}

inline int value3(int x, int y, int z, uint32_t seed) {
    return static_cast<int>(hash3(x, y, z, seed) & 1023u);
}

inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// Бикубический (smoothstep) билинейный шум, масштаб — размер ячейки в блоках.
inline int noise2(int x, int z, int cell, uint32_t seed) {
    if (cell < 1) cell = 1;
    const int gx = floorDiv(x, cell);
    const int gz = floorDiv(z, cell);
    const int fx = ((x % cell) + cell) % cell;
    const int fz = ((z % cell) + cell) % cell;
    // фиксированная точка 0..1024
    const int tx = fx * 1024 / cell;
    const int tz = fz * 1024 / cell;
    const int sx = tx * tx * (3 * 1024 - 2 * tx) / (1024 * 1024);
    const int sz = tz * tz * (3 * 1024 - 2 * tz) / (1024 * 1024);
    const int v00 = value2(gx, gz, seed);
    const int v10 = value2(gx + 1, gz, seed);
    const int v01 = value2(gx, gz + 1, seed);
    const int v11 = value2(gx + 1, gz + 1, seed);
    const int a = v00 + (v10 - v00) * sx / 1024;
    const int b = v01 + (v11 - v01) * sx / 1024;
    return a + (b - a) * sz / 1024;
}

inline int noise3(int x, int y, int z, int cell, uint32_t seed) {
    if (cell < 1) cell = 1;
    const int gx = floorDiv(x, cell);
    const int gy = floorDiv(y, cell);
    const int gz = floorDiv(z, cell);
    auto frac = [cell](int v) {
        const int f = ((v % cell) + cell) % cell;
        const int t = f * 1024 / cell;
        return t * t * (3 * 1024 - 2 * t) / (1024 * 1024);
    };
    const int sx = frac(x), sy = frac(y), sz = frac(z);
    const int v000 = value3(gx, gy, gz, seed);
    const int v100 = value3(gx + 1, gy, gz, seed);
    const int v010 = value3(gx, gy + 1, gz, seed);
    const int v110 = value3(gx + 1, gy + 1, gz, seed);
    const int v001 = value3(gx, gy, gz + 1, seed);
    const int v101 = value3(gx + 1, gy, gz + 1, seed);
    const int v011 = value3(gx, gy + 1, gz + 1, seed);
    const int v111 = value3(gx + 1, gy + 1, gz + 1, seed);
    auto mix = [](int a, int b, int t) { return a + (b - a) * t / 1024; };
    const int x00 = mix(v000, v100, sx);
    const int x10 = mix(v010, v110, sx);
    const int x01 = mix(v001, v101, sx);
    const int x11 = mix(v011, v111, sx);
    const int y0 = mix(x00, x10, sy);
    const int y1 = mix(x01, x11, sy);
    return mix(y0, y1, sz);
}

// Быстрый ПСЧ для игровых эффектов (не влияет на генерацию мира).
class Rng {
public:
    explicit Rng(uint32_t seed = 0x12345678u) : state_(seed ? seed : 0x12345678u) {}

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    float unit() { return static_cast<float>(next() & 0xffffffu) / static_cast<float>(0x1000000u); }
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
    int rangeInt(int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }

private:
    uint32_t state_;
};

}  // namespace razlom
