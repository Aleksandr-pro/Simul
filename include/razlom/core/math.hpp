// РАЗЛОМ — минимальная математика движка: векторы, матрицы, лучи, фрустум.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace razlom {

constexpr float kPi = 3.14159265358979323846f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float radians(float deg) { return deg * (kPi / 180.0f); }
inline float degrees(float rad) { return rad * (180.0f / kPi); }
inline float signf(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSq() const { return x * x + y * y + z * z; }
    Vec3 normalized() const {
        const float len = length();
        return len > 1e-6f ? Vec3{x / len, y / len, z / len} : Vec3{};
    }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct IVec3 {
    int x = 0, y = 0, z = 0;

    constexpr IVec3() = default;
    constexpr IVec3(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    IVec3 operator+(const IVec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    IVec3 operator-(const IVec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    bool operator==(const IVec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const IVec3& o) const { return !(*this == o); }
    Vec3 toVec3() const { return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}; }
};

inline float distSq(const Vec3& a, const Vec3& b) { return (b - a).lengthSq(); }

// Матрица 4x4, column-major (как в OpenGL/D3D после транспонирования в шейдере не нуждается).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return Mat4{}; }

    static Mat4 perspective(float fovyRad, float aspect, float znear, float zfar) {
        Mat4 r;
        for (float& v : r.m) v = 0.0f;
        const float f = 1.0f / std::tan(fovyRad * 0.5f);
        r.m[0] = f / (aspect > 1e-6f ? aspect : 1.0f);
        r.m[5] = f;
        r.m[10] = (zfar + znear) / (znear - zfar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
        return r;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float znear, float zfar) {
        Mat4 r;
        for (float& v : r.m) v = 0.0f;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (zfar - znear);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(zfar + znear) / (zfar - znear);
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = (center - eye).normalized();
        const Vec3 s = cross(f, up).normalized();
        const Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;   r.m[12] = -dot(s, eye);
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;   r.m[13] = -dot(u, eye);
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = dot(f, eye);
        r.m[3] = 0.0f; r.m[7] = 0.0f; r.m[11] = 0.0f; r.m[15] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }

    Vec3 transformPoint(const Vec3& p) const {
        const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (std::fabs(w) > 1e-6f) return {x / w, y / w, z / w};
        return {x, y, z};
    }
};

struct AABB {
    Vec3 min;
    Vec3 max;

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extent() const { return (max - min) * 0.5f; }

    bool overlaps(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x &&
               min.y < o.max.y && max.y > o.min.y &&
               min.z < o.max.z && max.z > o.min.z;
    }
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
};

struct Ray {
    Vec3 origin;
    Vec3 dir;
};

struct Plane {
    // n * p + d = 0
    Vec3 normal;
    float d = 0.0f;
};

// Плоскости фрустума, извлечённые из view-projection (6 штук, нормали внутрь).
struct Frustum {
    Plane planes[6];

    static Frustum fromViewProjection(const Mat4& vp) {
        Frustum f;
        const float* m = vp.m;
        const float rows[4][4] = {
            {m[0], m[4], m[8], m[12]},    // row0
            {m[1], m[5], m[9], m[13]},    // row1
            {m[2], m[6], m[10], m[14]},   // row2
            {m[3], m[7], m[11], m[15]},   // row3
        };
        auto setPlane = [&](int idx, int a, int b, float s) {
            Plane p;
            p.normal = {rows[3][0] + s * rows[a][0], rows[3][1] + s * rows[a][1], rows[3][2] + s * rows[a][2]};
            p.d = rows[3][3] + s * rows[a][3];
            const float len = p.normal.length();
            if (len > 1e-6f) {
                p.normal = p.normal / len;
                p.d /= len;
            }
            (void)b;
            f.planes[idx] = p;
        };
        setPlane(0, 0, 0, 1.0f);   // left
        setPlane(1, 0, 0, -1.0f);  // right
        setPlane(2, 1, 1, 1.0f);   // bottom
        setPlane(3, 1, 1, -1.0f);  // top
        setPlane(4, 2, 2, 1.0f);   // near
        setPlane(5, 2, 2, -1.0f);  // far
        return f;
    }

    bool intersects(const AABB& box) const {
        for (const Plane& p : planes) {
            // положительная вершина AABB по направлению нормали
            const Vec3 positive{
                p.normal.x >= 0.0f ? box.max.x : box.min.x,
                p.normal.y >= 0.0f ? box.max.y : box.min.y,
                p.normal.z >= 0.0f ? box.max.z : box.min.z,
            };
            if (dot(p.normal, positive) + p.d < 0.0f) return false;
        }
        return true;
    }
};

// Пересечение луча с AABB (slab-метод). Возвращает t входа или -1.
inline float rayAabb(const Ray& ray, const AABB& box, float maxT) {
    float tmin = 0.0f;
    float tmax = maxT;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = axis == 0 ? ray.origin.x : (axis == 1 ? ray.origin.y : ray.origin.z);
        const float d = axis == 0 ? ray.dir.x : (axis == 1 ? ray.dir.y : ray.dir.z);
        const float lo = axis == 0 ? box.min.x : (axis == 1 ? box.min.y : box.min.z);
        const float hi = axis == 0 ? box.max.x : (axis == 1 ? box.max.y : box.max.z);
        if (std::fabs(d) < 1e-8f) {
            if (o < lo || o > hi) return -1.0f;
            continue;
        }
        float t1 = (lo - o) / d;
        float t2 = (hi - o) / d;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return -1.0f;
    }
    return tmin;
}

}  // namespace razlom
