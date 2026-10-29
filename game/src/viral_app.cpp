#include "viral_app.hpp"
#include "simul/core/binary_save.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>

namespace simul::game {

namespace {

constexpr const char* kTitlePresetsA[core::kNicheCount][3] = {
    {"Я прошёл хардкорный рейд в одиночку", "100 дней выживания без брони", "Секретный билд нового сезона"},
    {"Я проверил алгоритм ленты на 50 000 зрителей", "Честный краш-тест нового флагмана", "Сборка бесшумного ПК за $400"},
    {"30 дней подъёма в 5 утра без кофе", "Месяц жизни в микро-студии", "Неделя полного цифрового детокса"},
    {"Как алгоритмы управляют дофамином", "Физика невозможных вирусных трюков", "Анатомия пузыря на миллиард"},
    {"Прятки в огромном павильоне на $10 000", "Угадай профи среди пяти актёров", "Блюдо шефа за $5 против $500"},
};

constexpr const char* kTitlePresetsB[core::kNicheCount][3] = {
    {"Никто не верил, что этот босс падет!", "Финал на 1 HP: выживание 100 дней", "Мета сломана? Тест секретного патча"},
    {"Что скрывает лента рекомендаций?", "Выдержит ли экран? Жёсткий тест", "Идеальный тихий ПК своими руками"},
    {"Что стало с энергией через месяц в 5:00?", "Минимализм без прикрас: итог 30 дней", "7 дней без смартфона: честный опыт"},
    {"Почему вы не можете закрыть ленту?", "Разоблачение вирусных трюков физикой", "Почему хайп всегда лопается?"},
    {"Последний найденный забирает весь банк!", "Кто из них настоящий мастер?", "Слепая дегустация: $5 или $500?"},
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
          "@viral_native",
          "ВИРАЛ Канал",
          core::NicheId::Tech,
          750.0,
          50'000,
          20'000}) {}

bool ViralGameApp::init() {
    sound_.init(44100);
    if (!renderer_.init()) {
        return false;
    }
    pool_status_ = world_.fetch_pool_status(selected_niche_);
    engine_.set_world_pool_multiplier(pool_status_.pool_multiplier);
    leaderboard_ = world_.leaderboard(
        selected_niche_,
        engine_.creator_system.creator.handle,
        engine_.creator_system.creator.display_name,
        engine_.creator_system.current_stage(),
        engine_.subscribers(),
        engine_.total_views());
    status_banner_ru_ = "Добро пожаловать в ВИРАЛ! Настройте первое видео и запустите волны алгоритма.";
    return true;
}

void ViralGameApp::shutdown() {
    renderer_.shutdown();
    sound_.shutdown();
}

void ViralGameApp::on_pointer_down(float x, float y) {
    ui_.pointer.x = x;
    ui_.pointer.y = y;
    ui_.pointer.press_start_x = x;
    ui_.pointer.press_start_y = y;
    ui_.pointer.down = true;
    ui_.pointer.just_pressed = true;
    ui_.pointer.is_scrolling = false;
}

void ViralGameApp::on_pointer_move(float x, float y) {
    if (ui_.pointer.down) {
        ui_.pointer.drag_dy += (y - ui_.pointer.y);
    }
    ui_.pointer.x = x;
    ui_.pointer.y = y;
}

void ViralGameApp::on_pointer_up(float x, float y) {
    ui_.pointer.x = x;
    ui_.pointer.y = y;
    ui_.pointer.down = false;
    ui_.pointer.just_released = true;
}

void ViralGameApp::on_wheel(float dy) {
    ui_.pointer.wheel_dy += dy;
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

    in.editing.hook_strength = hook_strength_;
    in.editing.pacing_tightness = pacing_tightness_;
    in.editing.payoff_strength = (hook_strength_ + pacing_tightness_) * 0.5f;
    in.editing.ad_placement_ratio = in.sponsor.active ? 0.45f : -1.0f;

    in.packaging.variant_a.id = 'A';
    in.packaging.variant_a.title = kTitlePresetsA[n_idx][p_idx];
    in.packaging.variant_a.visual_contrast = visual_contrast_;
    in.packaging.variant_a.curiosity_gap = visual_contrast_;
    in.packaging.variant_a.clickbait_level = clickbait_level_;

    in.packaging.has_variant_b = use_ab_test_;
    if (use_ab_test_) {
        in.packaging.variant_b.id = 'B';
        in.packaging.variant_b.title = kTitlePresetsB[n_idx][p_idx];
        in.packaging.variant_b.visual_contrast = core::clamp_val(visual_contrast_ + 0.07f, 0.1f, 1.0f);
        in.packaging.variant_b.curiosity_gap = core::clamp_val(visual_contrast_ + 0.05f, 0.1f, 1.0f);
        in.packaging.variant_b.clickbait_level = core::clamp_val(clickbait_level_ + 0.03f, 0.0f, 1.0f);
    }

    in.delegated_to_team = !engine_.creator_system.members().empty();
    return in;
}

void ViralGameApp::start_live_release() {
    std::string err;
    if (!engine_.begin_video_release(build_current_release(), err)) {
        sound_.play_warning();
        status_banner_ru_ = err;
        return;
    }
    sound_.play_tap();
    live_hour_timer_ = 0.0f;
    current_tab_ = GameTab::Algorithm;
    ui_.scroll_y = 0.0f;
    status_banner_ru_ = "Ролик опубликован! Первые 48 часов решают судьбу продвижения...";
}

void ViralGameApp::run_instant_release() {
    std::string err;
    if (!engine_.publish_and_simulate_full_video(build_current_release(), err)) {
        sound_.play_warning();
        status_banner_ru_ = err;
        return;
    }
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
    current_tab_ = GameTab::Algorithm;
    ui_.scroll_y = 0.0f;
    status_banner_ru_ = vid.post_mortem.verdict_title_ru;
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
                  "C++20 100k SoA: 10 видео (480 волн) за %.2f мс (%.3f мс/волна)!",
                  ms, ms / 480.0);
    bench_result_ru_ = buf;
    sound_.play_cash();
}

void ViralGameApp::update_and_render(float dt_sec, float logical_w, float logical_h) {
    // Почасовая живая симуляция активного ролика
    if (engine_.is_live_video_running()) {
        live_hour_timer_ += dt_sec;
        if (live_hour_timer_ >= 0.055f) {
            live_hour_timer_ = 0.0f;
            const std::uint8_t prev_w = engine_.published_videos().back().max_wave_reached;
            const auto* pt = engine_.step_live_video_hour();
            if (pt && pt->wave_index > prev_w && pt->wave_index >= 2) {
                sound_.play_wave_breakthrough(pt->wave_index);
                renderer_.spawn_wave_burst(logical_w * 0.5f, 220.0f, pt->wave_index);
            }
            if (!engine_.is_live_video_running()) {
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
                status_banner_ru_ = vid.post_mortem.verdict_title_ru;
            }
        }
    }

    anim_subs_.set_target(static_cast<float>(engine_.subscribers()));
    anim_cash_.set_target(static_cast<float>(engine_.creator_system.creator.cash_usd));
    anim_subs_.step(dt_sec);
    anim_cash_.step(dt_sec);

    const float header_h = 98.0f;
    const float nav_h = 62.0f;
    const float viewport_h = std::max(120.0f, logical_h - header_h - nav_h);
    const float max_scroll = std::max(0.0f, content_height_ - viewport_h + 24.0f);

    ui_.begin_input_frame(dt_sec, max_scroll);
    renderer_.begin_frame(logical_w, logical_h);

    // Фон приложения
    renderer_.draw_gradient_v(0.0f, 0.0f, logical_w, logical_h,
                              ui::Color::rgba(9, 14, 28),
                              ui::Color::rgba(5, 8, 18));

    // Центрированная колонка (адаптируется и под смартфон 9:19.5, и под ПК)
    const float col_w = std::min(logical_w - 24.0f, 760.0f);
    const float col_x = (logical_w - col_w) * 0.5f;
    const float scroll_top = header_h - ui_.scroll_y;

    float end_y = scroll_top;
    switch (current_tab_) {
        case GameTab::Studio: end_y = draw_tab_studio(col_x, scroll_top + 10.0f, col_w); break;
        case GameTab::Algorithm: end_y = draw_tab_algorithm(col_x, scroll_top + 10.0f, col_w); break;
        case GameTab::Audience: end_y = draw_tab_audience(col_x, scroll_top + 10.0f, col_w); break;
        case GameTab::Career: end_y = draw_tab_career(col_x, scroll_top + 10.0f, col_w); break;
        case GameTab::World: end_y = draw_tab_world(col_x, scroll_top + 10.0f, col_w); break;
    }
    content_height_ = end_y - scroll_top;

    // Непрозрачная верхняя шапка HUD и нижний таб-бар поверх скролла
    draw_hud_header(0.0f, 0.0f, logical_w);
    draw_bottom_nav(0.0f, logical_h - nav_h, logical_w, nav_h);

    renderer_.step_and_draw_particles(dt_sec);
    renderer_.flush_gpu_batch();
    ui_.end_input_frame();
}

void ViralGameApp::draw_hud_header(float x, float y, float w) {
    renderer_.draw_rect(x, y, w, 96.0f, ui::Color::rgba(12, 19, 38, 248));
    renderer_.draw_rect(x, y + 95.0f, w, 1.0f, ui::Color::rgba(56, 189, 248, 70));

    const float cx = (w - std::min(w - 24.0f, 760.0f)) * 0.5f;
    const float cw = std::min(w - 24.0f, 760.0f);

    const std::string title = std::string("ВИРАЛ • ") + engine_.creator_system.stage_spec().title_ru +
                              " • День " + std::to_string(engine_.clock.current_day());
    renderer_.draw_text(cx, y + 10.0f, title, 15.5f, ui::Color::rgba(248, 250, 252));

    char stats[160];
    std::snprintf(stats, sizeof(stats),
                  "Подп: %s   Бюджет: $%.0f   Энергия: %.0f%%   Траст: %.2f%s",
                  fmt_compact(static_cast<std::int64_t>(std::llround(anim_subs_.value))).c_str(),
                  static_cast<double>(anim_cash_.value),
                  static_cast<double>(engine_.creator_system.creator.energy),
                  static_cast<double>(engine_.recommender.trust_score()),
                  engine_.recommender.is_shadowbanned() ? " [ШАДОУБАН]" : "");

    renderer_.draw_text(cx, y + 36.0f, stats, 13.0f,
                        engine_.recommender.is_shadowbanned()
                            ? ui::Color::rgba(244, 63, 94)
                            : ui::Color::rgba(56, 189, 248));

    renderer_.draw_text(cx, y + 62.0f, status_banner_ru_, 12.0f, ui::Color::rgba(148, 163, 184));

    if (ui_.button(renderer_, cx + cw - 86.0f, y + 8.0f, 86.0f, 26.0f,
                   sound_.is_muted() ? "Звук: ВЫКЛ" : "Звук: ВКЛ")) {
        sound_.set_muted(!sound_.is_muted());
    }
}

float ViralGameApp::draw_tab_studio(float x, float y, float w) {
    float cy = y;
    renderer_.draw_rounded_rect(x, cy, w, 460.0f, 14.0f, ui::Color::rgba(15, 23, 42));

    const auto skills = engine_.creator_system.effective_skills();
    const auto release_preview = build_current_release();
    const auto pack_eval = core::evaluate_packaging_variant(
        release_preview.packaging.variant_a, selected_niche_, skills.analytics);

    char ctr_hdr[96];
    std::snprintf(ctr_hdr, sizeof(ctr_hdr), "Съёмка и упаковка • Прогноз CTR: %.1f%%",
                  static_cast<double>(pack_eval.predicted_ctr * 100.0f));
    renderer_.draw_text(x + 14.0f, cy + 12.0f, ctr_hdr, 15.0f, ui::Color::rgba(248, 250, 252));
    cy += 42.0f;

    // Переключатель ниши и темы
    const float half_w = (w - 36.0f) * 0.5f;
    if (ui_.button(renderer_, x + 14.0f, cy, half_w, 34.0f,
                   std::string("Ниша: ") + core::niche_spec(selected_niche_).title_ru)) {
        sound_.play_tap();
        selected_niche_ = static_cast<core::NicheId>((static_cast<int>(selected_niche_) + 1) % static_cast<int>(core::kNicheCount));
        pool_status_ = world_.fetch_pool_status(selected_niche_);
        engine_.set_world_pool_multiplier(pool_status_.pool_multiplier);
    }
    if (ui_.button(renderer_, x + 22.0f + half_w, cy, half_w, 34.0f, "Сменить идею ролика")) {
        sound_.play_tap();
        selected_title_preset_ = (selected_title_preset_ + 1) % 3;
    }
    cy += 44.0f;

    renderer_.draw_text(x + 14.0f, cy, "Заголовок А: " + release_preview.packaging.variant_a.title,
                        13.0f, ui::Color::rgba(226, 232, 240));
    cy += 22.0f;

    if (use_ab_test_) {
        renderer_.draw_text(x + 14.0f, cy, "Заголовок B: " + release_preview.packaging.variant_b.title,
                            12.5f, ui::Color::rgba(129, 140, 248));
        cy += 22.0f;
    }

    // Тренд и A/B тест
    const auto& trs = engine_.trends.active_trends();
    std::string trend_lbl = "Тренд: Авторская тема (x1.00)";
    if (selected_trend_idx_ >= 0 && static_cast<std::size_t>(selected_trend_idx_) < trs.size()) {
        const auto& t = trs[static_cast<std::size_t>(selected_trend_idx_)];
        char tb[64];
        std::snprintf(tb, sizeof(tb), " (x%.2f)", static_cast<double>(t.virality_boost));
        trend_lbl = std::string("Тренд: ") + core::trend_phase_label_ru(t.phase) + tb;
    }
    if (ui_.button(renderer_, x + 14.0f, cy, half_w, 32.0f, trend_lbl)) {
        sound_.play_tap();
        selected_trend_idx_ += 1;
        if (static_cast<std::size_t>(selected_trend_idx_) >= trs.size()) selected_trend_idx_ = -1;
    }
    if (ui_.button(renderer_, x + 22.0f + half_w, cy, half_w, 32.0f,
                   use_ab_test_ ? "A/B-тест обложек: ВКЛ" : "A/B-тест обложек: ВЫКЛ")) {
        sound_.play_tap();
        use_ab_test_ = !use_ab_test_;
    }
    cy += 42.0f;

    // Слайдеры мини-игр (Монтаж + Превью + Бюджет)
    const float sw = w - 28.0f;
    ui_.slider_float(renderer_, 1, x + 14.0f, cy, sw, "Монтаж: Сила хука первых 5 секунд", hook_strength_, 0.15f, 1.0f);
    cy += 40.0f;
    ui_.slider_float(renderer_, 2, x + 14.0f, cy, sw, "Монтаж: Плотность ритма и резка пауз", pacing_tightness_, 0.15f, 1.0f);
    cy += 40.0f;
    ui_.slider_float(renderer_, 3, x + 14.0f, cy, sw, "Превью: Контраст и интрига обложки", visual_contrast_, 0.15f, 1.0f);
    cy += 40.0f;
    ui_.slider_float(renderer_, 4, x + 14.0f, cy, sw, "Уровень кликбейта (риск потери траста!)", clickbait_level_, 0.0f, 0.95f);
    cy += 40.0f;
    ui_.slider_float(renderer_, 5, x + 14.0f, cy, sw, "Бюджет продакшна ролика", budget_usd_, 0.0f, 500.0f);
    cy += 44.0f;

    static constexpr const char* kSponsorLabels[3] = {
        "Интеграция: Без рекламы",
        "Интеграция: Белый бренд (+$450)",
        "Интеграция: Сомнительный токен (+$1800, риск!)",
    };
    if (ui_.button(renderer_, x + 14.0f, cy, sw, 32.0f, kSponsorLabels[sponsor_mode_])) {
        sound_.play_tap();
        sponsor_mode_ = (sponsor_mode_ + 1) % 3;
    }
    cy += 42.0f;

    // Кнопки запуска релиза
    const bool busy = engine_.is_live_video_running();
    if (ui_.button(renderer_, x + 14.0f, cy, sw * 0.66f - 4.0f, 42.0f,
                   "ВЫПУСТИТЬ РОЛИК (48Ч ВОЛНЫ)", true, busy)) {
        start_live_release();
    }
    if (ui_.button(renderer_, x + 14.0f + sw * 0.66f + 4.0f, cy, sw * 0.34f - 4.0f, 42.0f,
                   "Мгновенно", false, busy)) {
        run_instant_release();
    }
    cy += 58.0f;
    return cy;
}

float ViralGameApp::draw_tab_algorithm(float x, float y, float w) {
    float cy = y;
    const auto& vids = engine_.published_videos();
    const core::PublishedVideoRecord* latest = vids.empty() ? nullptr : &vids.back();

    const float half_w = (w - 8.0f) * 0.5f;
    if (ui_.button(renderer_, x, cy, half_w, 32.0f, "График волн (48ч)", !show_retention_chart_)) {
        sound_.play_tap();
        show_retention_chart_ = false;
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 32.0f, "Кривая удержания", show_retention_chart_)) {
        sound_.play_tap();
        show_retention_chart_ = true;
    }
    cy += 40.0f;

