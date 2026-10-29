#include "simul/core/engine.hpp"
#include <chrono>
#include <cstdio>

int main() {
    using namespace simul::core;

    constexpr std::size_t kAgentCount = 100'000;
    EngineConfig cfg{};
    cfg.seed = 20261029u;
    cfg.audience_capacity = kAgentCount;
    cfg.initial_platform_agents = kAgentCount;

    const auto t_init0 = std::chrono::steady_clock::now();
    Engine engine(cfg);
    const auto t_init1 = std::chrono::steady_clock::now();

    VideoReleaseInput in{};
    in.title = "Стресс-тест 100 000 SoA-агентов";
    in.niche = NicheId::Tech;
    in.production_budget_usd = 150.0;
    in.publish_hour_of_day = 19;
    in.editing = {0.90f, 0.88f, 0.88f, -1.0f};
    in.packaging.variant_a = {'A', in.title, 0.90f, 0.86f, 0.14f};
    in.packaging.has_variant_b = true;
    in.packaging.variant_b = {'B', "Альтернативная обложка теста", 0.92f, 0.88f, 0.15f};

    constexpr int kVideos = 12;
    constexpr int kTotalWaveTicks = kVideos * 48;
    std::string err;

    const auto t_sim0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kVideos; ++i) {
        engine.publish_and_simulate_full_video(in, err);
        engine.advance_days(3);
    }
    const auto t_sim1 = std::chrono::steady_clock::now();

    const double init_ms = std::chrono::duration<double, std::milli>(t_init1 - t_init0).count();
    const double sim_ms = std::chrono::duration<double, std::milli>(t_sim1 - t_sim0).count();
    const double per_tick_us = (sim_ms * 1000.0) / kTotalWaveTicks;
    const double soa_ram_mb =
        static_cast<double>(kAgentCount * (3 * sizeof(std::uint8_t) + 5 * sizeof(float) + sizeof(std::uint16_t))) /
        (1024.0 * 1024.0);

    std::printf("================================================================\n");
    std::printf(" Simul Native Engine (C++20 SoA) — Стресс-бенчмарк 100 000 агентов\n");
    std::printf("================================================================\n");
    std::printf(" Память пула 100 000 живых агентов : %.2f МБ (alignas(64) SoA)\n", soa_ram_mb);
    std::printf(" Холодная инициализация 100k пула   : %.2f мс\n", init_ms);
    std::printf(" Прогон 12 релизов (576 волн)       : %.2f мс\n", sim_ms);
    std::printf(" Среднее время 1 часовой волны      : %.1f мкс (~%.0f волн/сек)\n",
                per_tick_us, 1'000'000.0 / per_tick_us);
    std::printf(" Итоговые подписчики / просмотры    : %lld подп. / %lld просм.\n",
                static_cast<long long>(engine.subscribers()),
                static_cast<long long>(engine.total_views()));
    std::printf("================================================================\n");
    return 0;
}
