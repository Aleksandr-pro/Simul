// РАЗЛОМ — интеграционные тесты сети: хост в потоке, два клиента, синхронизация.
#include <chrono>
#include <functional>
#include <thread>

#include "razlom/net/client.hpp"
#include "razlom/net/server.hpp"
#include "test_util.hpp"

using namespace razlom;

namespace {

bool waitFor(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

bool containsAirChange(const std::vector<BlockChange>& changes, int x, int y, int z) {
    for (const BlockChange& c : changes) {
        if (c.x == x && c.y == y && c.z == z && c.block == kAir) return true;
    }
    return false;
}

bool containsBlock(const std::vector<BlockChange>& history, int x, int y, int z, uint8_t block) {
    for (const BlockChange& c : history) {
        if (c.x == x && c.y == y && c.z == z && c.block == block) return true;
    }
    return false;
}

}  // namespace

TEST(net_host_and_two_clients_sync) {
    SessionConfig cfg;
    cfg.seed = 555;
    cfg.mode = GameMode::Sandbox;
    cfg.maxPlayers = 4;

    net::Server server(cfg);
    CHECK(server.start(0));
    CHECK(server.port() != 0);
    CHECK(!server.roomCode().empty());
    CHECK(server.joinLink().find("razlom://") == 0);

    net::Client host;
    net::Client guest;
    CHECK(host.connect("127.0.0.1", server.port(), "Хост"));
    CHECK(guest.connect("127.0.0.1", server.port(), "Гость"));
    CHECK(waitFor([&] { return host.connected() && guest.connected(); }, 6000));

    net::NetInbox hostInbox;
    net::NetInbox guestInbox;
    hostInbox.clear();
    guestInbox.clear();
    host.poll(hostInbox);
    guest.poll(guestInbox);
    CHECK(hostInbox.welcome);
    CHECK(guestInbox.welcome);
    CHECK(hostInbox.playerId != 0);
    CHECK(guestInbox.playerId != 0);
    CHECK(hostInbox.playerId != guestInbox.playerId);
    CHECK_EQ(hostInbox.seed, 555u);

    // оба клиента видят двух игроков
    CHECK(waitFor(
        [&] {
            net::NetInbox tmp;
            tmp.clear();
            guest.poll(tmp);
            return tmp.playersUpdated && tmp.players.size() == 2;
        },
        4000));

    // хост ломает блок на площадке спавна — гость получает изменение
    const int bx = World::kSizeX / 2;
    const int bz = World::kSizeZ / 2;
    host.sendBreak(IVec3{bx, 34, bz});

    bool guestGotEdit = false;
    CHECK(waitFor(
        [&] {
            net::NetInbox tmp;
            tmp.clear();
            guest.poll(tmp);
            if (containsAirChange(tmp.changes, bx, 34, bz)) guestGotEdit = true;
            return guestGotEdit;
        },
        4000));
    CHECK(guestGotEdit);

    // чат доходит до всех
    guest.sendChat("привет из теста");
    bool chatSeen = false;
    CHECK(waitFor(
        [&] {
            net::NetInbox tmp;
            tmp.clear();
            host.poll(tmp);
            for (const SessionEvent& ev : tmp.events) {
                if (ev.kind == SessionEventKind::Chat && ev.text.find("привет из теста") != std::string::npos) {
                    chatSeen = true;
                }
            }
            return chatSeen;
        },
        4000));
    CHECK(chatSeen);

    // поздний игрок получает историю мира (включая сломанный блок)
    net::Client late;
    CHECK(late.connect("127.0.0.1", server.port(), "Поздний"));
    CHECK(waitFor([&] { return late.connected(); }, 6000));
    net::NetInbox lateInbox;
    lateInbox.clear();
    late.poll(lateInbox);
    CHECK(lateInbox.welcome);
    CHECK(containsBlock(lateInbox.history, bx, 34, bz, kAir));

    // гость выходит — хост видит, что игроков стало меньше
    guest.disconnect();
    CHECK(waitFor([&] { return server.clientCount() == 2; }, 4000));

    late.disconnect();
    host.disconnect();
    server.stop();
    CHECK_EQ(server.clientCount(), 0);
}

TEST(net_server_rejects_when_full) {
    SessionConfig cfg;
    cfg.seed = 31;
    cfg.maxPlayers = 1;
    net::Server server(cfg);
    CHECK(server.start(0));

    net::Client a;
    CHECK(a.connect("127.0.0.1", server.port(), "Первый"));
    CHECK(waitFor([&] { return a.connected(); }, 6000));

    net::Client b;
    CHECK(b.connect("127.0.0.1", server.port(), "Второй"));
    CHECK(waitFor([&] { return b.state() == net::ClientState::Failed; }, 6000));
    CHECK_EQ(server.clientCount(), 1);

    b.disconnect();
    a.disconnect();
    server.stop();
}
