// РАЗЛОМ — авторитетный хост мира: один поток на приём, один на симуляцию.
// Мир создаётся одной кнопкой, друзья подключаются по адресу хоста.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "razlom/core/session.hpp"
#include "razlom/net/protocol.hpp"
#include "razlom/net/socket.hpp"

namespace razlom::net {

class Server {
public:
    explicit Server(const SessionConfig& config = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(uint16_t port = kDefaultPort);
    void stop();

    bool running() const { return running_.load(); }
    uint16_t port() const { return port_; }
    std::string roomCode() const;
    std::string joinLink() const;
    int clientCount() const { return clientCount_.load(); }

private:
    struct Conn;

    struct Incoming {
        uint32_t connId = 0;
        MsgType type = MsgType::Invalid;
        std::vector<uint8_t> payload;
    };

    void acceptLoop();
    void readLoop(const std::shared_ptr<Conn>& conn);
    void tickLoop();
    void handleIncoming();
    void broadcast();
    void cleanupDead();
    std::shared_ptr<Conn> findConn(uint32_t connId);
    void sendRaw(const std::shared_ptr<Conn>& conn, const std::vector<uint8_t>& data);
    void sendText(const std::shared_ptr<Conn>& conn, const std::string& text, uint8_t eventKind);
    void sendWelcome(const std::shared_ptr<Conn>& conn, uint32_t playerId);

    SessionConfig config_;
    Session session_;
    Socket listener_;
    std::atomic<bool> running_{false};
    std::atomic<int> clientCount_{0};
    uint16_t port_ = 0;
    uint32_t nextConnId_ = 1;

    std::thread acceptThread_;
    std::thread tickThread_;
    std::mutex connsMutex_;
    std::vector<std::shared_ptr<Conn>> conns_;
    std::mutex queueMutex_;
    std::deque<Incoming> incoming_;
    std::vector<uint8_t> sendBuffer_;  // только для tick-потока
};

}  // namespace razlom::net
