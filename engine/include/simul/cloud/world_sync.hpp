#pragma once

#include "simul/core/types.hpp"
#include "simul/core/prng.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace simul::cloud {

struct LeaderboardEntry {
    int rank = 1;
    std::string handle;
    std::string display_name;
    core::NicheId niche = core::NicheId::Tech;
    core::ChannelStage stage = core::ChannelStage::Novice;
    std::int64_t subscribers = 0;
    std::int64_t total_views = 0;
    bool is_player = false;
};

struct WorldPoolStatus {
    bool cloud_connected = false;
    std::string season_id = "season-1";
    core::NicheId niche = core::NicheId::Tech;
    float competition_heat = 1.0f;
    float pool_multiplier = 1.0f;
    int viral_videos_24h = 1;
    std::string headline_ru;
};

/**
 * Нативный сервис синхронизации общего мира:
 * - На Windows использует встроенный системный стек WinHTTP для прямых HTTPS-запросов к Supabase RPC
 *   (0 внешних DLL вроде OpenSSL/libcurl);
 * - При отсутствии ключей окружения автоматически работает в автономном режиме с живыми ИИ-соперниками.
 */
class WorldSyncService {
public:
    WorldSyncService();

    void configure_supabase(std::string project_url, std::string publishable_key, std::string season_id = "season-1");
    bool is_supabase_configured() const noexcept { return !supabase_url_.empty() && !supabase_key_.empty(); }

    WorldPoolStatus fetch_pool_status(core::NicheId niche);

    WorldPoolStatus on_player_video_completed(
        const core::PublishedVideoRecord& video,
        std::int64_t player_subs,
        std::int64_t player_total_views);

    std::vector<LeaderboardEntry> leaderboard(
        core::NicheId niche,
        const std::string& player_handle,
        const std::string& player_name,
        core::ChannelStage player_stage,
        std::int64_t player_subs,
        std::int64_t player_views) const;

private:
    struct RivalChannel {
        std::string handle;
        std::string display_name;
        core::NicheId niche;
        std::int64_t subscribers;
        std::int64_t total_views;
    };

    bool try_supabase_claim_multiplier(core::NicheId niche, float& out_multiplier) const;

    std::string supabase_url_;
    std::string supabase_key_;
    std::string season_id_ = "season-1";

    core::SeededPrng prng_{991042u};
    std::vector<RivalChannel> rivals_;
    std::array<float, core::kNicheCount> heat_{1.04f, 0.98f, 1.00f, 0.94f, 1.10f};
    std::array<int, core::kNicheCount> viral_24h_{2, 1, 1, 1, 3};
    std::array<std::string, core::kNicheCount> headlines_{};
};

} // namespace simul::cloud
