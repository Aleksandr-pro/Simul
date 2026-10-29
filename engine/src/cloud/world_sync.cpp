#include "simul/cloud/world_sync.hpp"
#include "simul/core/math_utils.hpp"
#include <algorithm>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace simul::cloud {

WorldSyncService::WorldSyncService() {
    rivals_ = {
        {"@cyber_max", "КиберМакс Обзоры", core::NicheId::Tech, 185'000, 14'200'000},
        {"@nano_lab", "НаноЛаб", core::NicheId::Tech, 42'000, 2'900'000},
        {"@volt_bench", "Вольт Тест", core::NicheId::Tech, 9'400, 610'000},
        {"@frag_queen", "FragQueen", core::NicheId::Gaming, 310'000, 28'500'000},
        {"@indie_pixel", "Пиксельный Угол", core::NicheId::Gaming, 24'500, 1'750'000},
        {"@cozy_mira", "Мира Влог", core::NicheId::Lifestyle, 120'000, 8'400'000},
        {"@brain_spark", "Искра Науки", core::NicheId::Edutainment, 260'000, 19'100'000},
        {"@mega_show", "Шоу Без Границ", core::NicheId::Entertainment, 890'000, 94'000'000},
    };

    if (const char* env_url = std::getenv("SIMUL_SUPABASE_URL")) {
        if (const char* env_key = std::getenv("SIMUL_SUPABASE_KEY")) {
            configure_supabase(env_url, env_key);
        }
    }
}

void WorldSyncService::configure_supabase(
    std::string project_url, std::string publishable_key, std::string season_id) {
    supabase_url_ = std::move(project_url);
    supabase_key_ = std::move(publishable_key);
    season_id_ = std::move(season_id);
}

bool WorldSyncService::try_supabase_claim_multiplier(core::NicheId niche, float& out_multiplier) const {
    if (!is_supabase_configured()) return false;

#if defined(_WIN32)
    // Прямой нативный HTTPS POST к /rest/v1/rpc/claim_niche_pool_multiplier через системный WinHTTP
    std::string host = supabase_url_;
    const auto pos = host.find("://");
    if (pos != std::string::npos) host = host.substr(pos + 3);
    const auto slash = host.find('/');
    if (slash != std::string::npos) host = host.substr(0, slash);
    if (host.empty()) return false;

    std::wstring whost(host.begin(), host.end());
    HINTERNET hSession = WinHttpOpen(L"SimulNativeEngine/0.2",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 1500, 1500, 1500, 1500);

    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", L"/rest/v1/rpc/claim_niche_pool_multiplier",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    const std::string hdr_str =
        "Content-Type: application/json\r\napikey: " + supabase_key_ +
        "\r\nAuthorization: Bearer " + supabase_key_ + "\r\n";
    std::wstring whdrs(hdr_str.begin(), hdr_str.end());

    const std::string body =
        std::string("{\"p_season_id\":\"") + season_id_ +
        "\",\"p_niche\":\"" + core::niche_spec(niche).code + "\"}";

    bool ok = false;
    if (WinHttpSendRequest(hRequest, whdrs.c_str(), static_cast<DWORD>(-1L),
                           const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                           static_cast<DWORD>(body.size()), 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        char resp[64]{};
        DWORD downloaded = 0;
        if (WinHttpReadData(hRequest, resp, sizeof(resp) - 1, &downloaded) && downloaded > 0) {
            const float parsed = std::strtof(resp, nullptr);
            if (parsed >= 0.4f && parsed <= 1.5f) {
                out_multiplier = parsed;
                ok = true;
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
#else
    (void)niche;
    (void)out_multiplier;
    return false;
#endif
}

WorldPoolStatus WorldSyncService::fetch_pool_status(core::NicheId niche) {
    const auto idx = static_cast<std::size_t>(niche);
    WorldPoolStatus st{};
    st.season_id = season_id_;
    st.niche = niche;
    st.competition_heat = heat_[idx];
    st.pool_multiplier = core::clamp_val(1.0f / std::max(0.75f, heat_[idx]), 0.58f, 1.22f);
    st.viral_videos_24h = viral_24h_[idx];
    st.headline_ru = headlines_[idx];

    float remote_mult = 1.0f;
    if (try_supabase_claim_multiplier(niche, remote_mult)) {
        st.cloud_connected = true;
        st.pool_multiplier = remote_mult;
    }
    return st;
}

WorldPoolStatus WorldSyncService::on_player_video_completed(
    const core::PublishedVideoRecord& video,
    std::int64_t /*player_subs*/,
    std::int64_t /*player_total_views*/) {
    for (auto& r : rivals_) {
        const float g = prng_.range(0.996f, 1.030f);
        r.subscribers = std::llround(static_cast<double>(r.subscribers) * static_cast<double>(g));
        r.total_views += std::llround(static_cast<double>(r.subscribers) * static_cast<double>(prng_.range(0.15f, 0.80f)));
    }

    for (std::size_t i = 0; i < core::kNicheCount; ++i) {
        float h = 1.0f + (heat_[i] - 1.0f) * 0.85f;
        if (prng_.chance(0.18f)) {
            h += 0.14f;
            viral_24h_[i] += 1;
            for (const auto& r : rivals_) {
                if (static_cast<std::size_t>(r.niche) == i) {
                    headlines_[i] = "У соперника «" + r.display_name + "» выстрелил хит и сжал пул показов!";
                    break;
                }
            }
        } else {
            viral_24h_[i] = std::max(0, viral_24h_[i] - 1);
        }
        heat_[i] = core::clamp_val(h, 0.78f, 1.85f);
    }

    const auto v_idx = static_cast<std::size_t>(video.niche);
    if (video.max_wave_reached >= 3) {
        heat_[v_idx] = core::clamp_val(
            heat_[v_idx] + (video.max_wave_reached == 4 ? 0.15f : 0.07f),
            0.78f,
            1.95f);
        viral_24h_[v_idx] += 1;
        headlines_[v_idx] = "Ваш ролик ворвался в тренды и забрал трафик ниши!";
    }

    return fetch_pool_status(video.niche);
}

std::vector<LeaderboardEntry> WorldSyncService::leaderboard(
    core::NicheId niche,
    const std::string& player_handle,
    const std::string& player_name,
    core::ChannelStage player_stage,
    std::int64_t player_subs,
    std::int64_t player_views) const {
    std::vector<LeaderboardEntry> list;
    for (const auto& r : rivals_) {
        if (r.niche != niche) continue;
        LeaderboardEntry e{};
        e.handle = r.handle;
        e.display_name = r.display_name;
        e.niche = r.niche;
        e.stage = core::resolve_stage_spec(r.subscribers).id;
        e.subscribers = r.subscribers;
        e.total_views = r.total_views;
        e.is_player = false;
        list.push_back(std::move(e));
    }

    LeaderboardEntry me{};
    me.handle = player_handle;
    me.display_name = player_name;
    me.niche = niche;
    me.stage = player_stage;
    me.subscribers = player_subs;
    me.total_views = player_views;
    me.is_player = true;
    list.push_back(std::move(me));

    std::sort(list.begin(), list.end(), [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
        if (a.subscribers != b.subscribers) return a.subscribers > b.subscribers;
        return a.total_views > b.total_views;
    });

    for (std::size_t i = 0; i < list.size(); ++i) {
        list[i].rank = static_cast<int>(i + 1);
    }
    return list;
}

} // namespace simul::cloud
