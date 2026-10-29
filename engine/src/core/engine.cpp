#include "simul/core/engine.hpp"
#include "simul/core/math_utils.hpp"
#include <cmath>

namespace simul::core {

Engine::Engine(const EngineConfig& cfg)
    : prng(cfg.seed),
      clock(10),
      audience(cfg.audience_capacity),
      initial_seed_(cfg.seed) {
    creator_system.creator.handle = cfg.handle;
    creator_system.creator.display_name = cfg.display_name;
    creator_system.creator.origin_niche = cfg.origin_niche;
    creator_system.creator.current_niche = cfg.origin_niche;
    creator_system.creator.cash_usd = cfg.starting_cash_usd;

    audience.seed_platform_population(prng, cfg.origin_niche, cfg.initial_platform_agents);
    trends.seed_initial_trends(prng, clock.current_day());

    push_event(EventType::VideoPublished,
               "Нативный C++20 движок Simul инициализирован (" +
                   std::to_string(audience.active_count()) + " SoA-агентов).");
}

std::int64_t Engine::total_views() const noexcept {
    std::int64_t sum = 0;
    for (const auto& v : videos_) sum += v.total_views;
    return sum;
}

void Engine::set_world_pool_multiplier(float m) noexcept {
    world_pool_multiplier_ = clamp_val(m, 0.50f, 1.40f);
}

void Engine::push_event(EventType type, const std::string& msg, std::uint8_t wave) {
    EngineEvent ev{};
    ev.type = type;
    ev.day = clock.current_day();
    ev.hour = clock.hour_of_day();
    ev.wave_index = wave;
    ev.message_ru = msg;
    events_.insert(events_.begin(), std::move(ev));
    if (events_.size() > 40) {
        events_.resize(40);
    }
}

bool Engine::begin_video_release(const VideoReleaseInput& input, std::string& out_error) {
    if (live_ctx_.has_value()) {
        out_error = "Дождитесь завершения 48 часов текущего релиза!";
        return false;
    }

    const double budget = std::max(0.0, input.production_budget_usd);
    if (creator_system.creator.cash_usd < budget) {
        out_error = "Недостаточно бюджета на съёмку ролика!";
        return false;
    }

    const int target_h = ((input.publish_hour_of_day % 24) + 24) % 24;
    const int cur_h = clock.hour_of_day();
    const int wait_h = target_h >= cur_h ? (target_h - cur_h) : (24 - cur_h + target_h);
    if (wait_h > 0) {
        advance_hours_internal(wait_h);
    }

    creator_system.creator.cash_usd -= budget;
    creator_system.creator.current_niche = input.niche;

    const auto skills = creator_system.effective_skills();
    const auto edit_res = solve_editing_minigame(
        input.editing, skills, creator_system.creator.equipment_tier, creator_system.creator.burnout);

    const auto eval_a = evaluate_packaging_variant(input.packaging.variant_a, input.niche, skills.analytics);
    const auto eval_b = input.packaging.has_variant_b
                            ? evaluate_packaging_variant(input.packaging.variant_b, input.niche, skills.analytics)
                            : PackagingEvaluation{};

    const float budget_factor = clamp_val(static_cast<float>(std::log10(1.0 + budget / 80.0)) * 0.14f, 0.0f, 0.22f);
    const float equip_factor = (static_cast<float>(creator_system.creator.equipment_tier - 1) / 4.0f) * 0.14f;
    const float skill_factor =
        (skills.scriptwriting * 0.40f + skills.charisma * 0.35f + skills.editing * 0.25f) / 100.0f;
    const float burnout_pen =
        creator_system.creator.burnout > 70.0f ? ((creator_system.creator.burnout - 70.0f) / 30.0f) * 0.20f : 0.0f;

    const float intrinsic_quality = clamp_val(
        edit_res.overall_edit_score * 0.48f + skill_factor * 0.34f + budget_factor + equip_factor - burnout_pen,
        0.08f,
        1.0f);

    const auto* tr = trends.find_by_id(input.trend_id);
    const float trend_boost =
        tr ? (tr->niche == input.niche ? tr->virality_boost : tr->virality_boost * 0.88f) : 1.0f;

    const float consistency_mult = recommender.register_upload_on_day(clock.current_day());
    const float publish_hour_mult = recommender.compute_publish_hour_multiplier(clock.hour_of_day());

    const double sponsor_payout = input.sponsor.active ? std::max(0.0, input.sponsor.payout_usd) : 0.0;
    const float sketchy_level = input.sponsor.active ? clamp_val(input.sponsor.sketchiness, 0.0f, 1.0f) : 0.0f;
    const float ad_annoyance = input.sponsor.active ? clamp_val(0.18f + sketchy_level * 0.65f, 0.0f, 1.0f) : 0.0f;

    if (sponsor_payout > 0.0) {
        creator_system.creator.cash_usd += sponsor_payout;
        if (sketchy_level > 0.45f) {
            if (recommender.adjust_trust(-sketchy_level * 0.14f)) {
                push_event(EventType::ShadowbanChanged,
                           recommender.is_shadowbanned()
                               ? "ШАДОУБАН: индекс доверия алгоритма упал ниже 0.35!"
                               : "Шадоубан снят: доверие алгоритма восстановлено.");
            }
        }
    }

    const std::int64_t collab_bonus =
        input.collab_partner_subs > 0
            ? std::llround(std::pow(static_cast<double>(input.collab_partner_subs), 0.72) *
                           static_cast<double>(clamp_val(input.collab_synergy, 0.6f, 1.6f)))
            : 0;

    creator_system.apply_video_effort(input.delegated_to_team);

    PublishedVideoRecord rec{};
    rec.sequence_number = static_cast<std::uint32_t>(videos_.size() + 1);
    rec.title = !input.packaging.variant_a.title.empty() ? input.packaging.variant_a.title : input.title;
    rec.niche = input.niche;
    rec.published_day = clock.current_day();
    rec.published_hour = clock.hour_of_day();
    rec.is_live_simulating = true;
    rec.sponsor_revenue_usd = sponsor_payout;
    rec.production_cost_usd = budget;
    rec.retention_curve = edit_res.retention_curve;
    rec.hourly_timeline.reserve(48);

    videos_.push_back(std::move(rec));

    LiveContext ctx{};
    ctx.video_index = videos_.size() - 1;
    ctx.input = input;
    ctx.edit_res = edit_res;
    ctx.eval_a = eval_a;
    ctx.eval_b = eval_b;
    ctx.has_b = input.packaging.has_variant_b;
    ctx.intrinsic_quality = intrinsic_quality;
    ctx.trend_boost = trend_boost;
    ctx.consistency_mult = consistency_mult;
    ctx.publish_hour_mult = publish_hour_mult;
    ctx.collab_bonus_impressions = collab_bonus;
    ctx.ad_annoyance = ad_annoyance;
    ctx.sketchy_level = sketchy_level;
    ctx.current_viral_score = 0.46f;
    ctx.locked_variant = ctx.has_b ? 0 : 'A';
    live_ctx_ = std::move(ctx);

    push_event(EventType::VideoPublished, "Опубликован ролик «" + videos_.back().title + "».");
    return true;
}

const VideoWavePoint* Engine::step_live_video_hour() {
    if (!live_ctx_.has_value()) return nullptr;
    auto& ctx = *live_ctx_;
    auto& vid = videos_[ctx.video_index];
    const int hour = vid.simulated_hours + 1;

    char active_var = 'A';
    if (ctx.has_b) {
        if (hour <= 4) {
            active_var = (hour % 2 == 1) ? 'A' : 'B';
        } else {
            if (ctx.locked_variant == 0) {
                const float ctr_a = ctx.a_impr > 0 ? static_cast<float>(ctx.a_views) / static_cast<float>(ctx.a_impr) : 0.0f;
                const float ctr_b = ctx.b_impr > 0 ? static_cast<float>(ctx.b_views) / static_cast<float>(ctx.b_impr) : 0.0f;
                ctx.locked_variant = (ctr_b > ctr_a) ? 'B' : 'A';
                if (ctx.locked_variant == 'B' && !ctx.input.packaging.variant_b.title.empty()) {
                    vid.title = ctx.input.packaging.variant_b.title;
                }
            }
            active_var = ctx.locked_variant;
        }
    }

    const auto& pack = (active_var == 'B' && ctx.has_b) ? ctx.eval_b : ctx.eval_a;
    const auto [wave_idx, impressions] = recommender.plan_hourly_impressions(
        hour,
        audience.subscribers(),
        ctx.current_viral_score,
        ctx.publish_hour_mult,
        world_pool_multiplier_,
        ctx.collab_bonus_impressions,
        prng);

    WaveEvalParams wp{};
    wp.wave_index = wave_idx;
    wp.impressions = impressions;
    wp.video_niche = vid.niche;
    wp.channel_origin_niche = creator_system.creator.origin_niche;
    wp.current_day = static_cast<std::uint16_t>(std::min<std::int32_t>(65000, clock.current_day()));
    wp.packaging_appeal = pack.appeal_score;
    wp.clickbait_level = pack.clickbait_risk;
    wp.intrinsic_quality = ctx.intrinsic_quality;
    wp.retention_curve = ctx.edit_res.retention_curve;
    wp.trend_ctr_boost = ctx.trend_boost;
    wp.ad_annoyance = ctx.ad_annoyance;

    const auto rep = audience.evaluate_wave(wp, prng);

    if (active_var == 'A') {
        ctx.a_impr += rep.impressions;
        ctx.a_views += rep.views;
    } else {
        ctx.b_impr += rep.impressions;
        ctx.b_views += rep.views;
    }

    const std::uint8_t prev_max_wave = vid.max_wave_reached;
    const std::int64_t prev_views = vid.total_views;

    vid.simulated_hours = hour;
    if (wave_idx > vid.max_wave_reached) {
        vid.max_wave_reached = wave_idx;
    }
    vid.total_impressions += rep.impressions;
    vid.total_views += rep.views;
    vid.avg_ctr = vid.total_impressions > 0
                      ? static_cast<float>(static_cast<double>(vid.total_views) / static_cast<double>(vid.total_impressions))
                      : 0.0f;
    vid.avg_retention = vid.total_views > 0
                            ? static_cast<float>((static_cast<double>(vid.avg_retention) * static_cast<double>(prev_views) +
                                                  static_cast<double>(rep.avg_watch_pct) * static_cast<double>(rep.views)) /
                                                 static_cast<double>(vid.total_views))
                            : rep.avg_watch_pct;

    vid.likes += rep.likes;
    vid.dislikes += rep.dislikes;
    vid.comments_count += rep.comments;
    vid.net_subscribers += (rep.sub_gains - rep.sub_losses);
    vid.retention_curve = rep.observed_retention_curve;

    if (creator_system.stage_spec().monetization_unlocked && rep.views > 0) {
        const double cpm = static_cast<double>(niche_spec(vid.niche).base_cpm_usd);
        const double ret_bonus = 0.65 + static_cast<double>(rep.avg_watch_pct) * 0.85;
        const double rev = (static_cast<double>(rep.views) / 1000.0) * cpm * ret_bonus;
        vid.adsense_revenue_usd += rev;
        creator_system.creator.cash_usd += rev;
    }

    ViralScoreInput vs{};
    vs.niche = vid.niche;
    vs.wave_index = wave_idx;
    vs.observed_ctr = vid.avg_ctr;
    vs.observed_retention = vid.avg_retention;
    vs.views = vid.total_views;
    vs.likes = vid.likes;
    vs.dislikes = vid.dislikes;
    vs.comments = vid.comments_count;
    vs.sub_gains = std::max<std::int64_t>(0, vid.net_subscribers);
    vs.clickbait_level = pack.clickbait_risk;
    vs.intrinsic_quality = ctx.intrinsic_quality;
    vs.sketchy_sponsor_level = ctx.sketchy_level;
    vs.trend_boost = ctx.trend_boost;
    vs.consistency_multiplier = ctx.consistency_mult;
    vs.world_pool_multiplier = world_pool_multiplier_;
    ctx.current_viral_score = recommender.compute_viral_score(vs);

    VideoWavePoint pt{};
    pt.hour_after_publish = hour;
    pt.wave_index = wave_idx;
    pt.impressions = rep.impressions;
    pt.views = rep.views;
    pt.cumulative_views = vid.total_views;
    pt.ctr = rep.ctr;
    pt.avg_watch_pct = rep.avg_watch_pct;
    pt.likes = rep.likes;
    pt.dislikes = rep.dislikes;
    pt.comments = rep.comments;
    pt.sub_gains = rep.sub_gains;
    pt.sub_losses = rep.sub_losses;
    pt.viral_score = ctx.current_viral_score;
    pt.active_variant = active_var;
    vid.hourly_timeline.push_back(pt);

    if (wave_idx > prev_max_wave && wave_idx >= 2) {
        push_event(EventType::WaveBreakthrough,
                   "Прорыв на Волну " + std::to_string(wave_idx) + " алгоритма!",
                   wave_idx);
    }

    advance_hours_internal(1);

    if (creator_system.sync_stage(audience.subscribers())) {
        push_event(EventType::StageUpgraded,
                   std::string("Новый этап карьеры: «") + creator_system.stage_spec().title_ru + "»!",
                   4);
    }

    const VideoWavePoint* out_ptr = &vid.hourly_timeline.back();
    if (hour >= 48) {
        finalize_live_video(ctx);
        live_ctx_.reset();
    }
    return out_ptr;
}

bool Engine::publish_and_simulate_full_video(const VideoReleaseInput& input, std::string& out_error) {
    if (!begin_video_release(input, out_error)) return false;
    while (live_ctx_.has_value()) {
        step_live_video_hour();
    }
    return true;
}

void Engine::finalize_live_video(LiveContext& ctx) {
    auto& vid = videos_[ctx.video_index];
    const char winner = ctx.locked_variant != 0 ? ctx.locked_variant : 'A';
    const auto& chosen = (winner == 'B' && ctx.has_b) ? ctx.eval_b : ctx.eval_a;

    const float cb_gap = chosen.clickbait_risk - ctx.intrinsic_quality;
    if (cb_gap > 0.28f) {
        if (recommender.adjust_trust(-cb_gap * 0.18f)) {
            push_event(EventType::ShadowbanChanged,
                       recommender.is_shadowbanned()
                           ? "ШАДОУБАН: резкий обвал траста из-за кликбейта!"
                           : "Шадоубан снят.");
        }
    }

    const float ctr_a = ctx.a_impr > 0 ? static_cast<float>(ctx.a_views) / static_cast<float>(ctx.a_impr) : vid.avg_ctr;
    const float ctr_b = (ctx.has_b && ctx.b_impr > 0) ? static_cast<float>(ctx.b_views) / static_cast<float>(ctx.b_impr) : 0.0f;

    vid.is_live_simulating = false;
    vid.top_comments = comments.generate_for_video(
        vid,
        vid.niche != creator_system.creator.origin_niche,
        ctx.sketchy_level > 0.40f,
        cb_gap,
        ctx.edit_res.weakest_decile_index,
        prng,
        6);

    vid.post_mortem = build_video_post_mortem(
        vid,
        recommender.regime(),
        recommender.is_shadowbanned(),
        chosen.clickbait_risk,
        ctx.intrinsic_quality,
        ctx.edit_res.weakest_decile_index,
        winner,
        ctr_a,
        ctr_b,
        ctx.has_b);

    push_event(EventType::VideoCompleted,
               "Релиз «" + vid.title + "» завершил 48ч цикл (" + std::to_string(vid.total_views) + " просм.).",
               vid.max_wave_reached);
}

std::vector<ScheduledConsequence> Engine::advance_days(int days) {
    if (days <= 0) return {};
    const std::int32_t prev_day = clock.current_day();
    auto due = advance_hours_internal(static_cast<std::int64_t>(days) * 24);
    const std::int32_t next_day = clock.current_day();

    audience.advance_days(days, static_cast<std::uint16_t>(std::min<std::int32_t>(65000, next_day)));
    trends.advance_to_day(next_day, prng);

    const auto quitters = creator_system.advance_days(days, prev_day, next_day, prng);
    for (const auto& q : quitters) {
        push_event(EventType::TeamMemberQuit, "Из команды ушёл сотрудник: " + q.name_ru);
    }

    if (prng.chance(std::min(0.45f, static_cast<float>(days) * 0.035f))) {
        trigger_algorithm_update();
    }
    return due;
}

StreamMiniGameResult Engine::run_live_stream(const StreamMiniGameInput& input) {
    const auto skills = creator_system.effective_skills();
    const auto res = solve_stream_minigame(input, audience.subscribers(), skills);

    creator_system.creator.cash_usd += res.donations_usd;
    creator_system.creator.energy = clamp_val(creator_system.creator.energy - res.energy_spent, 0.0f, 100.0f);
    creator_system.creator.burnout = clamp_val(creator_system.creator.burnout + res.burnout_added, 0.0f, 100.0f);

    audience.apply_global_impulse(0.03f, 0.0f, res.new_subscribers);
    if (creator_system.sync_stage(audience.subscribers())) {
        push_event(EventType::StageUpgraded,
                   std::string("Новый этап карьеры: «") + creator_system.stage_spec().title_ru + "»!",
                   4);
    }
    advance_hours_internal(input.duration_hours);
    push_event(EventType::StreamFinished,
               "Стрим завершён: пик " + std::to_string(res.peak_concurrent_viewers) +
                   " зрителей, +" + std::to_string(res.new_subscribers) + " подп.");
    return res;
}

const AlgorithmRegime& Engine::trigger_algorithm_update() {
    const auto& next = recommender.mutate_regime(prng);
    push_event(EventType::AlgorithmMutated,
               std::string("Обновление алгоритма: «") + next.title_ru + "»!");
    return next;
}

bool Engine::choose_dilemma_option(const std::string& dilemma_id, const std::string& choice_id) {
    auto res = dilemmas.resolve_choice(dilemma_id, choice_id, clock.total_hours(), prng);
    if (res.choice.id.empty()) return false;

    creator_system.creator.cash_usd = std::max(0.0, creator_system.creator.cash_usd + res.choice.immediate_cash_usd);
    creator_system.creator.energy = clamp_val(creator_system.creator.energy + res.choice.immediate_energy_delta, 0.0f, 100.0f);
    creator_system.creator.reputation = clamp_val(creator_system.creator.reputation + res.choice.immediate_reputation_delta, 0.0f, 100.0f);

    if (res.choice.immediate_trust_delta != 0.0f) {
        recommender.adjust_trust(res.choice.immediate_trust_delta);
    }
    if (res.has_scheduled) {
        clock.schedule(res.scheduled);
    }
    return true;
}

std::vector<ScheduledConsequence> Engine::advance_hours_internal(std::int64_t hours) {
    auto due = clock.advance_hours(hours);
    for (const auto& c : due) {
        apply_consequence(c);
    }
    return due;
}

void Engine::apply_consequence(const ScheduledConsequence& c) {
    creator_system.creator.cash_usd = std::max(0.0, creator_system.creator.cash_usd + c.cash_delta_usd);
    creator_system.creator.reputation = clamp_val(creator_system.creator.reputation + c.reputation_delta, 0.0f, 100.0f);
    creator_system.creator.burnout = clamp_val(creator_system.creator.burnout + c.burnout_delta, 0.0f, 100.0f);
    if (recommender.adjust_trust(c.trust_delta)) {
        push_event(EventType::ShadowbanChanged,
                   recommender.is_shadowbanned()
                       ? "ШАДОУБАН после скандала!"
                       : "Шадоубан снят.");
    }
    audience.apply_global_impulse(c.loyalty_delta, c.nostalgia_delta, 0);
    push_event(EventType::ConsequenceTriggered, "Последствие: " + c.headline_ru);
}

} // namespace simul::core
