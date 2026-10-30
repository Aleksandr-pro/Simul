#include "razlom/net/client.hpp"

#include <algorithm>

namespace razlom::net {

Client::Client() = default;

Client::~Client() { disconnect(); }

void Client::setError(const std::string& text) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    error_ = text;
}

std::string Client::error() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return error_;
}

bool Client::connect(const std::string& host, uint16_t port, const std::string& name) {
    disconnect();
    if (!Socket::startup()) {
        setError("Сетевая подсистема недоступна");
        state_.store(ClientState::Failed);
        return false;
    }
    setError({});
    state_.store(ClientState::Connecting);
    running_.store(true);
    reader_ = std::thread([this, host, port, name] { connectLoop(host, port, name); });
    return true;
}

void Client::connectLoop(std::string host, uint16_t port, std::string name) {
    Socket sock;
    if (!sock.connectTo(host, port)) {
        setError("Не удалось подключиться к " + host + ":" + std::to_string(port));
        state_.store(ClientState::Failed);
        running_.store(false);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sock_ = std::move(sock);
    }
    std::vector<uint8_t> hello;
    writeHello(hello, name);
    if (!writePacket(hello)) {
        setError("Соединение разорвано при знакомстве");
        state_.store(ClientState::Failed);
        running_.store(false);
        return;
    }
    state_.store(ClientState::Handshaking);
    readLoop();
}

void Client::disconnect() {
    if (!running_.exchange(false)) {
        if (reader_.joinable()) reader_.join();
        state_.store(ClientState::Idle);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sock_.shutdownBoth();
        sock_.close();
    }
    if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id()) {
        reader_.join();
    } else if (reader_.joinable()) {
        reader_.detach();
    }
    state_.store(ClientState::Idle);
    pingNonce_ = 0;
    latency_.store(-1);
}

void Client::readLoop() {
    std::vector<uint8_t> body;
    while (running_.load()) {
        uint8_t header[4];
        size_t got = 0;
        while (got < 4) {
            const int n = sock_.recvSome(header + got, 4 - got);
            if (n <= 0) {
                if (running_.load()) {
                    setError("Соединение с хостом потеряно");
                    state_.store(ClientState::Failed);
                    std::lock_guard<std::mutex> lock(inboxMutex_);
                    inbox_.disconnected = true;
                    inbox_.disconnectReason = "Соединение с хостом потеряно";
                }
                running_.store(false);
                return;
            }
            got += static_cast<size_t>(n);
        }
        const uint32_t len = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) |
                             (static_cast<uint32_t>(header[2]) << 16) | (static_cast<uint32_t>(header[3]) << 24);
        if (len == 0 || len > kMaxPacketSize) {
            running_.store(false);
            state_.store(ClientState::Failed);
            return;
        }
        body.resize(len);
        size_t received = 0;
        while (received < len) {
            const int n = sock_.recvSome(body.data() + received, len - received);
            if (n <= 0) {
                running_.store(false);
                state_.store(ClientState::Failed);
                return;
            }
            received += static_cast<size_t>(n);
        }

        Reader r(body.data(), body.size());
        const MsgType type = static_cast<MsgType>(r.u8());
        if (!r.ok()) continue;
        handleMessage(type, r);
    }
}