    std::vector<core::VideoWavePoint> empty_tl;
    std::array<float, 10> empty_ret{};
    renderer_.draw_wave_telemetry(
        x, cy, w, 230.0f,
        latest ? latest->hourly_timeline : empty_tl,
        latest ? latest->retention_curve : empty_ret,
        show_retention_chart_);
    cy += 240.0f;

    if (!latest) {
        renderer_.draw_text(x + 8.0f, cy, "Пока нет опубликованных видео. Перейдите во вкладку «Студия».",
                            14.0f, ui::Color::rgba(148, 163, 184));
        return cy + 40.0f;
    }

    // KPI-плашка
    renderer_.draw_rounded_rect(x, cy, w, 74.0f, 12.0f, ui::Color::rgba(15, 23, 42));
    char kpi1[160];
    std::snprintf(kpi1, sizeof(kpi1),
                  "Просмотры: %s   CTR: %.1f%%   Удержание: %.0f%%   Волна: %u",
                  fmt_compact(latest->total_views).c_str(),
                  static_cast<double>(latest->avg_ctr * 100.0f),
                  static_cast<double>(latest->avg_retention * 100.0f),
                  static_cast<unsigned>(latest->max_wave_reached));
    renderer_.draw_text(x + 14.0f, cy + 12.0f, kpi1, 14.0f, ui::Color::rgba(248, 250, 252));

