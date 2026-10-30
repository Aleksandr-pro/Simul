#include "razlom/net/socket.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace razlom::net {
namespace {

#ifdef _WIN32
constexpr intptr_t kInvalid = static_cast<intptr_t>(INVALID_SOCKET);
#else
constexpr intptr_t kInvalid = -1;
#endif

void closeHandle(intptr_t handle) {
    if (handle == kInvalid) return;
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(handle));
#else
    ::close(static_cast<int>(handle));
#endif
}

}  // namespace

Socket::Socket(intptr_t handle) : handle_(handle) {}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = kInvalid; }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = kInvalid;
    }
    return *this;
}

bool Socket::startup() {
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
        started = true;
    }
#endif
    return true;
}

bool Socket::valid() const { return handle_ != kInvalid; }

bool Socket::setNoDelay(bool enabled) {
    if (!valid()) return false;
    int value = enabled ? 1 : 0;
    return ::setsockopt(static_cast<int>(handle_), IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
}

bool Socket::setReuseAddr(bool enabled) {
    if (!valid()) return false;
    int value = enabled ? 1 : 0;
    return ::setsockopt(static_cast<int>(handle_), SOL_SOCKET, SO_REUSEADDR,
                        reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
}

bool Socket::connectTo(const std::string& host, uint16_t port, int timeoutMs) {
    (void)timeoutMs;
    if (!startup()) return false;
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) return false;

    bool connected = false;
    for (struct addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        intptr_t fd = static_cast<intptr_t>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (fd == kInvalid) continue;
        if (::connect(static_cast<int>(fd), ai->ai_addr, static_cast<socklen_type>(ai->ai_addrlen)) == 0) {
            handle_ = fd;
            connected = true;
            break;
        }
        closeHandle(fd);
    }
    ::freeaddrinfo(result);
    if (connected) setNoDelay(true);
    return connected;
}

bool Socket::listenOn(uint16_t port, int backlog) {
    if (!startup()) return false;
    intptr_t fd = static_cast<intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (fd == kInvalid) return false;
    int yes = 1;
    ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(static_cast<int>(fd), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeHandle(fd);
        return false;
    }
    if (::listen(static_cast<int>(fd), backlog) != 0) {
        closeHandle(fd);
        return false;
    }
    handle_ = fd;
    return true;
}

Socket Socket::accept() {
    if (!valid()) return Socket();
    struct sockaddr_storage addr {};
    socklen_type len = sizeof(addr);
    intptr_t fd = static_cast<intptr_t>(
        ::accept(static_cast<int>(handle_), reinterpret_cast<struct sockaddr*>(&addr), &len));
    Socket s(fd);
    s.setNoDelay(true);
    return s;
}

bool Socket::sendAll(const void* data, size_t size) {
    if (!valid()) return false;
    const char* ptr = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        const int n = ::send(static_cast<SOCKET>(handle_), ptr + sent, static_cast<int>(size - sent), 0);
#else
        const ssize_t n = ::send(static_cast<int>(handle_), ptr + sent, size - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

int Socket::recvSome(void* data, size_t size) {
    if (!valid()) return -1;
#ifdef _WIN32
    return ::recv(static_cast<SOCKET>(handle_), static_cast<char*>(data), static_cast<int>(size), 0);
#else
    const ssize_t n = ::recv(static_cast<int>(handle_), data, size, 0);
    return static_cast<int>(n);
#endif
}

void Socket::shutdownBoth() {
    if (!valid()) return;
#ifdef _WIN32
    ::shutdown(static_cast<SOCKET>(handle_), SD_BOTH);
#else
    ::shutdown(static_cast<int>(handle_), SHUT_RDWR);
#endif
}

void Socket::close() {
    if (!valid()) return;
    closeHandle(handle_);
    handle_ = kInvalid;
}

std::string Socket::peer() const {
    if (!valid()) return {};
    struct sockaddr_storage addr {};
    socklen_type len = sizeof(addr);
    if (::getpeername(static_cast<int>(handle_), reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) return {};
    char buf[64] = {0};
    if (addr.ss_family == AF_INET) {
        auto* in = reinterpret_cast<struct sockaddr_in*>(&addr);
        ::inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(in->sin_port));
    }
    return {};
}

uint16_t Socket::localPort() const {
    if (!valid()) return 0;
    struct sockaddr_in addr {};
    socklen_type len = sizeof(addr);
    if (::getsockname(static_cast<int>(handle_), reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

std::string Socket::localAddress() {
    char hostname[256] = {0};
    if (::gethostname(hostname, sizeof(hostname)) != 0) return "127.0.0.1";
    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    if (::getaddrinfo(hostname, nullptr, &hints, &result) != 0) return "127.0.0.1";
    std::string best = "127.0.0.1";
    for (struct addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        auto* in = reinterpret_cast<struct sockaddr_in*>(ai->ai_addr);
        char buf[64] = {0};
        if (::inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf))) {
            const std::string ip(buf);
            if (ip != "127.0.0.1" && ip.rfind("169.254.", 0) != 0) {
                best = ip;
                break;
            }
        }
    }
    ::freeaddrinfo(result);
    return best;
}

bool Socket::isIpLiteral(const std::string& host) {
    struct in_addr addr4 {};
    if (::inet_pton(AF_INET, host.c_str(), &addr4) == 1) return true;
    struct in6_addr addr6 {};
    return ::inet_pton(AF_INET6, host.c_str(), &addr6) == 1;
}

}  // namespace razlom::net
