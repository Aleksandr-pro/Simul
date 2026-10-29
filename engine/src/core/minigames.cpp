#include "simul/core/minigames.hpp"
#include "simul/core/math_utils.hpp"
#include <cmath>
#include <cstdio>

namespace simul::core {

namespace {

std::size_t utf8_codepoint_len(const std::string& s) noexcept {
    std::size_t count = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

std::string fmt_f1(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

} // namespace

EditingMiniGameResult solve_editing_minigame(
    const EditingMiniGameInput& input,
    const CreatorSkills& skills,
    int equipment_tier,
    float burnout) noexcept {
    EditingMiniGameResult res{};
    const float edit_bonus = (skills.editing / 100.0f) * 0.20f;
    const float script_bonus = (skills.scriptwriting / 100.0f) * 0.14f;
    const float equip_bonus = (static_cast<float>(equipment_tier - 1) / 4.0f) * 0.10f;
    const float burnout_pen = burnout > 65.0f ? ((burnout - 65.0f) / 35.0f) * 0.22f : 0.0f;

    const float hook = clamp_val(input.hook_strength, 0.0f, 1.0f);
    const float pacing = clamp_val(input.pacing_tightness, 0.0f, 1.0f);
    const float payoff = clamp_val(input.payoff_strength, 0.0f, 1.0f);

    const int ad_decile = input.ad_placement_ratio >= 0.0f
                              ? clamp_val(static_cast<int>(input.ad_placement_ratio * 10.0f), 0, 9)
                              : -1;

    float weakest_val = 2.0f;
    int weakest_idx = 0;

    for (int d = 0; d < 10; ++d) {
        float base = 0.0f;
        if (d == 0) {
            base = 0.42f + hook * 0.46f + edit_bonus * 0.60f;
        } else if (d <= 6) {
            const float sag = (d == 4 || d == 5) ? 0.05f : 0.02f;
            base = 0.40f + pacing * 0.40f + script_bonus + edit_bonus * 0.50f - sag;
        } else {
            base = 0.44f + payoff * 0.42f + script_bonus * 0.80f;
        }

        if (d == ad_decile) {
            base -= (d <= 1) ? 0.22f : 0.08f;
        }

        const float step_v = clamp_val(base + equip_bonus - burnout_pen, 0.12f, 0.97f);
        res.retention_curve[static_cast<std::size_t>(d)] = step_v;
        if (step_v < weakest_val) {
            weakest_val = step_v;
            weakest_idx = d;
        }
    }

    res.weakest_decile_index = weakest_idx;
    res.overall_edit_score = clamp_val(
        hook * 0.36f + pacing * 0.38f + payoff * 0.26f + edit_bonus * 0.50f - burnout_pen,
        0.05f,
        1.0f);
    return res;
}

PackagingEvaluation evaluate_packaging_variant(
    const PackagingVariant& variant,
    NicheId niche,
    float analytics_skill) noexcept {
    const std::size_t len = utf8_codepoint_len(variant.title);
    float len_factor = 1.0f;
    if (len < 10) len_factor = 0.76f;
    else if (len > 75) len_factor = 0.84f;
    else if (len >= 22 && len <= 62) len_factor = 1.08f;

    const float contrast = clamp_val(variant.visual_contrast, 0.0f, 1.0f);
    const float curiosity = clamp_val(variant.curiosity_gap, 0.0f, 1.0f);
    const float clickbait = clamp_val(variant.clickbait_level, 0.0f, 1.0f);
    const float skill_edge = (analytics_skill / 100.0f) * 0.10f;

    PackagingEvaluation out{};
    out.appeal_score = clamp_val(
        (contrast * 0.42f + curiosity * 0.40f + clickbait * 0.24f + skill_edge) * len_factor,
        0.08f,
        1.0f);
    out.predicted_ctr = clamp_val(
        niche_spec(niche).baseline_ctr * (0.52f + 1.35f * out.appeal_score),
        0.012f,
        0.24f);
    out.clickbait_risk = clickbait;
    return out;
}

StreamMiniGameResult solve_stream_minigame(
    const StreamMiniGameInput& input,
    std::int64_t subscribers,
    const CreatorSkills& skills) noexcept {
    StreamMiniGameResult res{};
    const int hours = clamp_val(input.duration_hours, 1, 4);
    const float charisma_factor = 0.75f + (skills.charisma / 100.0f) * 0.75f;
    const float quality = clamp_val(
        input.chat_responsiveness * 0.42f +
            input.hype_moments * 0.38f +
            input.moderation_accuracy * 0.20f,
        0.10f,
        1.0f);

    const double base_conc = 12.0 + std::pow(static_cast<double>(std::max<std::int64_t>(1, subscribers)), 0.68) * 0.55;
    res.peak_concurrent_viewers = std::max<std::int64_t>(
        6,
        std::llround(base_conc * static_cast<double>(quality * charisma_factor) * (0.85 + hours * 0.10)));
    res.donations_usd = static_cast<double>(res.peak_concurrent_viewers) * hours * 0.16 *
                        (0.5 + static_cast<double>(input.chat_responsiveness));
    res.new_subscribers = std::llround(
        static_cast<double>(res.peak_concurrent_viewers) * 0.085 * static_cast<double>(quality) * hours * 0.5);
    res.energy_spent = std::min(85.0f, static_cast<float>(hours) * 16.0f);
    res.burnout_added = static_cast<float>(hours) * 2.8f + (1.0f - input.moderation_accuracy) * 4.5f;
    return res;
}

VideoPostMortem build_video_post_mortem(
    const PublishedVideoRecord& video,
    const AlgorithmRegime& regime,
    bool is_shadowbanned,
    float clickbait_level,
    float intrinsic_quality,
    int weakest_decile_index,
    char winning_variant,
    float variant_a_ctr,
    float variant_b_ctr,
    bool had_variant_b) {
    VideoPostMortem pm{};
    pm.ready = true;
    pm.max_wave_reached = video.max_wave_reached;
    pm.winning_variant = winning_variant;

    const auto& nspec = niche_spec(video.niche);
    const float ctr_ratio = video.avg_ctr / std::max(0.01f, nspec.baseline_ctr);

    if (ctr_ratio >= 1.20f) {
        pm.factors.push_back({
            FactorKind::Positive,
            "Сильная упаковка (CTR " + fmt_f1(video.avg_ctr * 100.0f) + "%)",
            "Превью пробило норму ниши (" + fmt_f1(nspec.baseline_ctr * 100.0f) + "%) и ускорило проход первых волн."});
    } else if (ctr_ratio < 0.85f) {
        pm.factors.push_back({
            FactorKind::Negative,
            "Низкий CTR (" + fmt_f1(video.avg_ctr * 100.0f) + "%)",
            "Зрители пролистывали обложку в ленте. Усильте контраст кадра и интригу заголовка."});
    } else {
        pm.factors.push_back({
            FactorKind::Neutral,
            "Рабочий CTR (" + fmt_f1(video.avg_ctr * 100.0f) + "%)",
            "Кликабельность держится на среднем уровне ниши."});
    }

    const float ret_ratio = video.avg_retention / std::max(0.10f, nspec.baseline_retention);
    const int drop_lo = weakest_decile_index * 10;
    const int drop_hi = drop_lo + 10;

    if (ret_ratio >= 1.12f) {
        pm.factors.push_back({
            FactorKind::Positive,
            "Отличное удержание (" + fmt_f1(video.avg_retention * 100.0f) + "%)",
            std::string("Плотный монтаж дал буст в текущей редакции алгоритма «") + regime.title_ru + "»."});
    } else if (ret_ratio < 0.86f) {
        pm.factors.push_back({
            FactorKind::Negative,
            "Отвал зрителей (" + fmt_f1(video.avg_retention * 100.0f) + "%)",
            "Просадка на отрезке " + std::to_string(drop_lo) + "%-" + std::to_string(drop_hi) + "% остановила волны."});
    }

    if (clickbait_level - intrinsic_quality > 0.25f) {
        pm.factors.push_back({
            FactorKind::Negative,
            "Штраф за кликбейт",
            "Громкая обложка обманула ожидания: выросли дизлайки и просел траст."});
    }

    if (had_variant_b && variant_a_ctr > 0.0001f && variant_b_ctr > 0.0001f) {
        const float best = std::max(variant_a_ctr, variant_b_ctr);
        const float worst = std::min(variant_a_ctr, variant_b_ctr);
        pm.ab_test_lift_pct = ((best - worst) / worst) * 100.0f;
        if (pm.ab_test_lift_pct >= 6.0f) {
            pm.factors.push_back({
                FactorKind::Positive,
                std::string("A/B-тест выбрал вариант ") + winning_variant,
                "Автовыбор лучшей обложки принёс +" + fmt_f1(pm.ab_test_lift_pct) + "% кликов."});
        }
    }

    if (is_shadowbanned) {
        pm.factors.push_back({
            FactorKind::Negative,
            "Шадоубан активен",
            "Траст канала ниже 0.35 — алгоритм заблокировал выход ролика за пределы Волны 0."});
    }

    switch (video.max_wave_reached) {
        case 0:
            pm.verdict_title_ru = "Волна 0: Тестовая группа";
            pm.summary_ru = "Стартовых сигналов не хватило для выхода за пределы ядра подписчиков.";
            break;
        case 1:
            pm.verdict_title_ru = "Волна 1: Узкий охват ниши";
            pm.summary_ru = "Ролик показали похожим зрителям, но для широкой ленты не хватило связки CTR + досмотр.";
            break;
        case 2:
            pm.verdict_title_ru = "Волна 2: Выход в рекомендации";
            pm.summary_ru = "Уверенный рабочий выпуск: стабильный приток целевой аудитории ниши.";
            break;
        case 3:
            pm.verdict_title_ru = "Волна 3: Вкладка «В тренде»!";
            pm.summary_ru = "Взрывной отклик первых часов вывел видео на широкую кросс-аудиторию!";
            break;
        default:
            pm.verdict_title_ru = "Волна 4: МЕГА-ВИРАЛ ПЛАТФОРМЫ!";
            pm.summary_ru = "Исторический хит! Все метрики идеально попали в алгоритм главной ленты.";
            break;
    }

    return pm;
}

} // namespace simul::core