    char kpi2[160];
    std::snprintf(kpi2, sizeof(kpi2),
                  "Подписчики с видео: %s%s   Доход ролика: $%.1f   Лайки: %s",
                  latest->net_subscribers >= 0 ? "+" : "",
                  fmt_compact(latest->net_subscribers).c_str(),
                  latest->adsense_revenue_usd + latest->sponsor_revenue_usd,
                  fmt_compact(latest->likes).c_str());
    renderer_.draw_text(x + 14.0f, cy + 40.0f, kpi2, 13.0f, ui::Color::rgba(56, 189, 248));
    cy += 84.0f;

    // Пост-мортем разбор алгоритма
    if (latest->post_mortem.ready) {
        renderer_.draw_text(x + 4.0f, cy, latest->post_mortem.verdict_title_ru,
                            15.5f, ui::Color::rgba(250, 204, 21));
        cy += 22.0f;
        float sh = 0.0f;
        renderer_.draw_text_wrapped(x + 4.0f, cy, w - 8.0f, latest->post_mortem.summary_ru,
                                    13.0f, ui::Color::rgba(203, 213, 225), &sh);
        cy += sh + 8.0f;

        for (const auto& f : latest->post_mortem.factors) {
            const ui::Color bar_c = f.kind == core::FactorKind::Positive
                                        ? ui::Color::rgba(34, 197, 94)
                                        : (f.kind == core::FactorKind::Negative
                                               ? ui::Color::rgba(244, 63, 94)
                                               : ui::Color::rgba(56, 189, 248));
            renderer_.draw_rounded_rect(x, cy, w, 56.0f, 9.0f, ui::Color::rgba(15, 23, 42));
            renderer_.draw_rect(x, cy, 4.0f, 56.0f, bar_c);
            renderer_.draw_text(x + 12.0f, cy + 7.0f, f.label_ru, 13.5f, bar_c);
            renderer_.draw_text_wrapped(x + 12.0f, cy + 26.0f, w - 24.0f, f.detail_ru,
                                        12.0f, ui::Color::rgba(203, 213, 225));
            cy += 64.0f;
        }
    }
    return cy;
}

float ViralGameApp::draw_tab_audience(float x, float y, float w) {
    float cy = y;
    const auto sum = engine_.audience.summarize();

    renderer_.draw_rounded_rect(x, cy, w, 210.0f, 12.0f, ui::Color::rgba(15, 23, 42));
    char hdr[128];
    std::snprintf(hdr, sizeof(hdr), "Пул живых SoA-агентов (%zu в памяти)", sum.active_agents);
    renderer_.draw_text(x + 14.0f, cy + 12.0f, hdr, 15.0f, ui::Color::rgba(248, 250, 252));

    char psych[160];
    std::snprintf(psych, sizeof(psych),
                  "Лояльность: %.1f%%   Усталость: %.1f%%   Ностальгия олдов: %.1f%%",
                  static_cast<double>(sum.avg_loyalty * 100.0f),
                  static_cast<double>(sum.avg_fatigue * 100.0f),
                  static_cast<double>(sum.avg_nostalgia * 100.0f));
    renderer_.draw_text(x + 14.0f, cy + 36.0f, psych, 12.5f, ui::Color::rgba(56, 189, 248));

    float sy = cy + 62.0f;
    for (std::size_t s = 0; s < core::kSegmentCount; ++s) {
        const auto seg = static_cast<core::ViewerSegment>(s);
        renderer_.draw_text(x + 14.0f, sy, core::segment_label_ru(seg), 13.0f, ui::Color::rgba(203, 213, 225));
        const std::string cnt = std::to_string(sum.segment_counts[s]);
        renderer_.draw_text(x + w - 18.0f - renderer_.measure_text(cnt, 13.0f), sy, cnt,
                            13.0f, ui::Color::rgba(248, 250, 252));
        sy += 20.0f;
    }
    cy += 222.0f;

    renderer_.draw_text(x + 4.0f, cy, "Комментарии под последним роликом:", 15.0f, ui::Color::rgba(248, 250, 252));
    cy += 26.0f;

    const auto& vids = engine_.published_videos();
    if (vids.empty() || vids.back().top_comments.empty()) {
        renderer_.draw_text(x + 4.0f, cy, "Выпустите видео, чтобы прочитать реакцию сегментов аудитории.",
                            13.0f, ui::Color::rgba(148, 163, 184));
        return cy + 34.0f;
    }

    for (const auto& c : vids.back().top_comments) {
        renderer_.draw_rounded_rect(x, cy, w, 62.0f, 10.0f, ui::Color::rgba(15, 23, 42));
        const std::string head = c.author_handle + " [" + core::segment_label_ru(c.segment) + "]   +" + fmt_compact(c.likes);
        renderer_.draw_text(x + 12.0f, cy + 8.0f, head, 12.5f,
                            c.is_toxic ? ui::Color::rgba(244, 63, 94) : ui::Color::rgba(56, 189, 248));
        renderer_.draw_text_wrapped(x + 12.0f, cy + 28.0f, w - 24.0f, c.text, 12.5f, ui::Color::rgba(226, 232, 240));
        cy += 70.0f;
    }
    return cy;
}

float ViralGameApp::draw_tab_career(float x, float y, float w) {
    float cy = y;
    const bool busy = engine_.is_live_video_running();
    const float half_w = (w - 8.0f) * 0.5f;

    if (ui_.button(renderer_, x, cy, half_w, 36.0f, "Отдых +4 дня (+энергия)", false, busy)) {
        sound_.play_tap();
        engine_.advance_days(4);
        status_banner_ru_ = "Вы отдохнули 4 дня: энергия восстановлена.";
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 36.0f, "Провести стрим (2ч)", false, busy)) {
        sound_.play_cash();
        const auto sr = engine_.run_live_stream({});
        status_banner_ru_ = "Стрим принёс $" + std::to_string(static_cast<int>(sr.donations_usd)) +
                            " донатов и +" + std::to_string(sr.new_subscribers) + " подписчиков!";
    }
    cy += 44.0f;

