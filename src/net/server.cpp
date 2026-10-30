#include "razlom/net/server.hpp"

#include <algorithm>
#include <chrono>

#include "razlom/core/rng.hpp"

namespace razlom::net {

struct Server::Conn {
    Socket sock;
    uint32_t connId = 0;
    std::atomic<uint32_t> playerId{0};
    std::atomic<bool> alive{true};
    bool joined = false;
    std::string name;
    std::thread reader;
    std::mutex sendMutex;
};

Server::Server(const SessionConfig& config) : config_(config), session_(config) {
    sendBuffer_.reserve(64 * 1024);
}

Server::~Server() { stop(); }

bool Server::start(uint16_t port) {
    if (running_.load()) return false;
    if (!Socket::startup()) return false;

    Socket listener;
    uint16_t bound = 0;
    if (port == 0) {
        // для тестов: свободный порт из диапазона
        for (uint16_t candidate = 47800; candidate < 47900; ++candidate) {
            if (listener.listenOn(candidate)) {
                bound = candidate;
                break;
            }
        }
    } else if (listener.listenOn(port)) {
        bound = port;
    }
    if (bound == 0) return false;

    listener_ = std::move(listener);
    port_ = bound;
    running_.store(true);
    acceptThread_ = std::thread([this] { acceptLoop(); });
    tickThread_ = std::thread([this] { tickLoop(); });
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) {
        // всё равно подчистим остатки
    }
    listener_.shutdownBoth();
    listener_.close();
    if (acceptThread_.joinable()) acceptThread_.join();
    if (tickThread_.joinable()) tickThread_.join();

    std::vector<std::shared_ptr<Conn>> conns;
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        conns.swap(conns_);
    }
    for (const auto& conn : conns) {
        conn->alive.store(false);
        conn->sock.shutdownBoth();
        conn->sock.close();
        if (conn->reader.joinable()) conn->reader.join();
    }
    clientCount_.store(0);
    listener_ = Socket();
    port_ = 0;
}

std::string Server::roomCode() const {
    static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const uint32_t h = hashU32(static_cast<uint32_t>(port_) * 2654435761u + config_.seed);
    std::string code;
    for (int i = 0; i < 4; ++i) code.push_back(kAlphabet[(h >> (i * 5)) % 32u]);
    return code;
}

std::string Server::joinLink() const {
    return "razlom://" + Socket::localAddress() + ":" + std::to_string(port_) + "/" + roomCode();
}

void Server::acceptLoop() {
    while (running_.load()) {
        Socket sock = listener_.accept();
        if (!sock.valid()) {
            if (!running_.load()) break;
            continue;
        }

        auto conn = std::make_shared<Conn>();
        conn->sock = std::move(sock);
        conn->connId = nextConnId_++;

        // Мьютекс держим до запуска потока чтения: тик-поток не должен увидеть
        // соединение, у которого ещё нет потока.
        std::lock_guard<std::mutex> lock(connsMutex_);
        conn->reader = std::thread([this, conn] { readLoop(conn); });
        conns_.push_back(conn);
    }
}

void Server::readLoop(const std::shared_ptr<Conn>& conn) {
    std::vector<uint8_t> body;
    while (running_.load() && conn->alive.load()) {
        uint8_t header[4];
        size_t got = 0;
        while (got < 4) {
            const int n = conn->sock.recvSome(header + got, 4 - got);
            if (n <= 0) {
                conn->alive.store(false);
                return;
            }
            got += static_cast<size_t>(n);
        }
        const uint32_t len = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) |
                             (static_cast<uint32_t>(header[2]) << 16) | (static_cast<uint32_t>(header[3]) << 24);
        if (len == 0 || len > kMaxPacketSize) {
            conn->alive.store(false);
            return;
        }
        body.resize(len);
        size_t received = 0;
        while (received < len) {
            const int n = conn->sock.recvSome(body.data() + received, len - received);
            if (n <= 0) {
                conn->alive.store(false);
                return;
            }
            received += static_cast<size_t>(n);
        }

        Reader r(body.data(), body.size());
        const MsgType type = static_cast<MsgType>(r.u8());
        if (!r.ok()) continue;

        Incoming msg;
        msg.connId = conn->connId;
        msg.type = type;
        msg.payload = body;  // первый байт — тип, парсим заново в тик-потоке
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (incoming_.size() > 4096) continue;
            incoming_.push_back(std::move(msg));
        }
    }
}

