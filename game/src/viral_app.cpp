#include "viral_app.hpp"
#include "simul/core/binary_save.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace simul::game {

namespace {

constexpr const char* kTitlePresetsA[core::kNicheCount][3] = {
    {"Соло-рейд без брони на хардкоре", "100 дней выживания в проклятом мире", "Секретный билд нового патча"},
    {"Тест алгоритма ленты на 50 000 зрителей", "Жёсткий краш-тест нового флагмана", "Тихий игровой ПК за $400"},
    {"30 дней подъёма в 5 утра без кофе", "Месяц жизни в микро-студии 12 м2", "Неделя полного цифрового детокса"},
    {"Как алгоритмы управляют дофамином", "Физика невозможных вирусных трюков", "Анатомия пузыря на миллиард"},
    {"Прятки в огромном павильоне на $10 000", "Угадай профи среди пяти актёров", "Блюдо шефа за $5 против $500"},
};

constexpr const char* kTitlePresetsB[core::kNicheCount][3] = {
    {"Никто не верил, что босс упадёт!", "Финал на 1 HP: 100 дней хардкора", "Мета сломана? Разбор патча"},
    {"Что скрывает лента рекомендаций?", "Выдержит ли экран? Честный тест", "Лучший бюджетный ПК своими руками"},
    {"Что стало с мозгом через 30 дней в 5:00?", "Минимализм без прикрас: честный итог", "7 дней без смартфона: эксперимент"},
    {"Почему вы не можете закрыть ленту?", "Разоблачение вирусных видео физикой", "Почему любой хайп лопается?"},
    {"Последний найденный забирает $10 000!", "Кто из пяти — настоящий мастер?", "Слепая дегустация: $5 или $500?"},
};

std::string fmt_compact(std::int64_t v) {
    char buf[32];
    const double av = std::fabs(static_cast<double>(v));
    if (av >= 1'000'000.0) std::snprintf(buf, sizeof(buf), "%.2fM", static_cast<double>(v) / 1'000'000.0);
    else if (av >= 1'000.0) std::snprintf(buf, sizeof(buf), "%.1fK", static_cast<double>(v) / 1'000.0);
    else std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    return buf;
}

std::string fmt_money(double usd) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%.0f", usd);
    return buf;
}

} // namespace

