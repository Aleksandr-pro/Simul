// РАЗЛОМ — выделенный сервер мира (headless). Друзья подключаются по ссылке.
//   razlom_server --port 47800 --seed 1234 --mode sandbox --players 4
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "razlom/core/session.hpp"
#include "razlom/net/server.hpp"

namespace {
volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

void printUsage() {
    std::printf(
        "РАЗЛОМ — хост мира\n"
        "  --port <число>     порт (по умолчанию 47800)\n"
        "  --seed <число>     сид мира (по умолчанию случайный)\n"
        "  --mode <режим>     sandbox | last | base\n"
        "  --players <число>  максимум игроков (1..16)\n");
}
}  // namespace

int main(int argc, char** argv) {
    uint16_t port = razlom::net::kDefaultPort;
    uint32_t seed = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0xffffffffu);
    razlom::GameMode mode = razlom::GameMode::Sandbox;
    int maxPlayers = 4;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::printf("не хватает значения для %s\n", name);
                std::exit(1);
            }
            return argv[++i];
        };
        if (arg == "--port") {
            port = static_cast<uint16_t>(std::atoi(next("--port")));
        } else if (arg == "--seed") {
            seed = static_cast<uint32_t>(std::strtoul(next("--seed"), nullptr, 10));
        } else if (arg == "--mode") {
            const std::string value = next("--mode");
            if (value == "last" || value == "laststanding") mode = razlom::GameMode::LastStanding;
            else if (value == "base") mode = razlom::GameMode::BaseDefense;
            else mode = razlom::GameMode::Sandbox;
        } else if (arg == "--players") {
            maxPlayers = std::atoi(next("--players"));
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }
    if (maxPlayers < 1) maxPlayers = 1;
    if (maxPlayers > 16) maxPlayers = 16;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    razlom::SessionConfig config;
    config.seed = seed;
    config.mode = mode;
    config.maxPlayers = maxPlayers;
    config.tickRate = 20;

    razlom::net::Server server(config);
    if (!server.start(port)) {
        std::printf("не удалось запустить мир на порту %u\n", static_cast<unsigned>(port));
        return 1;
    }

    std::printf("==============================================\n");
    std::printf("  РАЗЛОМ — мир поднят\n");
    std::printf("  режим:   %s\n", razlom::gameModeName(mode));
    std::printf("  сид:     %u\n", seed);
    std::printf("  игроки:  до %d\n", maxPlayers);
    std::printf("  порт:    %u\n", static_cast<unsigned>(server.port()));
    std::printf("  код:     %s\n", server.roomCode().c_str());
    std::printf("  ссылка:  %s\n", server.joinLink().c_str());
    std::printf("==============================================\n");
    std::printf("Клиент: Razlom <ссылка>  (или вставить ссылку в меню «Присоединиться»)\n");
    std::fflush(stdout);

    int seconds = 0;
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++seconds;
        if (seconds % 10 == 0) {
            std::printf("[%d c] игроков: %d\n", seconds, server.clientCount());
            std::fflush(stdout);
        }
    }

    std::printf("останавливаем мир…\n");
    server.stop();
    std::printf("готово\n");
    return 0;
}