std::shared_ptr<Server::Conn> Server::findConn(uint32_t connId) {
    std::lock_guard<std::mutex> lock(connsMutex_);
    for (const auto& conn : conns_) {
        if (conn->connId == connId) return conn;
    }
    return nullptr;
}

void Server::sendRaw(const std::shared_ptr<Conn>& conn, const std::vector<uint8_t>& data) {
    if (!conn || !conn->alive.load()) return;
    std::lock_guard<std::mutex> lock(conn->sendMutex);
    if (!conn->sock.sendAll(data.data(), data.size())) {
        conn->alive.store(false);
        conn->sock.shutdownBoth();
    }
}

void Server::sendText(const std::shared_ptr<Conn>& conn, const std::string& text, uint8_t eventKind) {
    std::vector<uint8_t> buf;
    Writer w(buf);
    w.begin(MsgType::Event);
    w.u8(eventKind);
    w.vec3(Vec3{});
    w.str(text);
    w.end();
    sendRaw(conn, buf);
}

void Server::sendWelcome(const std::shared_ptr<Conn>& conn, uint32_t playerId) {
    std::vector<uint8_t> buf;
    writeWelcome(buf, static_cast<uint8_t>(playerId), session_.seed(), static_cast<uint8_t>(session_.mode()),
                 session_.playerSnapshots(), session_.history());
    sendRaw(conn, buf);
}

void Server::handleIncoming() {
    std::deque<Incoming> batch;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        batch.swap(incoming_);
    }
    for (Incoming& msg : batch) {
        auto conn = findConn(msg.connId);
        if (!conn || !conn->alive.load()) continue;

        Reader r(msg.payload.data(), msg.payload.size());
        r.u8();  // тип

        if (msg.type == MsgType::Hello) {
            if (conn->joined) continue;
            const uint16_t version = r.u16();
            const std::string name = r.str();
            if (!r.ok()) continue;
            if (version != kProtocolVersion) {
                sendText(conn, "Версия игры не совпадает с хостом", static_cast<uint8_t>(SessionEventKind::Info));
                conn->alive.store(false);
                conn->sock.shutdownBoth();
                continue;
            }
            std::string error;
            const uint32_t playerId = session_.addPlayer(name, &error);
            if (playerId == 0) {
                sendText(conn, error.empty() ? "Не удалось войти в мир" : error,
                         static_cast<uint8_t>(SessionEventKind::Info));
                conn->alive.store(false);
                conn->sock.shutdownBoth();
                continue;
            }
            conn->playerId.store(playerId);
            conn->joined = true;
            conn->name = name;
            clientCount_.fetch_add(1);
            sendWelcome(conn, playerId);
            continue;
        }

        if (!conn->joined) continue;
        const uint32_t playerId = conn->playerId.load();

        switch (msg.type) {
            case MsgType::Input: {
                const Vec3 pos = r.vec3();
                const float yaw = r.f32();
                const float pitch = r.f32();
                const uint8_t flags = r.u8();
                const uint8_t slot = r.u8();
                if (r.ok()) session_.applyInput(playerId, pos, yaw, pitch, flags, slot);
                break;
            }
            case MsgType::Edit: {
                const int16_t x = r.i16();
                const int16_t y = r.i16();
                const int16_t z = r.i16();
                const uint8_t block = r.u8();
                const uint8_t action = r.u8();
                if (!r.ok()) break;
                if (action == 0) {
                    session_.requestBreak(playerId, IVec3{x, y, z});
                } else {
                    session_.requestPlace(playerId, IVec3{x, y, z}, block);
                }
                break;
            }
            case MsgType::Explode: {
                const Vec3 pos = r.vec3();
                const float radius = r.f32();
                if (r.ok()) session_.requestExplode(playerId, pos, radius);
                break;
            }
            case MsgType::Attack:
                session_.requestAttack(playerId);
                break;
            case MsgType::Chat: {
                const std::string text = r.str();
                if (r.ok()) session_.chat(playerId, text);
                break;
            }
            case MsgType::Respawn:
                session_.requestRespawn(playerId);
                break;
            case MsgType::SetMode: {
                const uint8_t mode = r.u8();
                if (r.ok() && mode <= static_cast<uint8_t>(GameMode::BaseDefense)) {
                    session_.requestMode(static_cast<GameMode>(mode));
                }
                break;
            }
            case MsgType::Ping: {
                const uint32_t nonce = r.u32();
                std::vector<uint8_t> buf;
                Writer w(buf);
                w.begin(MsgType::Pong);
                w.u32(nonce);
                w.end();
                sendRaw(conn, buf);
                break;
            }
            default:
                break;
        }
    }
}