ViralGameApp::ViralGameApp()
    : engine_(core::EngineConfig{
          20261029u,
          "@viral_creator",
          "ВИРАЛ Канал",
          core::NicheId::Tech,
          750.0,
          50'000,
          20'000}) {
    reset_timeline_clips();
}

void ViralGameApp::set_storage_directory(std::string dir) {
    storage_dir_ = std::move(dir);
}

void ViralGameApp::set_safe_area(float top_dp, float bottom_dp, float left_dp, float right_dp) noexcept {
    ui_.safe_area = {top_dp, bottom_dp, left_dp, right_dp};
}

std::string ViralGameApp::autosave_path() const {
    if (storage_dir_.empty()) {
        return "career_autosave.simsave";
    }
    if (storage_dir_.back() == '/' || storage_dir_.back() == '\\') {
        return storage_dir_ + "career_autosave.simsave";
    }
    return storage_dir_ + "/career_autosave.simsave";
}

void ViralGameApp::save_autosave() {
    std::string err;
    core::BinarySave::save_to_file(engine_, autosave_path(), err);
}

void ViralGameApp::notify_toast(std::string msg) {
    toast_text_ru_ = std::move(msg);
    toast_timer_ = 5.5f;
}

bool ViralGameApp::init() {
    sound_.init(44100);
    if (!renderer_.init()) {
        return false;
    }

    // Попытка автозагрузки сохранённой карьеры с телефона/ПК
    std::string load_err;
    const bool resumed = core::BinarySave::load_from_file(engine_, autosave_path(), load_err);
    selected_niche_ = engine_.creator_system.creator.current_niche;

    pool_status_ = world_.fetch_pool_status(selected_niche_);
    engine_.set_world_pool_multiplier(pool_status_.pool_multiplier);
    leaderboard_ = world_.leaderboard(
        selected_niche_,
        engine_.creator_system.creator.handle,
        engine_.creator_system.creator.display_name,
        engine_.creator_system.current_stage(),
        engine_.subscribers(),
        engine_.total_views());

    anim_subs_.value = static_cast<float>(engine_.subscribers());
    anim_subs_.target = anim_subs_.value;
    anim_cash_.value = static_cast<float>(engine_.creator_system.creator.cash_usd);
    anim_cash_.target = anim_cash_.value;

    if (resumed) {
        notify_toast("Карьера восстановлена (День " + std::to_string(engine_.clock.current_day()) + ")!");
    } else {
        notify_toast("Добро пожаловать в ВИРАЛ! Снимите, смонтируйте и упакуйте свой первый хит.");
    }
    return true;
}

void ViralGameApp::shutdown() {
    save_autosave();
    renderer_.shutdown();
    sound_.shutdown();
}

void ViralGameApp::on_pointer_down(float x, float y, bool is_touch) {
    ui_.pointer.x = x;
    ui_.pointer.y = y;
    ui_.pointer.press_start_x = x;
    ui_.pointer.press_start_y = y;
    ui_.pointer.down = true;
    ui_.pointer.just_pressed = true;
    ui_.pointer.is_scrolling = false;
    ui_.pointer.is_touch = is_touch;
}

void ViralGameApp::on_pointer_move(float x, float y, bool is_touch) {
    if (ui_.pointer.down) {
        ui_.pointer.drag_dy += (y - ui_.pointer.y);
    }
    ui_.pointer.x = x;
    ui_.pointer.y = y;
    ui_.pointer.is_touch = is_touch;
}

void ViralGameApp::on_pointer_up(float x, float y, bool is_touch) {
    ui_.pointer.x = x;
    ui_.pointer.y = y;
    ui_.pointer.down = false;
    ui_.pointer.just_released = true;
    ui_.pointer.is_touch = is_touch;
}

void ViralGameApp::on_wheel(float dy) {
    ui_.pointer.wheel_dy += dy;
}

void ViralGameApp::reset_timeline_clips() {
    clips_ = {{
        {"0:00 Хук (Первые 5 сек)", false, false, true,  0.88f},
        {"0:18 Долгая пауза и эканье", true,  false, false, 0.22f},
        {"1:10 Разгон главной темы",   false, false, false, 0.75f},
        {"3:05 Неудачный скучный дубль", true, false, false, 0.18f},
        {"5:20 Яркий эксперимент",     false, false, false, 0.84f},
        {"7:40 Повтор и лишняя вода",  true,  false, false, 0.25f},
        {"9:50 Главная кульминация!",  false, false, true,  0.92f},
        {"11:40 Финал и байт на коммент", false, false, false, 0.78f},
    }};
}

int ViralGameApp::used_timeline_boosts() const noexcept {
    int count = 0;
    for (const auto& c : clips_) {
        if (!c.is_filler && !c.cut && c.boosted) ++count;
    }
    return count;
}

int ViralGameApp::max_timeline_boosts() const noexcept {
    int bonus = 0;
    for (const auto& m : engine_.creator_system.members()) {
        if (m.role == core::TeamRole::Editor) ++bonus;
    }
    return 3 + bonus;
}

void ViralGameApp::auto_edit_timeline() {
    int boosts_left = max_timeline_boosts();
    for (auto& c : clips_) {
        if (c.is_filler) {
            c.cut = true;
            c.boosted = false;
        } else {
            c.cut = false;
            c.boosted = false;
        }
    }
    // Приоритетно усиливаем Хук (0), Кульминацию (6), Эксперимент (4) и Финал (7)
    for (std::size_t idx : {std::size_t{0}, std::size_t{6}, std::size_t{4}, std::size_t{7}}) {
        if (boosts_left > 0) {
            clips_[idx].boosted = true;
            --boosts_left;
        }
    }
}

core::EditingMiniGameInput ViralGameApp::compute_editing_from_timeline() const noexcept {
    int fillers_left = 0;
    int good_kept = 0;
    float energy_sum = 0.0f;

    for (std::size_t i = 0; i < clips_.size(); ++i) {
        const auto& c = clips_[i];
        if (c.cut) continue;
        if (c.is_filler) {
            ++fillers_left;
        } else {
            ++good_kept;
            energy_sum += c.base_energy + (c.boosted ? 0.14f : 0.0f);
        }
    }

    float hook = 0.35f;
    if (!clips_[0].cut) {
        hook = clips_[0].boosted ? 0.94f : 0.74f;
    }
    if (!clips_[1].cut) {
        hook -= 0.14f; // невырезанная пауза сразу после хука роняет старт!
    }

    const float avg_good = good_kept > 0 ? (energy_sum / static_cast<float>(good_kept)) : 0.30f;
    const float pacing = core::clamp_val(
        0.92f - static_cast<float>(fillers_left) * 0.19f - (good_kept < 4 ? 0.18f : 0.0f),
        0.18f, 0.97f);

    float payoff = 0.42f;
    if (!clips_[6].cut) payoff += clips_[6].boosted ? 0.38f : 0.24f;
    if (!clips_[7].cut) payoff += clips_[7].boosted ? 0.15f : 0.09f;

    static constexpr float kAdRatios[3] = {0.14f, 0.48f, 0.78f};
    const float ad_pos = sponsor_mode_ > 0 ? kAdRatios[std::clamp(ad_slot_pos_, 0, 2)] : -1.0f;

    return {
        core::clamp_val(hook, 0.18f, 0.98f),
        core::clamp_val((pacing + avg_good) * 0.5f, 0.18f, 0.98f),
        core::clamp_val(payoff, 0.18f, 0.98f),
        ad_pos,
    };
}

core::VideoReleaseInput ViralGameApp::build_current_release() const {
    const auto n_idx = static_cast<std::size_t>(selected_niche_);
    const int p_idx = selected_title_preset_ % 3;

    core::VideoReleaseInput in{};
    in.niche = selected_niche_;
    in.title = kTitlePresetsA[n_idx][p_idx];
    in.production_budget_usd = static_cast<double>(budget_usd_);
    in.publish_hour_of_day = 19;

    const auto& trs = engine_.trends.active_trends();
    if (selected_trend_idx_ >= 0 && static_cast<std::size_t>(selected_trend_idx_) < trs.size()) {
        in.trend_id = trs[static_cast<std::size_t>(selected_trend_idx_)].id;
    }

    if (sponsor_mode_ == 1) {
        in.sponsor = {true, "TechNova", 450.0, 0.08f};
    } else if (sponsor_mode_ == 2) {
        in.sponsor = {true, "MiracleToken", 1800.0, 0.86f};
    }

    in.editing = compute_editing_from_timeline();

    in.packaging.variant_a.id = 'A';
    in.packaging.variant_a.title = kTitlePresetsA[n_idx][p_idx];
    apply_thumbnail_design_to_variant(thumb_a_, selected_niche_, in.packaging.variant_a);

    in.packaging.has_variant_b = use_ab_test_;
    if (use_ab_test_) {
        in.packaging.variant_b.id = 'B';
        in.packaging.variant_b.title = kTitlePresetsB[n_idx][p_idx];
        apply_thumbnail_design_to_variant(thumb_b_, selected_niche_, in.packaging.variant_b);
    }

    in.delegated_to_team = !engine_.creator_system.members().empty();
    return in;
}

void ViralGameApp::on_release_finished() {
    sound_.play_cash();
    const auto& vid = engine_.published_videos().back();
    pool_status_ = world_.on_player_video_completed(vid, engine_.subscribers(), engine_.total_views());
    engine_.set_world_pool_multiplier(pool_status_.pool_multiplier);
    leaderboard_ = world_.leaderboard(
        selected_niche_,
        engine_.creator_system.creator.handle,
        engine_.creator_system.creator.display_name,
        engine_.creator_system.current_stage(),
        engine_.subscribers(),
        engine_.total_views());
    notify_toast(vid.post_mortem.verdict_title_ru);
    reset_timeline_clips();
    studio_step_ = StudioStep::Concept;
    save_autosave();
}

void ViralGameApp::start_live_release() {
    std::string err;
    if (!engine_.begin_video_release(build_current_release(), err)) {
        sound_.play_warning();
        notify_toast(err);
        return;
    }
    sound_.play_tap();
    live_hour_timer_ = 0.0f;
    current_tab_ = GameTab::Algorithm;
    ui_.scroll_y = 0.0f;
    notify_toast("Ролик в эфире! Первые 48 часов решают, пробьёт ли он волны рекомендаций.");
}

void ViralGameApp::run_instant_release() {
    std::string err;
    if (!engine_.publish_and_simulate_full_video(build_current_release(), err)) {
        sound_.play_warning();
        notify_toast(err);
        return;
    }
    current_tab_ = GameTab::Algorithm;
    ui_.scroll_y = 0.0f;
    on_release_finished();
}

void ViralGameApp::run_100k_benchmark() {
    sound_.play_tap();
    core::EngineConfig cfg{};
    cfg.seed = 777u;
    cfg.audience_capacity = 100'000;
    cfg.initial_platform_agents = 100'000;
    core::Engine bench(cfg);

    const auto t0 = std::chrono::steady_clock::now();
    std::string err;
    for (int i = 0; i < 10; ++i) {
        bench.publish_and_simulate_full_video(build_current_release(), err);
        bench.advance_days(3);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "100 000 живых SoA-агентов: 10 видео (480 волн) за %.1f мс (%.3f мс/волна)!",
                  ms, ms / 480.0);
    bench_result_ru_ = buf;
    sound_.play_cash();
}

void ViralGameApp::update_and_render(float dt_sec, float logical_w, float logical_h) {
    anim_time_sec_ += dt_sec;
    if (toast_timer_ > 0.0f) {
        toast_timer_ = std::max(0.0f, toast_timer_ - dt_sec);
    }

    // Почасовая симуляция активного релиза (поддерживает 1x и 4x скорость!)
    if (engine_.is_live_video_running()) {
        live_hour_timer_ += dt_sec * static_cast<float>(live_speed_mult_);
        while (live_hour_timer_ >= 0.065f && engine_.is_live_video_running()) {
            live_hour_timer_ -= 0.065f;
            const std::uint8_t prev_w = engine_.published_videos().back().max_wave_reached;
            const auto* pt = engine_.step_live_video_hour();
            if (pt && pt->wave_index > prev_w && pt->wave_index >= 2) {
                sound_.play_wave_breakthrough(pt->wave_index);
                renderer_.spawn_wave_burst(logical_w * 0.5f, ui_.safe_area.top + 190.0f, pt->wave_index);
            }
            if (!engine_.is_live_video_running()) {
                on_release_finished();
                break;
            }
        }
    }

    anim_subs_.set_target(static_cast<float>(engine_.subscribers()));
    anim_cash_.set_target(static_cast<float>(engine_.creator_system.creator.cash_usd));
    anim_subs_.step(dt_sec);
    anim_cash_.step(dt_sec);

    const float header_h = ui_.safe_area.top + 78.0f;
    const float nav_h = 66.0f + ui_.safe_area.bottom;
    const float scroll_top_bound = header_h;
    const float scroll_bot_bound = std::max(header_h + 80.0f, logical_h - nav_h);
    const float viewport_h = scroll_bot_bound - scroll_top_bound;
    const float max_scroll = std::max(0.0f, content_height_ - viewport_h + 24.0f);

    ui_.begin_input_frame(dt_sec, max_scroll, scroll_top_bound, scroll_bot_bound);
    renderer_.begin_frame(logical_w, logical_h);

    // Глубокий ночной фон приложения
    renderer_.draw_gradient_v(0.0f, 0.0f, logical_w, logical_h,
                              ui::Color::rgba(9, 14, 29),
                              ui::Color::rgba(5, 8, 18));

    // На смартфоне (< 560 dp) занимаем всю ширину с аккуратным отступом 12 dp; на ПК центрируем до 680 dp
    const float side_pad = logical_w < 540.0f ? 12.0f : 20.0f;
    const float col_w = std::min(logical_w - side_pad * 2.0f, 680.0f);
    const float col_x = (logical_w - col_w) * 0.5f;

    const float toast_extra = (toast_timer_ > 0.0f && !toast_text_ru_.empty())
                                  ? (renderer_.measure_text_wrapped(col_w - 24.0f, toast_text_ru_, 12.5f) + 22.0f)
                                  : 0.0f;
    const float start_y = scroll_top_bound + 10.0f + toast_extra - ui_.scroll_y;

    ui_.begin_scroll_region(scroll_top_bound, scroll_bot_bound);
    float end_y = start_y;
    switch (current_tab_) {
        case GameTab::Studio: end_y = draw_tab_studio(col_x, start_y, col_w); break;
        case GameTab::Algorithm: end_y = draw_tab_algorithm(col_x, start_y, col_w); break;
        case GameTab::Audience: end_y = draw_tab_audience(col_x, start_y, col_w); break;
        case GameTab::Career: end_y = draw_tab_career(col_x, start_y, col_w); break;
        case GameTab::World: end_y = draw_tab_world(col_x, start_y, col_w); break;
    }
    ui_.end_scroll_region();

    content_height_ = (end_y - start_y) + toast_extra + 14.0f;

    // Всплывающий тост-баннер (под липкой шапкой)
    if (toast_timer_ > 0.0f && !toast_text_ru_.empty()) {
        const float th = renderer_.measure_text_wrapped(col_w - 24.0f, toast_text_ru_, 12.5f) + 14.0f;
        const float ty = scroll_top_bound + 6.0f;
        renderer_.draw_rounded_rect(col_x, ty, col_w, th, 10.0f, ui::Color::rgba(19, 34, 68, 242));
        renderer_.draw_rect_outline(col_x, ty, col_w, th, 1.0f, ui::Color::rgba(56, 189, 248, 120));
        renderer_.draw_text_wrapped(col_x + 12.0f, ty + 7.0f, col_w - 24.0f,
                                    toast_text_ru_, 12.5f, ui::Color::rgba(224, 242, 254));
    }

    // Липкая шапка HUD (с отступом под камеру телефона) и нижний таб-бар (с отступом под жестовую полосу)
    draw_hud_header(0.0f, 0.0f, logical_w);
    draw_bottom_nav(0.0f, logical_h - nav_h, logical_w, nav_h);

    renderer_.step_and_draw_particles(dt_sec);
    renderer_.flush_gpu_batch();
    ui_.end_input_frame();
}

float ViralGameApp::draw_hud_header(float x, float y, float w) {
    const float h = ui_.safe_area.top + 78.0f;
    renderer_.draw_rect(x, y, w, h, ui::Color::rgba(11, 18, 36, 250));
    renderer_.draw_rect(x, y + h - 1.0f, w, 1.0f, ui::Color::rgba(56, 189, 248, 70));

    const float side_pad = w < 540.0f ? 12.0f : 20.0f;
    const float cw = std::min(w - side_pad * 2.0f, 680.0f);
    const float cx = (w - cw) * 0.5f;
    const float ty = y + ui_.safe_area.top + 6.0f;

    // Строка 1: Подписчики + Касса + День + Кнопка Звука
    const std::string subs_str = fmt_compact(static_cast<std::int64_t>(std::llround(anim_subs_.value))) + " подп.";
    const std::string cash_str = fmt_money(static_cast<double>(anim_cash_.value));
    renderer_.draw_text(cx, ty + 2.0f, subs_str, 17.5f, ui::Color::rgba(248, 250, 252));

    const float sub_w = renderer_.measure_text(subs_str, 17.5f);
    renderer_.draw_text(cx + sub_w + 12.0f, ty + 3.0f, cash_str, 16.5f, ui::Color::rgba(74, 222, 128));

    const std::string sound_lbl = sound_.is_muted() ? "Звук: ВЫКЛ" : "Звук: ВКЛ";
    if (ui_.chip_button(renderer_, cx + cw - 88.0f, ty, 88.0f, 30.0f, sound_lbl, !sound_.is_muted())) {
        sound_.set_muted(!sound_.is_muted());
    }

    // Строка 2: Визуальные полоски Энергии и Траста алгоритма + День
    const float r2y = ty + 38.0f;
    const float energy = core::clamp_val(engine_.creator_system.creator.energy / 100.0f, 0.0f, 1.0f);
    const float trust = core::clamp_val(engine_.recommender.trust_score(), 0.0f, 1.0f);
    const bool shadow = engine_.recommender.is_shadowbanned();

    const float gauge_w = (cw - 86.0f) * 0.5f;
    // Полоска энергии
    char e_lbl[48];
    std::snprintf(e_lbl, sizeof(e_lbl), "Энергия %.0f%%", static_cast<double>(energy * 100.0f));
    renderer_.draw_text(cx, r2y, e_lbl, 11.5f, ui::Color::rgba(203, 213, 225));
    renderer_.draw_rounded_rect(cx, r2y + 16.0f, gauge_w, 7.0f, 3.5f, ui::Color::rgba(30, 41, 59));
    const ui::Color e_col = energy > 0.45f ? ui::Color::rgba(34, 197, 94)
                                           : (energy > 0.22f ? ui::Color::rgba(250, 204, 21) : ui::Color::rgba(244, 63, 94));
    renderer_.draw_rounded_rect(cx, r2y + 16.0f, gauge_w * energy, 7.0f, 3.5f, e_col);

    // Полоска траста алгоритма
    const float tx = cx + gauge_w + 10.0f;
    char t_lbl[64];
    std::snprintf(t_lbl, sizeof(t_lbl), shadow ? "ШАДОУБАН! (%.2f)" : "Траст %.2f", static_cast<double>(trust));
    renderer_.draw_text(tx, r2y, t_lbl, 11.5f, shadow ? ui::Color::rgba(244, 63, 94) : ui::Color::rgba(125, 211, 252));
    renderer_.draw_rounded_rect(tx, r2y + 16.0f, gauge_w, 7.0f, 3.5f, ui::Color::rgba(30, 41, 59));
    renderer_.draw_rounded_rect(tx, r2y + 16.0f, gauge_w * trust, 7.0f, 3.5f,
                                shadow ? ui::Color::rgba(244, 63, 94) : ui::Color::rgba(56, 189, 248));

    // День справа
    const std::string day_str = "День " + std::to_string(engine_.clock.current_day());
    const float dw = renderer_.measure_text(day_str, 12.5f);
    renderer_.draw_text(cx + cw - dw, r2y + 6.0f, day_str, 12.5f, ui::Color::rgba(148, 163, 184));
    return h;
}

float ViralGameApp::draw_tab_studio(float x, float y, float w) {
    float cy = y;

    // 1. Живая иллюстрированная комната-студия блогера!
    draw_creator_studio_room(
        renderer_, x, cy, w, 142.0f,
        anim_time_sec_,
        static_cast<std::uint8_t>(engine_.creator_system.creator.equipment_tier),
        engine_.creator_system.members().size(),
        engine_.creator_system.creator.energy,
        engine_.is_live_video_running());
    cy += 152.0f;

    // 2. Переключатель 3 шагов создания ролика под большой палец
    const float step_w = (w - 12.0f) / 3.0f;
    static constexpr const char* kSteps[3] = {"1. Идея", "2. Монтаж", "3. Обложка"};
    for (int i = 0; i < 3; ++i) {
        const auto st = static_cast<StudioStep>(i);
        if (ui_.chip_button(renderer_, x + static_cast<float>(i) * (step_w + 6.0f), cy, step_w, 40.0f,
                            kSteps[i], studio_step_ == st)) {
            sound_.play_tap();
            studio_step_ = st;
        }
    }
    cy += 50.0f;

    switch (studio_step_) {
        case StudioStep::Concept: return draw_studio_step_concept(x, cy, w);
        case StudioStep::Timeline: return draw_studio_step_timeline(x, cy, w);
        case StudioStep::Thumbnail: return draw_studio_step_thumbnail(x, cy, w);
    }
    return cy;
}

float ViralGameApp::draw_studio_step_concept(float x, float y, float w) {
    float cy = y;
    renderer_.draw_text(x + 2.0f, cy, "Выберите нишу и тему выпуска:", 14.5f, ui::Color::rgba(248, 250, 252));
    cy += 24.0f;

    // Сетка 5 ниш удобными тач-чипами (2 в ряд + 1)
    const float half_w = (w - 8.0f) * 0.5f;
    for (std::size_t i = 0; i < core::kNicheCount; ++i) {
        const auto nid = static_cast<core::NicheId>(i);
        const float bx = (i % 2 == 0) ? x : (x + half_w + 8.0f);
        const float bw = (i == core::kNicheCount - 1) ? w : half_w;
        if (ui_.chip_button(renderer_, bx, cy, bw, 40.0f,
                            core::niche_spec(nid).title_ru, selected_niche_ == nid)) {
            sound_.play_tap();
            selected_niche_ = nid;
            pool_status_ = world_.fetch_pool_status(selected_niche_);
            engine_.set_world_pool_multiplier(pool_status_.pool_multiplier);
        }
        if (i % 2 == 1 || i == core::kNicheCount - 1) {
            cy += 46.0f;
        }
    }
    cy += 4.0f;

    // Карточка идеи заголовка
    const auto preview = build_current_release();
    renderer_.draw_rounded_rect(x, cy, w, 78.0f, 12.0f, ui::Color::rgba(15, 23, 44));
    renderer_.draw_rect_outline(x, cy, w, 78.0f, 1.0f, ui::Color::rgba(56, 189, 248, 55));
    renderer_.draw_text_fit(x + 12.0f, cy + 10.0f, w - 24.0f,
                            "Тема: «" + preview.packaging.variant_a.title + "»",
                            13.5f, ui::Color::rgba(248, 250, 252));
    if (ui_.button(renderer_, x + 12.0f, cy + 36.0f, w - 24.0f, 34.0f, "Другая идея ролика")) {
        sound_.play_tap();
        selected_title_preset_ = (selected_title_preset_ + 1) % 3;
    }
    cy += 88.0f;

    // Волна тренда + Бюджет продакшна
    const auto& trs = engine_.trends.active_trends();
    std::string trend_lbl = "Тренд: Авторская вечнозелёная тема (x1.00)";
    if (selected_trend_idx_ >= 0 && static_cast<std::size_t>(selected_trend_idx_) < trs.size()) {
        const auto& t = trs[static_cast<std::size_t>(selected_trend_idx_)];
        char tb[64];
        std::snprintf(tb, sizeof(tb), " (x%.2f)", static_cast<double>(t.virality_boost));
        trend_lbl = std::string("Тренд: ") + t.title_ru + " • " + core::trend_phase_label_ru(t.phase) + tb;
    }
    if (ui_.button(renderer_, x, cy, w, 44.0f, trend_lbl)) {
        sound_.play_tap();
        selected_trend_idx_ += 1;
        if (static_cast<std::size_t>(selected_trend_idx_) >= trs.size()) selected_trend_idx_ = -1;
    }
    cy += 54.0f;

    ui_.slider_float(renderer_, 10, x + 4.0f, cy, w - 8.0f,
                     "Бюджет съёмки и реквизита", budget_usd_, 0.0f, 500.0f);
    cy += 48.0f;

    // Спонсорские контракты (крупные карточки)
    renderer_.draw_text(x + 2.0f, cy, "Рекламная интеграция:", 14.0f, ui::Color::rgba(226, 232, 240));
    cy += 22.0f;

    if (ui_.card_option(renderer_, x, cy, w, 54.0f,
                        "Без рекламы", "Максимальное удержание зрителей и чистый траст канала",
                        sponsor_mode_ == 0, ui::Color::rgba(56, 189, 248))) {
        sound_.play_tap();
        sponsor_mode_ = 0;
    }
    cy += 60.0f;

    if (ui_.card_option(renderer_, x, cy, w, 54.0f,
                        "Белый бренд TechNova (+$450)", "Умеренная вставка проверенного гаджета (низкий риск)",
                        sponsor_mode_ == 1, ui::Color::rgba(34, 197, 94))) {
        sound_.play_tap();
        sponsor_mode_ = 1;
    }
    cy += 60.0f;

    if (ui_.card_option(renderer_, x, cy, w, 54.0f,
                        "Токен MiracleCoin (+$1800 • РИСК!)", "Огромный гонорар сейчас, но удар по доверию алгоритма и аудитории!",
                        sponsor_mode_ == 2, ui::Color::rgba(244, 63, 94))) {
        sound_.play_tap();
        sponsor_mode_ = 2;
    }
    cy += 66.0f;

    if (ui_.button(renderer_, x, cy, w, 50.0f, "ДАЛЕЕ: ТАЙМЛАЙН МОНТАЖА →", true)) {
        sound_.play_tap();
        studio_step_ = StudioStep::Timeline;
        ui_.scroll_y = 0.0f;
    }
    return cy + 58.0f;
}

float ViralGameApp::draw_studio_step_timeline(float x, float y, float w) {
    float cy = y;
    const auto ed = compute_editing_from_timeline();
    const int used_b = used_timeline_boosts();
    const int max_b = max_timeline_boosts();

    // Шапка мини-игры с живыми метриками монтажа
    renderer_.draw_rounded_rect(x, cy, w, 84.0f, 12.0f, ui::Color::rgba(15, 23, 44));
    renderer_.draw_rect_outline(x, cy, w, 84.0f, 1.0f, ui::Color::rgba(56, 189, 248, 60));

    char m_hdr[128];
    std::snprintf(m_hdr, sizeof(m_hdr), "Хук: %.0f%%   Темп: %.0f%%   Финал: %.0f%%   Бусты: %d/%d",
                  static_cast<double>(ed.hook_strength * 100.0f),
                  static_cast<double>(ed.pacing_tightness * 100.0f),
                  static_cast<double>(ed.payoff_strength * 100.0f),
                  used_b, max_b);
    renderer_.draw_text_fit(x + 12.0f, cy + 10.0f, w - 24.0f, m_hdr, 13.5f, ui::Color::rgba(56, 189, 248));
    renderer_.draw_text_wrapped(
        x + 12.0f, cy + 32.0f, w - 24.0f,
        "Нажимайте на красные дубли с «водой», чтобы ВЫРЕЗАТЬ их, и усиливайте ключевые сцены звуком/зумом (⚡)!",
        12.0f, ui::Color::rgba(186, 199, 218));
    cy += 92.0f;

    // Быстрые кнопки помощника монтажа
    const float half_w = (w - 8.0f) * 0.5f;
    if (ui_.button(renderer_, x, cy, half_w, 38.0f, "Авто-монтаж")) {
        sound_.play_cash();
        auto_edit_timeline();
        notify_toast("Лишние паузы вырезаны, ключевые сцены усилены!");
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 38.0f, "Сбросить дорожку")) {
        sound_.play_tap();
        reset_timeline_clips();
    }
    cy += 46.0f;

    // 8 интерактивных клипов таймлайна
    for (std::size_t i = 0; i < clips_.size(); ++i) {
        auto& clip = clips_[i];
        const bool clicked = ui_.hit_rect(x, cy, w, 48.0f) &&
                             ui_.pointer.just_released && !ui_.pointer.is_scrolling;

        ui::Color bg = clip.cut
                           ? ui::Color::rgba(15, 20, 34, 150)
                           : (clip.is_filler
                                  ? ui::Color::rgba(69, 20, 31)
                                  : (clip.boosted ? ui::Color::rgba(20, 56, 96) : ui::Color::rgba(19, 30, 56)));
        ui::Color border = clip.cut
                               ? ui::Color::rgba(71, 85, 105, 90)
                               : (clip.is_filler
                                      ? ui::Color::rgba(244, 63, 94)
                                      : (clip.boosted ? ui::Color::rgba(56, 189, 248) : ui::Color::rgba(100, 116, 139, 140)));

        renderer_.draw_rounded_rect(x, cy, w, 48.0f, 10.0f, bg);
        renderer_.draw_rect_outline(x, cy, w, 48.0f, clip.boosted ? 1.6f : 1.0f, border);

        // Мини-волна звука слева
        for (int b = 0; b < 5; ++b) {
            const float bh = clip.cut ? 4.0f : (clip.is_filler ? 7.0f : (10.0f + static_cast<float>((b * 7 + i * 3) % 16)));
            const float bx = x + 10.0f + static_cast<float>(b) * 5.0f;
            renderer_.draw_rect(bx, cy + 24.0f - bh * 0.5f, 3.0f, bh,
                                clip.cut ? ui::Color::rgba(71, 85, 105) : border);
        }

        std::string status_tag;
        ui::Color tag_col = ui::Color::rgba(203, 213, 225);
        if (clip.cut) {
            status_tag = "ВЫРЕЗАНО";
            tag_col = ui::Color::rgba(148, 163, 184);
        } else if (clip.is_filler) {
            status_tag = "ВОДА • Нажмите, чтобы вырезать";
            tag_col = ui::Color::rgba(251, 113, 133);
        } else if (clip.boosted) {
            status_tag = "⚡ SFX + ЗУМ АКТИВЕН";
            tag_col = ui::Color::rgba(56, 189, 248);
        } else {
            status_tag = "Нажмите для буста ⚡";
            tag_col = ui::Color::rgba(148, 163, 184);
        }

        renderer_.draw_text_fit(x + 42.0f, cy + 7.0f, w - 54.0f, clip.name_ru, 13.5f,
                                clip.cut ? ui::Color::rgba(100, 116, 139) : ui::Color::rgba(248, 250, 252));
        renderer_.draw_text_fit(x + 42.0f, cy + 26.0f, w - 54.0f, status_tag, 11.5f, tag_col);

        if (clicked) {
            if (clip.is_filler) {
                clip.cut = !clip.cut;
                sound_.play_tap();
            } else {
                if (clip.boosted) {
                    clip.boosted = false;
                    sound_.play_tap();
                } else if (used_timeline_boosts() < max_b) {
                    clip.boosted = true;
                    sound_.play_cash();
                } else {
                    sound_.play_warning();
                    notify_toast("Лимит бустов монтажа исчерпан! Наймите монтажёра в «Карьере» для +1 буста.");
                }
            }
        }
        cy += 54.0f;
    }

    // Выбор места рекламной вставки, если выбран спонсор
    if (sponsor_mode_ > 0) {
        cy += 4.0f;
        renderer_.draw_text(x + 2.0f, cy, "Куда вклеить рекламную интеграцию:", 13.5f, ui::Color::rgba(250, 204, 21));
        cy += 22.0f;
        const float third_w = (w - 12.0f) / 3.0f;
        static constexpr const char* kAdSlots[3] = {"В начало", "В середину", "К финалу"};
        for (int s = 0; s < 3; ++s) {
            if (ui_.chip_button(renderer_, x + static_cast<float>(s) * (third_w + 6.0f), cy, third_w, 38.0f,
                                kAdSlots[s], ad_slot_pos_ == s, ui::Color::rgba(250, 204, 21))) {
                sound_.play_tap();
                ad_slot_pos_ = s;
            }
        }
        cy += 46.0f;
    }

    cy += 6.0f;
    if (ui_.button(renderer_, x, cy, w, 50.0f, "ДАЛЕЕ: КОНСТРУКТОР ОБЛОЖКИ →", true)) {
        sound_.play_tap();
        studio_step_ = StudioStep::Thumbnail;
        ui_.scroll_y = 0.0f;
    }
    return cy + 58.0f;
}

