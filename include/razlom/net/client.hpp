// РАЗЛОМ — сетевой клиент: асинхронное подключение, приём снимков мира.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "razlom/core/session.hpp"
#include "razlom/net/protocol.hpp"
#include "razlom/net/socket.hpp"

namespace razlom::net {

enum class ClientState : int {
    Idle = 0,
    Connecting = 1,
    Handshaking = 2,
    Connected = 3,
    Failed = 4,
};

struct NetInbox {
    bool welcome = false;
    uint8_t playerId = 0;
    uint32_t seed = 0;
    uint8_t mode = 0;
    std::vector<PlayerSnapshot> players;
    std::vector<BlockChange> history;

    bool playersUpdated = false;
    std::vector<BlockChange> changes;
    std::vector<DebrisSnapshot> debris;
    std::vector<SessionEvent> events;
    uint32_t serverTick = 0;
    int latencyMs = -1;

    bool disconnected = false;
    std::string disconnectReason;

    void clear() {
        welcome = false;
        playerId = 0;
        seed = 0;
        mode = 0;
        players.clear();
        history.clear();
        playersUpdated = false;
        changes.clear();
        debris.clear();
        events.clear();
        serverTick = 0;
        disconnected = false;
        disconnectReason.clear();
    }
};

class Client {
public:
    Client();
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool connect(const std::string& host, uint16_t port, const std::string& name);
    void disconnect();
    bool connected() const { return state_.load() == ClientState::Connected; }
    ClientState state() const { return state_.load(); }
    std::string error() const;
    int latencyMs() const { return latency_.load(); }

    void sendInput(const Vec3& pos, float yaw, float pitch, uint8_t flags, uint8_t slot);
    void sendBreak(const IVec3& block);
    void sendPlace(const IVec3& block, uint8_t type);
    void sendExplode(const Vec3& pos, float radius);
    void sendAttack();
    void sendChat(const std::string& text);
    void sendRespawn();
    void sendSetMode(GameMode mode);
    void sendPing();

    void poll(NetInbox& out);

private:
    void connectLoop(std::string host, uint16_t port, std::string name);
    void readLoop();
    void handleMessage(MsgType type, Reader& r);
    bool writePacket(const std::vector<uint8_t>& data);
    void setError(const std::string& text);

    Socket sock_;
    std::thread reader_;
    std::atomic<bool> running_{false};
    std::atomic<ClientState> state_{ClientState::Idle};
    std::atomic<int> latency_{-1};
    mutable std::mutex sendMutex_;
    std::mutex inboxMutex_;
    mutable std::mutex errorMutex_;
    NetInbox inbox_;
    std::string error_;
    std::chrono::steady_clock::time_point pingSent_{};
    uint32_t pingNonce_ = 0;
};

}  // namespace razlom::net