void Server::broadcast() {
    std::vector<std::shared_ptr<Conn>> conns;
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        for (const auto& conn : conns_)
            if (conn->alive.load() && conn->joined) conns.push_back(conn);
    }
    if (conns.empty()) {
        session_.takeChanges();
        session_.takeEvents();
        return;
    }

    const std::vector<BlockChange> changes = session_.takeChanges();
    const std::vector<SessionEvent> events = session_.takeEvents();
    const std::vector<PlayerSnapshot> players = session_.playerSnapshots();
    const std::vector<DebrisSnapshot> debris = session_.debrisSnapshot();

    std::vector<uint8_t> changesPacket;
    if (!changes.empty()) {
        Writer w(changesPacket);
        w.begin(MsgType::Changes);
        w.u16(static_cast<uint16_t>(changes.size()));
        for (const BlockChange& c : changes) {
            w.i16(c.x);
            w.i16(c.y);
            w.i16(c.z);
            w.u8(c.block);
        }
        w.end();
    }

    std::vector<uint8_t> playersPacket;
    {
        Writer w(playersPacket);
        w.begin(MsgType::Players);
        w.u8(static_cast<uint8_t>(players.size()));
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
        w.end();
    }

    std::vector<uint8_t> debrisPacket;
    {
        Writer w(debrisPacket);
        w.begin(MsgType::Debris);
        w.u16(static_cast<uint16_t>(debris.size()));
        for (const DebrisSnapshot& d : debris) {
            w.u32(d.id);
            w.vec3(d.pos);
            w.u8(d.block);
        }
        w.end();
    }

    std::vector<uint8_t> tickPacket;
    {
        Writer w(tickPacket);
        w.begin(MsgType::Tick);
        w.u32(session_.tickIndex());
        w.u32(static_cast<uint32_t>(session_.historySize()));
        w.end();
    }

    std::vector<std::vector<uint8_t>> eventPackets;
    eventPackets.reserve(events.size());
    for (const SessionEvent& ev : events) {
        std::vector<uint8_t> buf;
        Writer w(buf);
        w.begin(MsgType::Event);
        w.u8(static_cast<uint8_t>(ev.kind));
        w.vec3(ev.pos);
        w.str(ev.text);
        w.end();
        eventPackets.push_back(std::move(buf));
    }

    for (const auto& conn : conns) {
        if (!changesPacket.empty()) sendRaw(conn, changesPacket);
        sendRaw(conn, playersPacket);
        sendRaw(conn, debrisPacket);
        sendRaw(conn, tickPacket);
        for (const auto& ev : eventPackets) sendRaw(conn, ev);
    }
}

void Server::cleanupDead() {
    std::vector<std::shared_ptr<Conn>> dead;
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        for (auto it = conns_.begin(); it != conns_.end();) {
            if ((*it)->alive.load()) {
                ++it;
                continue;
            }
            dead.push_back(*it);
            it = conns_.erase(it);
        }
    }
    for (const auto& conn : dead) {
        if (conn->joined) {
            session_.removePlayer(conn->playerId.load());
            clientCount_.fetch_sub(1);
            conn->joined = false;
        }
        conn->sock.shutdownBoth();
        conn->sock.close();
        if (conn->reader.joinable()) conn->reader.join();
    }
}

void Server::tickLoop() {
    using clock = std::chrono::steady_clock;
    const auto step = std::chrono::microseconds(1000000 / std::max(1, config_.tickRate));
    auto next = clock::now();
    const float dt = 1.0f / static_cast<float>(std::max(1, config_.tickRate));

    while (running_.load()) {
        handleIncoming();
        session_.tick(dt);
        broadcast();
        cleanupDead();

        next += step;
        const auto now = clock::now();
        if (next > now) {
            std::this_thread::sleep_until(next);
        } else {
            next = now;
        }
    }
}

}  // namespace razlom::net