float ViralGameApp::draw_studio_step_thumbnail(float x, float y, float w) {
    float cy = y;
    const auto skills = engine_.creator_system.effective_skills();
    const auto release_preview = build_current_release();

    // Переключатель A/B теста и редактируемого варианта
    const float third_w = (w - 12.0f) / 3.0f;
    if (ui_.chip_button(renderer_, x, cy, third_w, 38.0f, "Обложка А", !editing_variant_b_)) {
        sound_.play_tap();
        editing_variant_b_ = false;
    }
    if (ui_.chip_button(renderer_, x + third_w + 6.0f, cy, third_w, 38.0f, "Обложка B",
                        editing_variant_b_ && use_ab_test_, ui::Color::rgba(168, 85, 247))) {
        sound_.play_tap();
        use_ab_test_ = true;
        editing_variant_b_ = true;
    }
    if (ui_.chip_button(renderer_, x + (third_w + 6.0f) * 2.0f, cy, third_w, 38.0f,
                        use_ab_test_ ? "A/B: ВКЛ" : "A/B: ВЫКЛ", use_ab_test_, ui::Color::rgba(34, 197, 94))) {
        sound_.play_tap();
        use_ab_test_ = !use_ab_test_;
        if (!use_ab_test_) editing_variant_b_ = false;
    }
    cy += 46.0f;

    ThumbnailDesign& active_design = (editing_variant_b_ && use_ab_test_) ? thumb_b_ : thumb_a_;
    const core::PackagingVariant& active_var =
        (editing_variant_b_ && use_ab_test_) ? release_preview.packaging.variant_b : release_preview.packaging.variant_a;
    const auto pack_eval = core::evaluate_packaging_variant(active_var, selected_niche_, skills.analytics);

    // Живой визуальный холст превью 16:9 + предпросмотр карточки в мобильной ленте!
    draw_thumbnail_canvas(
        renderer_, x, cy, w,
        active_design,
        active_var.title,
        engine_.creator_system.creator.display_name,
        active_var.id,
        pack_eval.predicted_ctr);
    cy += std::round(w * 0.50f) + 68.0f;

    // Кнопки кастомизации обложки
    const float half_w = (w - 8.0f) * 0.5f;
    static constexpr const char* kPaletteNames[4] = {"Фон: Неон", "Фон: Огонь", "Фон: Изумруд", "Фон: Золото"};
    static constexpr const char* kFocalNames[4] = {"Кадр: Эмоция", "Кадр: VS Битва", "Кадр: Стрелка", "Кадр: График"};
    static constexpr const char* kStickerNames[4] = {"Плашка: Нет", "Плашка: ПРОВЕРКА!", "Плашка: БЕЗ ШАНСОВ", "Плашка: ШОК ФИНАЛ"};

    if (ui_.button(renderer_, x, cy, half_w, 42.0f, kPaletteNames[active_design.palette_idx % 4])) {
        sound_.play_tap();
        active_design.palette_idx = (active_design.palette_idx + 1) % 4;
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 42.0f, kFocalNames[active_design.focal_idx % 4])) {
        sound_.play_tap();
        active_design.focal_idx = (active_design.focal_idx + 1) % 4;
    }
    cy += 50.0f;

    if (ui_.button(renderer_, x, cy, w, 42.0f, kStickerNames[active_design.sticker_idx % 4])) {
        sound_.play_tap();
        active_design.sticker_idx = (active_design.sticker_idx + 1) % 4;
    }
    cy += 52.0f;

    ui_.slider_float(renderer_, 20, x + 4.0f, cy, w - 8.0f,
                     "Кликбейтность заголовка (выше риск потери траста)", active_design.clickbait, 0.0f, 0.90f);
    cy += 52.0f;

    // Крупные кнопки запуска релиза!
    const bool busy = engine_.is_live_video_running();
    if (ui_.button(renderer_, x, cy, w, 52.0f,
                   busy ? "РОЛИК УЖЕ НАБИРАЕТ ПРОСМОТРЫ..." : "ОПУБЛИКОВАТЬ В ЛЕНТУ (48Ч ВОЛНЫ)",
                   true, busy)) {
        start_live_release();
    }
    cy += 60.0f;

    if (ui_.button(renderer_, x, cy, w, 42.0f, "Быстрый расчёт без ожидания (Мгновенно)", false, busy)) {
        run_instant_release();
    }
    return cy + 52.0f;
}

