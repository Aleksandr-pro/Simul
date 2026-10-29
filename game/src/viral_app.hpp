#pragma once

#include "studio_scene.hpp"
#include "simul/audio/synth.hpp"
#include "simul/cloud/world_sync.hpp"
#include "simul/core/engine.hpp"
#include "simul/core/math_utils.hpp"
#include "simul/ui/renderer_2d.hpp"
#include "simul/ui/widgets.hpp"
#include <array>
#include <string>
#include <vector>

namespace simul::game {

enum class GameTab : int {
    Studio = 0,     // Живая студия + 3 шага создания видео
    Algorithm = 1,  // 48-часовой график волн и пост-мортем
    Audience = 2,   // Живые сегменты SoA и комментарии
    Career = 3,     // Дилеммы, команда, апгрейды, сейвы
    World = 4,      // Рейтинг ниши и общий пул показов
};

enum class StudioStep : int {
    Concept = 0,    // 1. Идея, ниша, тренд и спонсор
    Timeline = 1,   // 2. Мини-игра: таймлайн нелинейного монтажа
    Thumbnail = 2,  // 3. Мини-игра: визуальный конструктор обложки A/B
};

struct TimelineClip {
    const char* name_ru = "";
    bool is_filler = false; // "вода" / пауза, которую выгодно вырезать
    bool cut = false;       // вырезан ли клип игроком
    bool boosted = false;   // усилен ли клип (SFX / зум)
    float base_energy = 0.7f;
};

class ViralGameApp {
public:
    ViralGameApp();

    void set_storage_directory(std::string dir);
    void set_safe_area(float top_dp, float bottom_dp, float left_dp = 0.0f, float right_dp = 0.0f) noexcept;

    bool init();
    void shutdown();
    void save_autosave();

    void on_pointer_down(float x, float y, bool is_touch = false);
    void on_pointer_move(float x, float y, bool is_touch = false);
    void on_pointer_up(float x, float y, bool is_touch = false);
    void on_wheel(float dy);

    void update_and_render(float dt_sec, float logical_w, float logical_h);

    audio::NativeSoundSynth& sound() noexcept { return sound_; }
    const core::Engine& engine() const noexcept { return engine_; }

private:
    float draw_hud_header(float x, float y, float w);
    float draw_tab_studio(float x, float y, float w);
    float draw_studio_step_concept(float x, float y, float w);
    float draw_studio_step_timeline(float x, float y, float w);
    float draw_studio_step_thumbnail(float x, float y, float w);

    float draw_tab_algorithm(float x, float y, float w);
    float draw_tab_audience(float x, float y, float w);
    float draw_tab_career(float x, float y, float w);
    float draw_tab_world(float x, float y, float w);
    void draw_bottom_nav(float x, float y, float w, float h);

    void reset_timeline_clips();
    void auto_edit_timeline();
    int used_timeline_boosts() const noexcept;
    int max_timeline_boosts() const noexcept;
    core::EditingMiniGameInput compute_editing_from_timeline() const noexcept;

    core::VideoReleaseInput build_current_release() const;
    void start_live_release();
    void run_instant_release();
    void on_release_finished();
    void run_100k_benchmark();
    void notify_toast(std::string msg);

    std::string autosave_path() const;

    core::Engine engine_;
    cloud::WorldSyncService world_;
    audio::NativeSoundSynth sound_;
    ui::Renderer2D renderer_;
    ui::UiContext ui_;

    std::string storage_dir_;
    GameTab current_tab_ = GameTab::Studio;
    StudioStep studio_step_ = StudioStep::Concept;
    bool show_retention_chart_ = false;
    bool editing_variant_b_ = false;

    // Параметры готовящегося ролика
    core::NicheId selected_niche_ = core::NicheId::Tech;
    int selected_title_preset_ = 0;
    bool use_ab_test_ = true;
    int selected_trend_idx_ = -1; // -1 = авторская тема
    int sponsor_mode_ = 0;        // 0 = нет, 1 = белый бренд, 2 = скам-токен
    int ad_slot_pos_ = 1;         // 0 = в начале (15%), 1 = середина (48%), 2 = к финалу (78%)
    float budget_usd_ = 120.0f;

    // Состояние мини-игры таймлайна монтажа (8 клипов)
    std::array<TimelineClip, 8> clips_{};

    // Состояние визуального конструктора обложек A и B
    ThumbnailDesign thumb_a_{0, 0, 1, 0.14f};
    ThumbnailDesign thumb_b_{1, 1, 2, 0.18f};

    int live_speed_mult_ = 1; // 1x или 4x скорость прокрутки 48 часов
    float live_hour_timer_ = 0.0f;
    float anim_time_sec_ = 0.0f;
    float content_height_ = 0.0f;
    bool confirm_reset_career_ = false;

    core::SpringValue anim_subs_{0.0f, 180.0f, 24.0f};
    core::SpringValue anim_cash_{750.0f, 180.0f, 24.0f};

    cloud::WorldPoolStatus pool_status_{};
    std::vector<cloud::LeaderboardEntry> leaderboard_;
    std::string toast_text_ru_;
    float toast_timer_ = 0.0f;
    std::string bench_result_ru_;
};

} // namespace simul::game