void Client::handleMessage(MsgType type, Reader& r) {
    std::lock_guard<std::mutex> lock(inboxMutex_);
    switch (type) {
        case MsgType::Welcome: {
            inbox_.playerId = r.u8();
            inbox_.seed = r.u32();
            inbox_.mode = r.u8();
            const uint16_t count = r.u16();
            inbox_.players.clear();
            inbox_.players.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                PlayerSnapshot p;
                p.id = r.u32();
                p.name = r.str();
                p.pos = r.vec3();
                p.yaw = r.f32();
                p.pitch = r.f32();
                p.health = r.i16();
                p.flags = r.u8();
                p.slot = r.u8();
                inbox_.players.push_back(std::move(p));
            }
            const uint32_t changes = r.u32();
            inbox_.history.clear();
            if (changes < 400000u) inbox_.history.reserve(changes);
            for (uint32_t i = 0; i < changes && r.ok(); ++i) {
                BlockChange c;
                c.x = r.i16();
                c.y = r.i16();
                c.z = r.i16();
                c.block = r.u8();
                inbox_.history.push_back(c);
            }
            inbox_.welcome = r.ok();
            if (inbox_.welcome) state_.store(ClientState::Connected);
            break;
        }
        case MsgType::Changes: {
            const uint16_t count = r.u16();
            for (uint16_t i = 0; i < count && r.ok(); ++i) {
                BlockChange c;
                c.x = r.i16();
                c.y = r.i16();
                c.z = r.i16();
                c.block = r.u8();
                inbox_.changes.push_back(c);
            }
            break;
        }
        case MsgType::Players: {
            const uint8_t count = r.u8();
            std::vector<PlayerSnapshot> list;
            list.reserve(count);
            for (uint8_t i = 0; i < count && r.ok(); ++i) {
                PlayerSnapshot p;
                p.id = r.u32();
                p.name = r.str();
                p.pos = r.vec3();
                p.yaw = r.f32();
                p.pitch = r.f32();
                p.health = r.i16();
                p.flags = r.u8();
                p.slot = r.u8();
                list.push_back(std::move(p));
            }
            if (r.ok()) {
                inbox_.players = std::move(list);
                inbox_.playersUpdated = true;
            }
            break;
        }
        case MsgType::Debris: {
            const uint16_t count = r.u16();
            std::vector<DebrisSnapshot> list;
            list.reserve(count);
            for (uint16_t i = 0; i < count && r.ok(); ++i) {
                DebrisSnapshot d;
                d.id = r.u32();
                d.pos = r.vec3();
                d.block = r.u8();
                list.push_back(d);
            }
            if (r.ok()) inbox_.debris = std::move(list);
            break;
        }
        case MsgType::Event: {
            SessionEvent ev;
            ev.kind = static_cast<SessionEventKind>(r.u8());
            ev.pos = r.vec3();
            ev.text = r.str();
            if (r.ok()) inbox_.events.push_back(std::move(ev));
            break;
        }
        case MsgType::Tick: {
            inbox_.serverTick = r.u32();
            r.u32();
            break;
        }
        case MsgType::Pong: {
            const uint32_t nonce = r.u32();
            if (nonce == pingNonce_) {
                const auto now = std::chrono::steady_clock::now();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - pingSent_).count();
                latency_.store(static_cast<int>(ms));
                inbox_.latencyMs = static_cast<int>(ms);
            }
            break;
        }
        default:
            break;
    }
}

bool Client::writePacket(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!sock_.valid()) return false;
    if (!sock_.sendAll(data.data(), data.size())) {
        running_.store(false);
        return false;
    }
    return true;
}

void Client::sendInput(const Vec3& pos, float yaw, float pitch, uint8_t flags, uint8_t slot) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Input);
    w.vec3(pos);
    w.f32(yaw);
    w.f32(pitch);
    w.u8(flags);
    w.u8(slot);
    w.end();
    writePacket(buf);
}

void Client::sendBreak(const IVec3& block) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Edit);
    w.i16(static_cast<int16_t>(block.x));
    w.i16(static_cast<int16_t>(block.y));
    w.i16(static_cast<int16_t>(block.z));
    w.u8(kAir);
    w.u8(0);
    w.end();
    writePacket(buf);
}

void Client::sendPlace(const IVec3& block, uint8_t type) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Edit);
    w.i16(static_cast<int16_t>(block.x));
    w.i16(static_cast<int16_t>(block.y));
    w.i16(static_cast<int16_t>(block.z));
    w.u8(type);
    w.u8(1);
    w.end();
    writePacket(buf);
}

void Client::sendExplode(const Vec3& pos, float radius) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Explode);
    w.vec3(pos);
    w.f32(radius);
    w.end();
    writePacket(buf);
}

void Client::sendAttack() {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Attack);
    w.end();
    writePacket(buf);
}

void Client::sendChat(const std::string& text) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Chat);
    w.str(text);
    w.end();
    writePacket(buf);
}

void Client::sendRespawn() {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Respawn);
    w.end();
    writePacket(buf);
}

void Client::sendSetMode(GameMode mode) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::SetMode);
    w.u8(static_cast<uint8_t>(mode));
    w.end();
    writePacket(buf);
}

void Client::sendPing() {
    if (!connected()) return;
    pingNonce_ = pingNonce_ + 1;
    pingSent_ = std::chrono::steady_clock::now();
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Ping);
    w.u32(pingNonce_);
    w.end();
    writePacket(buf);
}

void Client::poll(NetInbox& out) {
    std::lock_guard<std::mutex> lock(inboxMutex_);
    out.welcome = inbox_.welcome;
    out.playerId = inbox_.playerId;
    out.seed = inbox_.seed;
    out.mode = inbox_.mode;
    out.players = inbox_.players;
    out.history = std::move(inbox_.history);
    out.playersUpdated = inbox_.playersUpdated;
    out.changes = std::move(inbox_.changes);
    out.debris = std::move(inbox_.debris);
    out.events = std::move(inbox_.events);
    out.serverTick = inbox_.serverTick;
    out.latencyMs = inbox_.latencyMs;
    out.disconnected = inbox_.disconnected;
    out.disconnectReason = inbox_.disconnectReason;

    inbox_.welcome = false;
    inbox_.playersUpdated = false;
    inbox_.disconnected = false;
    inbox_.disconnectReason.clear();
    inbox_.history.clear();
    inbox_.changes.clear();
    inbox_.debris.clear();
    inbox_.events.clear();
}

}  // namespace razlom::net