float ViralGameApp::draw_tab_algorithm(float x, float y, float w) {
    float cy = y;
    const auto& vids = engine_.published_videos();
    const core::PublishedVideoRecord* latest = vids.empty() ? nullptr : &vids.back();

    // Переключатель графика и скорости прокрутки волн
    const float half_w = (w - 8.0f) * 0.5f;
    if (ui_.chip_button(renderer_, x, cy, half_w, 38.0f, "Волны алгоритма (48ч)", !show_retention_chart_)) {
        sound_.play_tap();
        show_retention_chart_ = false;
    }
    if (ui_.chip_button(renderer_, x + half_w + 8.0f, cy, half_w, 38.0f, "Кривая удержания %", show_retention_chart_)) {
        sound_.play_tap();
        show_retention_chart_ = true;
    }
    cy += 46.0f;

    if (engine_.is_live_video_running()) {
        const float third_w = (w - 12.0f) / 3.0f;
        if (ui_.chip_button(renderer_, x, cy, third_w, 36.0f, "Скорость 1x", live_speed_mult_ == 1)) {
            sound_.play_tap();
            live_speed_mult_ = 1;
        }
        if (ui_.chip_button(renderer_, x + third_w + 6.0f, cy, third_w, 36.0f, "Ускорить 4x", live_speed_mult_ == 4)) {
            sound_.play_tap();
            live_speed_mult_ = 4;
        }
        if (ui_.button(renderer_, x + (third_w + 6.0f) * 2.0f, cy, third_w, 36.0f, "До конца")) {
            while (engine_.is_live_video_running()) {
                engine_.step_live_video_hour();
            }
            on_release_finished();
        }
        cy += 44.0f;
    }

    std::vector<core::VideoWavePoint> empty_tl;
    std::array<float, 10> empty_ret{};
    renderer_.draw_wave_telemetry(
        x, cy, w, 224.0f,
        latest ? latest->hourly_timeline : empty_tl,
        latest ? latest->retention_curve : empty_ret,
        show_retention_chart_);
    cy += 236.0f;

    if (!latest) {
        if (ui_.button(renderer_, x, cy, w, 48.0f, "Перейти в «Студию» и выпустить первый ролик", true)) {
            sound_.play_tap();
            current_tab_ = GameTab::Studio;
        }
        return cy + 56.0f;
    }

    // Мобильная сетка KPI 2x2
    const float card_w = (w - 8.0f) * 0.5f;
    const auto draw_kpi_box = [&](float bx, float by, const char* label, const std::string& val, ui::Color vc) {
        renderer_.draw_rounded_rect(bx, by, card_w, 58.0f, 11.0f, ui::Color::rgba(15, 23, 44));
        renderer_.draw_rect_outline(bx, by, card_w, 58.0f, 1.0f, ui::Color::rgba(51, 65, 85, 110));
        renderer_.draw_text(bx + 12.0f, by + 8.0f, label, 11.5f, ui::Color::rgba(148, 163, 184));
        renderer_.draw_text_fit(bx + 12.0f, by + 27.0f, card_w - 24.0f, val, 16.5f, vc);
    };

    draw_kpi_box(x, cy, "Просмотры ролика", fmt_compact(latest->total_views), ui::Color::rgba(248, 250, 252));
    draw_kpi_box(x + card_w + 8.0f, cy, "Новые подписчики",
                 (latest->net_subscribers >= 0 ? "+" : "") + fmt_compact(latest->net_subscribers),
                 latest->net_subscribers >= 0 ? ui::Color::rgba(56, 189, 248) : ui::Color::rgba(244, 63, 94));
    cy += 66.0f;

    char ctr_ret[64];
    std::snprintf(ctr_ret, sizeof(ctr_ret), "%.1f%% / %.0f%% (В.%u)",
                  static_cast<double>(latest->avg_ctr * 100.0f),
                  static_cast<double>(latest->avg_retention * 100.0f),
                  static_cast<unsigned>(latest->max_wave_reached));
    char rev_str[64];
    std::snprintf(rev_str, sizeof(rev_str), "+$%.1f", latest->adsense_revenue_usd + latest->sponsor_revenue_usd);

    draw_kpi_box(x, cy, "CTR / Удержание / Волна", ctr_ret, ui::Color::rgba(250, 204, 21));
    draw_kpi_box(x + card_w + 8.0f, cy, "Заработано с видео", rev_str, ui::Color::rgba(74, 222, 128));
    cy += 70.0f;

    // Пост-мортем разбор алгоритма с ДИНАМИЧЕСКОЙ высотой карточек под узкие экраны телефонов!
    if (latest->post_mortem.ready) {
        const float verdict_h = renderer_.measure_text_wrapped(w - 24.0f, latest->post_mortem.verdict_title_ru, 15.0f);
        const float sum_h = renderer_.measure_text_wrapped(w - 24.0f, latest->post_mortem.summary_ru, 12.5f);
        const float head_card_h = verdict_h + sum_h + 26.0f;

        renderer_.draw_rounded_rect(x, cy, w, head_card_h, 12.0f, ui::Color::rgba(19, 29, 56));
        renderer_.draw_rect_outline(x, cy, w, head_card_h, 1.0f, ui::Color::rgba(250, 204, 21, 90));
        renderer_.draw_text_wrapped(x + 12.0f, cy + 10.0f, w - 24.0f,
                                    latest->post_mortem.verdict_title_ru, 15.0f, ui::Color::rgba(250, 204, 21));
        renderer_.draw_text_wrapped(x + 12.0f, cy + 14.0f + verdict_h, w - 24.0f,
                                    latest->post_mortem.summary_ru, 12.5f, ui::Color::rgba(226, 232, 240));
        cy += head_card_h + 10.0f;

        for (const auto& f : latest->post_mortem.factors) {
            const ui::Color bar_c = f.kind == core::FactorKind::Positive
                                        ? ui::Color::rgba(34, 197, 94)
                                        : (f.kind == core::FactorKind::Negative
                                               ? ui::Color::rgba(244, 63, 94)
                                               : ui::Color::rgba(56, 189, 248));
            const float det_h = renderer_.measure_text_wrapped(w - 28.0f, f.detail_ru, 12.5f);
            const float fh = 34.0f + det_h + 10.0f;

            renderer_.draw_rounded_rect(x, cy, w, fh, 10.0f, ui::Color::rgba(15, 23, 44));
            renderer_.draw_rect(x, cy + 6.0f, 4.0f, fh - 12.0f, bar_c);
            renderer_.draw_text_fit(x + 14.0f, cy + 8.0f, w - 26.0f, f.label_ru, 13.5f, bar_c);
            renderer_.draw_text_wrapped(x + 14.0f, cy + 29.0f, w - 28.0f, f.detail_ru,
                                        12.5f, ui::Color::rgba(203, 213, 225));
            cy += fh + 8.0f;
        }
    }
    return cy;
}

