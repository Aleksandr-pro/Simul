// РАЗЛОМ — кроссплатформенные TCP-сокеты (Windows / Linux / Android / macOS).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace razlom::net {

class Socket {
public:
    Socket() = default;
    explicit Socket(intptr_t handle);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    static bool startup();  // WSAStartup на Windows

    bool connectTo(const std::string& host, uint16_t port, int timeoutMs = 5000);
    bool listenOn(uint16_t port, int backlog = 8);
    Socket accept();
    bool setNoDelay(bool enabled);
    bool setReuseAddr(bool enabled);
    bool valid() const;

    // Возвращает true, если все байты отправлены.
    bool sendAll(const void* data, size_t size);
    // >0 — прочитано байт, 0 — соединение закрыто, <0 — ошибка.
    int recvSome(void* data, size_t size);

    void shutdownBoth();
    void close();

    intptr_t handle() const { return handle_; }
    std::string peer() const;
    uint16_t localPort() const;

    static std::string localAddress();
    static bool isIpLiteral(const std::string& host);

private:
    intptr_t handle_ = -1;
};

}  // namespace razlom::net
