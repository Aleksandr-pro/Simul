// РАЗЛОМ — бинарный протокол клиент/сервер (TCP, length-prefixed).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "razlom/core/math.hpp"
#include "razlom/core/world.hpp"

namespace razlom::net {

constexpr uint32_t kProtocolVersion = 1;
constexpr uint16_t kDefaultPort = 47800;
constexpr size_t kMaxPacketSize = 1 << 20;  // 1 МБ
constexpr size_t kMaxNameLength = 36;

enum class MsgType : uint8_t {
    Invalid = 0,
    Hello = 1,
    Welcome = 2,
    Input = 3,
    Edit = 4,
    Explode = 5,
    Chat = 6,
    SetMode = 7,
    Respawn = 8,
    Attack = 9,
    Ping = 10,
    Pong = 11,
    Changes = 20,
    Players = 21,
    Debris = 22,
    Event = 23,
    Tick = 24,
    Mode = 25,
    WorldSnapshot = 26,
};

enum PlayerFlags : uint8_t {
    kFlagFlying = 1 << 0,
    kFlagOnGround = 1 << 1,
    kFlagSprint = 1 << 2,
    kFlagDead = 1 << 3,
};

struct PlayerSnapshot {
    uint32_t id = 0;
    std::string name;
    Vec3 pos;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int16_t health = 100;
    uint8_t flags = 0;
    uint8_t slot = 0;
};

struct DebrisSnapshot {
    uint32_t id = 0;
    Vec3 pos;
    uint8_t block = 0;
};

class Writer {
public:
    explicit Writer(std::vector<uint8_t>& out) : out_(out) {}

    void begin(MsgType type) {
        out_.clear();
        u32(0);  // заглушка длины
        u8(static_cast<uint8_t>(type));
    }
    void end() {
        const uint32_t size = static_cast<uint32_t>(out_.size() - 4);
        std::memcpy(out_.data(), &size, sizeof(size));
    }
    void u8(uint8_t v) { out_.push_back(v); }
    void u16(uint16_t v) {
        out_.push_back(static_cast<uint8_t>(v & 0xff));
        out_.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void vec3(const Vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void str(const std::string& s) {
        const uint16_t n = static_cast<uint16_t>(s.size() > 255 ? 255 : s.size());
        u16(n);
        out_.insert(out_.end(), s.begin(), s.begin() + n);
    }
    void bytes(const uint8_t* data, size_t size) { out_.insert(out_.end(), data, data + size); }

private:
    std::vector<uint8_t>& out_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool ok() const { return ok_; }
    size_t remaining() const { return ok_ ? size_ - pos_ : 0; }

    uint8_t u8() {
        if (pos_ + 1 > size_) return fail();
        return data_[pos_++];
    }
    uint16_t u16() {
        if (pos_ + 2 > size_) return fail();
        const uint16_t v = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        if (pos_ + 4 > size_) return fail();
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 4;
        return v;
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32() {
        const uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    Vec3 vec3() {
        const float x = f32();
        const float y = f32();
        const float z = f32();
        return {x, y, z};
    }
    std::string str() {
        const uint16_t n = u16();
        if (!ok_ || pos_ + n > size_) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    uint8_t fail() {
        ok_ = false;
        pos_ = size_;
        return 0;
    }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// Готовые сообщения (сборка на отправляющей стороне).
inline void writeHello(std::vector<uint8_t>& buf, const std::string& name) {
    Writer w(buf);
    w.begin(MsgType::Hello);
    w.u16(static_cast<uint16_t>(kProtocolVersion));
    w.str(name);
    w.end();
}

inline void writeWelcome(std::vector<uint8_t>& buf, uint8_t playerId, uint32_t seed, uint8_t mode,
                         const std::vector<PlayerSnapshot>& players, const std::vector<BlockChange>& history) {
    Writer w(buf);
    w.begin(MsgType::Welcome);
    w.u8(playerId);
    w.u32(seed);
    w.u8(mode);
    w.u16(static_cast<uint16_t>(players.size()));
    for (const PlayerSnapshot& p : players) {
        w.u32(p.id);
        w.str(p.name);
        w.vec3(p.pos);
        w.f32(p.yaw);
        w.f32(p.pitch);
        w.i16(p.health);
        w.u8(p.flags);
        w.u8(p.slot);
    }
    w.u32(static_cast<uint32_t>(history.size()));
    for (const BlockChange& c : history) {
        w.i16(c.x);
        w.i16(c.y);
        w.i16(c.z);
        w.u8(c.block);
    }
    w.end();
}

}  // namespace razlom::net