float ViralGameApp::draw_tab_audience(float x, float y, float w) {
    float cy = y;
    const auto sum = engine_.audience.summarize();

    renderer_.draw_rounded_rect(x, cy, w, 250.0f, 14.0f, ui::Color::rgba(15, 23, 44));
    renderer_.draw_rect_outline(x, cy, w, 250.0f, 1.0f, ui::Color::rgba(56, 189, 248, 55));

    char hdr[128];
    std::snprintf(hdr, sizeof(hdr), "Живая аудитория (%zu SoA-агентов)", sum.active_agents);
    renderer_.draw_text_fit(x + 14.0f, cy + 12.0f, w - 28.0f, hdr, 15.0f, ui::Color::rgba(248, 250, 252));

    char psych[160];
    std::snprintf(psych, sizeof(psych),
                  "Лояльность %.0f%% • Усталость %.0f%% • Ностальгия %.0f%%",
                  static_cast<double>(sum.avg_loyalty * 100.0f),
                  static_cast<double>(sum.avg_fatigue * 100.0f),
                  static_cast<double>(sum.avg_nostalgia * 100.0f));
    renderer_.draw_text_fit(x + 14.0f, cy + 36.0f, w - 28.0f, psych, 12.5f, ui::Color::rgba(56, 189, 248));

    static constexpr ui::Color kSegCols[core::kSegmentCount] = {
        ui::Color::rgba(56, 189, 248),
        ui::Color::rgba(129, 140, 248),
        ui::Color::rgba(250, 204, 21),
        ui::Color::rgba(244, 63, 94),
        ui::Color::rgba(74, 222, 128),
    };

    float sy = cy + 64.0f;
    const float total_a = static_cast<float>(std::max<std::size_t>(1, sum.active_agents));
    for (std::size_t s = 0; s < core::kSegmentCount; ++s) {
        const auto seg = static_cast<core::ViewerSegment>(s);
        const float share = static_cast<float>(sum.segment_counts[s]) / total_a;
        renderer_.draw_text(x + 14.0f, sy, core::segment_label_ru(seg), 12.5f, ui::Color::rgba(226, 232, 240));
        const std::string cnt = fmt_compact(static_cast<std::int64_t>(sum.segment_counts[s]));
        renderer_.draw_text(x + w - 14.0f - renderer_.measure_text(cnt, 12.5f), sy, cnt, 12.5f, kSegCols[s]);

        renderer_.draw_rounded_rect(x + 14.0f, sy + 18.0f, w - 28.0f, 7.0f, 3.5f, ui::Color::rgba(30, 41, 59));
        renderer_.draw_rounded_rect(x + 14.0f, sy + 18.0f, (w - 28.0f) * share, 7.0f, 3.5f, kSegCols[s]);
        sy += 34.0f;
    }
    cy += 262.0f;

    renderer_.draw_text(x + 4.0f, cy, "Свежие комментарии под видео:", 15.0f, ui::Color::rgba(248, 250, 252));
    cy += 26.0f;

    const auto& vids = engine_.published_videos();
    if (vids.empty() || vids.back().top_comments.empty()) {
        renderer_.draw_text_wrapped(x + 4.0f, cy, w - 8.0f,
                                    "Опубликуйте видео во вкладке «Студия», чтобы увидеть живую реакцию разных сегментов вашей аудитории.",
                                    13.0f, ui::Color::rgba(148, 163, 184));
        return cy + 44.0f;
    }

    for (const auto& c : vids.back().top_comments) {
        const float text_h = renderer_.measure_text_wrapped(w - 26.0f, c.text, 13.0f);
        const float ch = 34.0f + text_h + 10.0f;

        renderer_.draw_rounded_rect(x, cy, w, ch, 11.0f, ui::Color::rgba(15, 23, 44));
        renderer_.draw_rect_outline(x, cy, w, ch, 1.0f,
                                    c.is_toxic ? ui::Color::rgba(244, 63, 94, 70) : ui::Color::rgba(51, 65, 85, 110));
        const std::string head = c.author_handle + " • " + core::segment_label_ru(c.segment) + "  ♥ " + fmt_compact(c.likes);
        renderer_.draw_text_fit(x + 13.0f, cy + 8.0f, w - 26.0f, head, 12.0f,
                                c.is_toxic ? ui::Color::rgba(251, 113, 133) : ui::Color::rgba(56, 189, 248));
        renderer_.draw_text_wrapped(x + 13.0f, cy + 28.0f, w - 26.0f, c.text, 13.0f, ui::Color::rgba(226, 232, 240));
        cy += ch + 8.0f;
    }
    return cy;
}