    if (ui_.button(renderer_, x, cy, half_w, 36.0f,
                   "Апгрейд студии (Ур." + std::to_string(engine_.creator_system.creator.equipment_tier) + ")", false, busy)) {
        double cost = 0.0;
        std::string err;
        if (engine_.creator_system.upgrade_equipment(cost, err)) {
            sound_.play_cash();
            status_banner_ru_ = "Оборудование улучшено за " + fmt_money(cost) + "!";
        } else {
            sound_.play_warning();
            status_banner_ru_ = err;
        }
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 36.0f, "Нанять монтажёра", false, busy)) {
        std::string err;
        if (engine_.creator_system.hire_member(core::TeamRole::Editor, 65.0f, engine_.prng, err)) {
            sound_.play_cash();
            status_banner_ru_ = "В команду нанят профессиональный монтажёр!";
        } else {
            sound_.play_warning();
            status_banner_ru_ = err;
        }
    }
    cy += 44.0f;

    if (ui_.button(renderer_, x, cy, half_w, 36.0f, "Сохранить (.simsave)", false, busy)) {
        std::string err;
        if (core::BinarySave::save_to_file(engine_, "career_slot1.simsave", err)) {
            sound_.play_cash();
            status_banner_ru_ = "Бинарное сохранение записано в career_slot1.simsave";
        } else {
            status_banner_ru_ = err;
        }
    }
    if (ui_.button(renderer_, x + half_w + 8.0f, cy, half_w, 36.0f, "Бенчмарк 100k агентов", false, busy)) {
        run_100k_benchmark();
    }
    cy += 44.0f;

    if (!bench_result_ru_.empty()) {
        renderer_.draw_rounded_rect(x, cy, w, 34.0f, 8.0f, ui::Color::rgba(20, 83, 45));
        renderer_.draw_text(x + 12.0f, cy + 8.0f, bench_result_ru_, 12.5f, ui::Color::rgba(134, 239, 172));
        cy += 42.0f;
    }

    // Нарративные дилеммы
    renderer_.draw_text(x + 4.0f, cy,
                        "Нарративные дилеммы (в очереди последствий: " + std::to_string(engine_.clock.pending_count()) + "):",
                        15.0f, ui::Color::rgba(248, 250, 252));
    cy += 26.0f;

    const auto dils = engine_.dilemmas.available_dilemmas(engine_.creator_system.current_stage());
    if (dils.empty()) {
        renderer_.draw_text_wrapped(
            x + 4.0f, cy, w - 8.0f,
            "Достигните этапа «Растущий канал» (1 000 подписчиков), чтобы открыть первые крупные дилеммы с отложенными последствиями!",
            13.0f, ui::Color::rgba(148, 163, 184));
        cy += 48.0f;
    } else {
        const auto* d = dils.front();
        renderer_.draw_rounded_rect(x, cy, w, 178.0f, 12.0f, ui::Color::rgba(15, 23, 42));
        renderer_.draw_text(x + 12.0f, cy + 10.0f, d->title_ru, 14.5f, ui::Color::rgba(250, 204, 21));
        float sh = 0.0f;
        renderer_.draw_text_wrapped(x + 12.0f, cy + 32.0f, w - 24.0f, d->situation_ru,
                                    12.5f, ui::Color::rgba(203, 213, 225), &sh);
        float by = cy + 84.0f;
        for (const auto& ch : d->choices) {
            if (ui_.button(renderer_, x + 12.0f, by, w - 24.0f, 38.0f, ch.label_ru, false, busy)) {
                sound_.play_tap();
                engine_.choose_dilemma_option(d->id, ch.id);
                status_banner_ru_ = "Выбор сделан. Последствия проявятся со временем...";
                break;
            }
            by += 44.0f;
        }
        cy += 188.0f;
    }

    renderer_.draw_text(x + 4.0f, cy, "Журнал событий канала:", 14.0f, ui::Color::rgba(248, 250, 252));
    cy += 24.0f;
    for (std::size_t i = 0; i < std::min<std::size_t>(6, engine_.event_log().size()); ++i) {
        const auto& ev = engine_.event_log()[i];
        renderer_.draw_text_wrapped(
            x + 4.0f, cy, w - 8.0f,
            "[День " + std::to_string(ev.day) + "] " + ev.message_ru,
            12.0f, ui::Color::rgba(148, 163, 184));
        cy += 22.0f;
    }
    return cy;
}

