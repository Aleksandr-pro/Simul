#pragma once

#include "simul/audio/synth.hpp"
#include "simul/cloud/world_sync.hpp"
#include "simul/core/engine.hpp"
#include "simul/core/math_utils.hpp"
#include "simul/ui/renderer_2d.hpp"
#include "simul/ui/widgets.hpp"
#include <string>
#include <vector>

namespace simul::game {

enum class GameTab : int {
    Studio = 0,     // Съёмка и упаковка видео
    Algorithm = 1,  // Живой график 48ч и пост-мортем разбор
    Audience = 2,   // 50k SoA-агентов и живые комментарии
    Career = 3,     // Дилеммы, команда, стримы, сохранения
    World = 4,      // Рейтинг ниши и общий пул показов
};

class ViralGameApp {
public:
    ViralGameApp();

    bool init();
    void shutdown();

    void on_pointer_down(float x, float y);
    void on_pointer_move(float x, float y);
    void on_pointer_up(float x, float y);
    void on_wheel(float dy);

    void update_and_render(float dt_sec, float logical_w, float logical_h);

    audio::NativeSoundSynth& sound() noexcept { return sound_; }

private:
    void draw_hud_header(float x, float y, float w);
    float draw_tab_studio(float x, float y, float w);
    float draw_tab_algorithm(float x, float y, float w);
    float draw_tab_audience(float x, float y, float w);
    float draw_tab_career(float x, float y, float w);
    float draw_tab_world(float x, float y, float w);
    void draw_bottom_nav(float x, float y, float w, float h);

    core::VideoReleaseInput build_current_release() const;
    void start_live_release();
    void run_instant_release();
    void run_100k_benchmark();

    core::Engine engine_;
    cloud::WorldSyncService world_;
    audio::NativeSoundSynth sound_;
    ui::Renderer2D renderer_;
    ui::UiContext ui_;

    GameTab current_tab_ = GameTab::Studio;
    bool show_retention_chart_ = false;

    // Параметры текущего готовящегося ролика
    core::NicheId selected_niche_ = core::NicheId::Tech;
    int selected_title_preset_ = 0;
    bool use_ab_test_ = true;
    int selected_trend_idx_ = -1; // -1 = авторская тема
    int sponsor_mode_ = 0;        // 0 = нет, 1 = белый бренд, 2 = скам-токен
    float hook_strength_ = 0.80f;
    float pacing_tightness_ = 0.76f;
    float visual_contrast_ = 0.78f;
    float clickbait_level_ = 0.16f;
    float budget_usd_ = 120.0f;

    float live_hour_timer_ = 0.0f;
    float content_height_ = 0.0f;
    core::SpringValue anim_subs_{0.0f, 180.0f, 24.0f};
    core::SpringValue anim_cash_{700.0f, 180.0f, 24.0f};

    cloud::WorldPoolStatus pool_status_{};
    std::vector<cloud::LeaderboardEntry> leaderboard_;
    std::string status_banner_ru_;
    std::string bench_result_ru_;
};

} // namespace simul::game