float ViralGameApp::draw_tab_career(float x, float y, float w) {
    float cy = y;
    const bool busy = engine_.is_live_video_running();
    const float half_w = (w - 8.0f) * 0.5f;

    // Этап карьеры
    renderer_.draw_rounded_rect(x, cy, w, 58.0f, 12.0f, ui::Color::rgba(19, 30, 58));
    renderer_.draw_text_fit(x + 14.0f, cy + 9.0f, w - 28.0f,
                            std::string("Этап карьеры: ") + engine_.creator_system.stage_spec().title_ru,
                            14.5f, ui::Color::rgba(250, 204, 21));
    char sk_buf[128];
    const auto sk = engine_.creator_system.effective_skills();
    std::snprintf(sk_buf, sizeof(sk_buf), "Харизма %.0f • Монтаж %.0f • Аналитика %.0f • Свет/Камера Ур.%u",
                  static_cast<double>(sk.charisma), static_cast<double>(sk.editing),
                  static_cast<double>(sk.analytics),
                  static_cast<unsigned>(engine_.creator_system.creator.equipment_tier));
    renderer_.draw_text_fit(x + 14.0f, cy + 32.0f, w - 28.0f, sk_buf, 12.0f, ui::Color::rgba(186, 199, 218));
    cy += 68.0f;

    if (ui_.button(renderer_, x, cy, half_w, 46.0f, "Отдых +4 дня (+энергия)", false, busy)) {
        sound_.play_tap();
        engine_.advance_days(4);
        save_autosave();
        notify_toast("Вы отдохнули 4 дня: энергия и выгорание восстановлены!");
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 46.0f, "Живой стрим (2ч)", true, busy)) {
        sound_.play_cash();
        const auto sr = engine_.run_live_stream({});
        save_autosave();
        notify_toast("Стрим принёс $" + std::to_string(static_cast<int>(sr.donations_usd)) +
                     " донатов и +" + std::to_string(sr.new_subscribers) + " подписчиков!");
    }
    cy += 54.0f;

    // Прокачка оборудования и найм команды
    const int cur_tier = engine_.creator_system.creator.equipment_tier;
    const double next_eq_cost = 400.0 * std::pow(2.4, static_cast<double>(cur_tier - 1));
    if (ui_.button(renderer_, x, cy, half_w, 46.0f,
                   "Апгрейд студии (" + fmt_money(next_eq_cost) + ")", false, busy)) {
        double cost = 0.0;
        std::string err;
        if (engine_.creator_system.upgrade_equipment(cost, err)) {
            sound_.play_cash();
            save_autosave();
            notify_toast("Студия улучшена до Уровня " + std::to_string(engine_.creator_system.creator.equipment_tier) + "!");
        } else {
            sound_.play_warning();
            notify_toast(err);
        }
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 46.0f, "Нанять монтажёра", false, busy)) {
        std::string err;
        if (engine_.creator_system.hire_member(core::TeamRole::Editor, 68.0f, engine_.prng, err)) {
            sound_.play_cash();
            save_autosave();
            notify_toast("Монтажёр в штате! Теперь в таймлайне доступно больше бустов ⚡.");
        } else {
            sound_.play_warning();
            notify_toast(err);
        }
    }
    cy += 56.0f;

    // Нарративные дилеммы с ДИНАМИЧЕСКОЙ высотой под телефон
    renderer_.draw_text_fit(
        x + 2.0f, cy, w - 4.0f,
        "Дилеммы карьеры (отложенных последствий: " + std::to_string(engine_.clock.pending_count()) + "):",
        14.5f, ui::Color::rgba(248, 250, 252));
    cy += 24.0f;

    const auto dils = engine_.dilemmas.available_dilemmas(engine_.creator_system.current_stage());
    if (dils.empty()) {
        const float eh = renderer_.measure_text_wrapped(
            w - 24.0f,
            "Наберите 1 000 подписчиков (этап «Растущий канал»), чтобы открыть сюжетные дилеммы с отложенными последствиями через недели!",
            12.5f);
        renderer_.draw_rounded_rect(x, cy, w, eh + 20.0f, 11.0f, ui::Color::rgba(15, 23, 44));
        renderer_.draw_text_wrapped(
            x + 12.0f, cy + 10.0f, w - 24.0f,
            "Наберите 1 000 подписчиков (этап «Растущий канал»), чтобы открыть сюжетные дилеммы с отложенными последствиями через недели!",
            12.5f, ui::Color::rgba(148, 163, 184));
        cy += eh + 30.0f;
    } else {
        const auto* d = dils.front();
        const float sit_h = renderer_.measure_text_wrapped(w - 28.0f, d->situation_ru, 13.0f);
        const float card_h = 44.0f + sit_h + static_cast<float>(d->choices.size()) * 52.0f + 12.0f;

        renderer_.draw_rounded_rect(x, cy, w, card_h, 14.0f, ui::Color::rgba(23, 25, 52));
        renderer_.draw_rect_outline(x, cy, w, card_h, 1.4f, ui::Color::rgba(250, 204, 21, 130));
        renderer_.draw_text_fit(x + 14.0f, cy + 10.0f, w - 28.0f, d->title_ru, 15.0f, ui::Color::rgba(250, 204, 21));
        renderer_.draw_text_wrapped(x + 14.0f, cy + 34.0f, w - 28.0f, d->situation_ru,
                                    13.0f, ui::Color::rgba(226, 232, 240));
        float by = cy + 42.0f + sit_h;
        for (const auto& ch : d->choices) {
            if (ui_.button(renderer_, x + 12.0f, by, w - 24.0f, 44.0f, ch.label_ru, false, busy)) {
                sound_.play_tap();
                engine_.choose_dilemma_option(d->id, ch.id);
                save_autosave();
                notify_toast("Решение принято! Последствия догонят канал спустя время...");
                break;
            }
            by += 50.0f;
        }
        cy += card_h + 12.0f;
    }

    // Системные функции: Бенчмарк 100k и Новая карьера
    if (ui_.button(renderer_, x, cy, half_w, 42.0f, "Тест 100k агентов", false, busy)) {
        run_100k_benchmark();
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 42.0f,
                   confirm_reset_career_ ? "Точно сбросить?" : "Новая карьера", false, busy)) {
        if (!confirm_reset_career_) {
            sound_.play_warning();
            confirm_reset_career_ = true;
            notify_toast("Нажмите «Точно сбросить?» ещё раз, чтобы начать карьеру заново.");
        } else {
            confirm_reset_career_ = false;
            engine_ = core::Engine(core::EngineConfig{
                static_cast<std::uint32_t>(anim_time_sec_ * 1000.0f) + 20261029u,
                "@viral_creator", "ВИРАЛ Канал", core::NicheId::Tech, 750.0, 50'000, 20'000});
            reset_timeline_clips();
            save_autosave();
            sound_.play_cash();
            notify_toast("Новая карьера начата с чистого листа!");
        }
    }
    cy += 50.0f;

    if (!bench_result_ru_.empty()) {
        const float bh = renderer_.measure_text_wrapped(w - 24.0f, bench_result_ru_, 12.5f) + 16.0f;
        renderer_.draw_rounded_rect(x, cy, w, bh, 10.0f, ui::Color::rgba(20, 83, 45));
        renderer_.draw_text_wrapped(x + 12.0f, cy + 8.0f, w - 24.0f, bench_result_ru_, 12.5f, ui::Color::rgba(134, 239, 172));
        cy += bh + 10.0f;
    }

    // Журнал событий канала
    renderer_.draw_text(x + 4.0f, cy, "Журнал событий канала:", 14.0f, ui::Color::rgba(248, 250, 252));
    cy += 24.0f;
    for (std::size_t i = 0; i < std::min<std::size_t>(6, engine_.event_log().size()); ++i) {
        const auto& ev = engine_.event_log()[i];
        const std::string line = "[День " + std::to_string(ev.day) + "] " + ev.message_ru;
        const float lh = renderer_.measure_text_wrapped(w - 16.0f, line, 12.0f);
        renderer_.draw_text_wrapped(x + 6.0f, cy, w - 12.0f, line, 12.0f, ui::Color::rgba(148, 163, 184));
        cy += lh + 6.0f;
    }
    return cy;
}