float ViralGameApp::draw_tab_world(float x, float y, float w) {
    float cy = y;
    renderer_.draw_rounded_rect(x, cy, w, 92.0f, 12.0f, ui::Color::rgba(15, 23, 42));

    const std::string mode_str = pool_status_.cloud_connected
                                     ? "Режим мира: Supabase Cloud (Общий пул)"
                                     : "Режим мира: Нативная симуляция соперников";
    renderer_.draw_text(x + 14.0f, cy + 12.0f, mode_str, 14.5f, ui::Color::rgba(56, 189, 248));

    char pool_info[160];
    std::snprintf(pool_info, sizeof(pool_info),
                  "Накал конкуренции ниши: %.2f   Множитель пула показов: x%.2f",
                  static_cast<double>(pool_status_.competition_heat),
                  static_cast<double>(engine_.world_pool_multiplier()));
    renderer_.draw_text(x + 14.0f, cy + 38.0f, pool_info, 13.0f, ui::Color::rgba(226, 232, 240));

    if (!pool_status_.headline_ru.empty()) {
        renderer_.draw_text(x + 14.0f, cy + 62.0f, pool_status_.headline_ru, 12.0f, ui::Color::rgba(250, 204, 21));
    }
    cy += 106.0f;

    if (ui_.button(renderer_, x, cy, w, 34.0f, "Спровоцировать смену эпохи алгоритма")) {
        const auto& reg = engine_.trigger_algorithm_update();
        sound_.play_warning();
        status_banner_ru_ = std::string("Новая эпоха алгоритма: ") + reg.title_ru;
    }
    cy += 46.0f;

    renderer_.draw_text(x + 4.0f, cy,
                        std::string("Сезонный рейтинг ниши «") + core::niche_spec(selected_niche_).title_ru + "»:",
                        15.0f, ui::Color::rgba(248, 250, 252));
    cy += 28.0f;

    for (const auto& entry : leaderboard_) {
        renderer_.draw_rounded_rect(x, cy, w, 40.0f, 9.0f,
                                    entry.is_player ? ui::Color::rgba(30, 58, 138) : ui::Color::rgba(15, 23, 42));
        char rank_buf[24];
        std::snprintf(rank_buf, sizeof(rank_buf), "#%d  ", entry.rank);
        std::string left(rank_buf);
        left += entry.display_name;
        left += " (";
        left += entry.handle;
        left += ")";
        const std::string right = fmt_compact(entry.subscribers) + " подп.";
        renderer_.draw_text(x + 14.0f, cy + 11.0f, left, 13.5f,
                            entry.is_player ? ui::Color::rgba(255, 255, 255) : ui::Color::rgba(226, 232, 240));
        renderer_.draw_text(x + w - 14.0f - renderer_.measure_text(right, 13.5f), cy + 11.0f, right,
                            13.5f, ui::Color::rgba(56, 189, 248));
        cy += 46.0f;
    }
    return cy;
}

void ViralGameApp::draw_bottom_nav(float x, float y, float w, float h) {
    renderer_.draw_rect(x, y, w, h, ui::Color::rgba(12, 19, 38, 250));
    renderer_.draw_rect(x, y, w, 1.0f, ui::Color::rgba(56, 189, 248, 65));

    const float cw = std::min(w - 16.0f, 760.0f);
    const float cx = (w - cw) * 0.5f;
    const float bw = (cw - 16.0f) / 5.0f;

    static constexpr const char* kLabels[5] = {"Студия", "Алгоритм", "Аудитория", "Карьера", "Мир"};
    for (int i = 0; i < 5; ++i) {
        const auto tab = static_cast<GameTab>(i);
        if (ui_.tab_button(renderer_, cx + static_cast<float>(i) * (bw + 4.0f), y + 8.0f, bw, h - 16.0f,
                           kLabels[i], current_tab_ == tab)) {
            sound_.play_tap();
            current_tab_ = tab;
            ui_.scroll_y = 0.0f;
        }
    }
}

} // namespace simul::game