float ViralGameApp::draw_tab_world(float x, float y, float w) {
    float cy = y;

    const std::string mode_str = pool_status_.cloud_connected
                                     ? "Облако Supabase: Общий пул активен"
                                     : "Локальная симуляция AI-конкурентов";
    char pool_info[160];
    std::snprintf(pool_info, sizeof(pool_info),
                  "Накал конкуренции: %.2f • Множитель показов ниши: x%.2f",
                  static_cast<double>(pool_status_.competition_heat),
                  static_cast<double>(engine_.world_pool_multiplier()));
    const float hl_h = pool_status_.headline_ru.empty()
                           ? 0.0f
                           : renderer_.measure_text_wrapped(w - 28.0f, pool_status_.headline_ru, 12.5f) + 6.0f;
    const float box_h = 66.0f + hl_h;

    renderer_.draw_rounded_rect(x, cy, w, box_h, 14.0f, ui::Color::rgba(15, 23, 44));
    renderer_.draw_rect_outline(x, cy, w, box_h, 1.0f, ui::Color::rgba(56, 189, 248, 60));
    renderer_.draw_text_fit(x + 14.0f, cy + 12.0f, w - 28.0f, mode_str, 14.5f, ui::Color::rgba(56, 189, 248));
    renderer_.draw_text_fit(x + 14.0f, cy + 36.0f, w - 28.0f, pool_info, 12.5f, ui::Color::rgba(226, 232, 240));
    if (!pool_status_.headline_ru.empty()) {
        renderer_.draw_text_wrapped(x + 14.0f, cy + 58.0f, w - 28.0f, pool_status_.headline_ru,
                                    12.5f, ui::Color::rgba(250, 204, 21));
    }
    cy += box_h + 12.0f;

    if (ui_.button(renderer_, x, cy, w, 44.0f, "Смена эпохи алгоритма платформы")) {
        const auto& reg = engine_.trigger_algorithm_update();
        sound_.play_warning();
        notify_toast(std::string("Платформа обновила алгоритм: ") + reg.title_ru);
    }
    cy += 54.0f;

    renderer_.draw_text_fit(
        x + 2.0f, cy, w - 4.0f,
        std::string("Топ каналов ниши «") + core::niche_spec(selected_niche_).title_ru + "»:",
        15.0f, ui::Color::rgba(248, 250, 252));
    cy += 26.0f;

    for (const auto& entry : leaderboard_) {
        renderer_.draw_rounded_rect(
            x, cy, w, 46.0f, 11.0f,
            entry.is_player ? ui::Color::rgba(30, 58, 138) : ui::Color::rgba(15, 23, 44));
        if (entry.is_player) {
            renderer_.draw_rect_outline(x, cy, w, 46.0f, 1.4f, ui::Color::rgba(56, 189, 248));
        }

        char rank_buf[24];
        std::snprintf(rank_buf, sizeof(rank_buf), "#%d  ", entry.rank);
        std::string left(rank_buf);
        left += entry.display_name;

        const std::string right = fmt_compact(entry.subscribers) + " подп.";
        const float rw = renderer_.measure_text(right, 13.5f);
        renderer_.draw_text_fit(x + 14.0f, cy + 14.0f, w - rw - 34.0f, left, 13.5f,
                                entry.is_player ? ui::Color::rgba(255, 255, 255) : ui::Color::rgba(226, 232, 240));
        renderer_.draw_text(x + w - 14.0f - rw, cy + 14.0f, right, 13.5f, ui::Color::rgba(56, 189, 248));
        cy += 52.0f;
    }
    return cy;
}

void ViralGameApp::draw_bottom_nav(float x, float y, float w, float h) {
    renderer_.draw_rect(x, y, w, h, ui::Color::rgba(11, 18, 36, 252));
    renderer_.draw_rect(x, y, w, 1.0f, ui::Color::rgba(56, 189, 248, 70));

    const float side_pad = w < 540.0f ? 6.0f : 16.0f;
    const float cw = std::min(w - side_pad * 2.0f, 680.0f);
    const float cx = (w - cw) * 0.5f;
    const float bw = cw / 5.0f;
    const float active_h = h - ui_.safe_area.bottom;

    const bool has_dilemma = !engine_.dilemmas.available_dilemmas(engine_.creator_system.current_stage()).empty();
    const bool live_airing = engine_.is_live_video_running();

    static constexpr const char* kLabels[5] = {"Студия", "Алгоритм", "Аудитория", "Карьера", "Мир"};
    for (int i = 0; i < 5; ++i) {
        const auto tab = static_cast<GameTab>(i);
        const bool badge = (i == 1 && live_airing) || (i == 3 && has_dilemma);
        const ui::Color bcol = (i == 1) ? ui::Color::rgba(56, 189, 248) : ui::Color::rgba(250, 204, 21);

        if (ui_.nav_tab_item(renderer_, i, cx + static_cast<float>(i) * bw, y + 2.0f, bw, active_h - 4.0f,
                             kLabels[i], current_tab_ == tab, badge, bcol)) {
            sound_.play_tap();
            current_tab_ = tab;
            confirm_reset_career_ = false;
            ui_.scroll_y = 0.0f;
        }
    }
}

} // namespace simul::game
