#include "simul/core/recommender.hpp"
#include "simul/core/math_utils.hpp"
#include <cmath>

namespace simul::core {

const AlgorithmRegime& RecommendationEngine::mutate_regime(SeededPrng& prng) noexcept {
    const int cur = static_cast<int>(regime_id_);
    int next = prng.int_range(0, 3);
    if (next >= cur) ++next;
    regime_id_ = static_cast<AlgorithmRegimeId>(next);
    return regime();
}

bool RecommendationEngine::adjust_trust(float delta) noexcept {
    const bool was_banned = is_shadowbanned();
    trust_score_ = clamp_val(trust_score_ + delta, 0.08f, 1.15f);
    return was_banned != is_shadowbanned();
}

float RecommendationEngine::register_upload_on_day(std::int32_t current_day) noexcept {
    if (last_publish_day_ < 0) {
        last_publish_day_ = current_day;
        streak_ = 1;
        return 1.0f;
    }
    const int gap = std::max(0, static_cast<int>(current_day - last_publish_day_));
    last_publish_day_ = current_day;

    if (gap >= 2 && gap <= 7) {
        streak_ = std::min(12, streak_ + 1);
        adjust_trust(0.025f);
        return clamp_val(1.0f + static_cast<float>(streak_) * 0.012f, 1.0f, 1.12f);
    }
    if (gap < 2) return 0.93f;
    if (gap > 16) {
        streak_ = 0;
        return 0.84f;
    }
    streak_ = std::max(1, streak_ - 1);
    return 0.97f;
}

float RecommendationEngine::compute_publish_hour_multiplier(int hour_of_day) const noexcept {
    const int h = ((hour_of_day % 24) + 24) % 24;
    if (h >= 17 && h <= 21) return 1.12f;
    if (h >= 12 && h <= 16) return 1.04f;
    if (h >= 2 && h <= 6) return 0.86f;
    return 0.96f;
}

float RecommendationEngine::compute_viral_score(const ViralScoreInput& in) const noexcept {
    const auto& reg = regime();
    const auto& nspec = niche_spec(in.niche);

    const float wave_ctr_bench = nspec.baseline_ctr * (1.0f - static_cast<float>(in.wave_index) * 0.065f);
    const float wave_ret_bench = nspec.baseline_retention * (1.0f - static_cast<float>(in.wave_index) * 0.035f);

    const float ctr_score = sigmoid_f32((in.observed_ctr - wave_ctr_bench) / 0.026f);
    const float ret_score = clamp_val(in.observed_retention / std::max(0.20f, wave_ret_bench), 0.0f, 1.28f);

    const double safe_views = static_cast<double>(std::max<std::int64_t>(1, in.views));
    const float er = static_cast<float>(
        (static_cast<double>(in.likes) +
         static_cast<double>(in.comments) * 2.5 +
         static_cast<double>(in.sub_gains) * 4.0 -
         static_cast<double>(in.dislikes) * 3.0) / safe_views);
    const float vel_score = sigmoid_f32((er - 0.042f) / 0.025f);

    const float exp_gap = in.intrinsic_quality - in.clickbait_level * 0.90f;
    const float cb_penalty = exp_gap < -0.15f
                                 ? std::fabs(exp_gap + 0.15f) * 0.55f * reg.clickbait_penalty_scale
                                 : 0.0f;
    const float sp_penalty = in.sketchy_sponsor_level * 0.28f * reg.ad_safety_sensitivity;

    const float sat_score = clamp_val(
        0.55f * in.intrinsic_quality + 0.45f * in.observed_retention - cb_penalty - sp_penalty,
        0.05f,
        1.15f);

    const float weighted =
        reg.ctr_weight * ctr_score +
        reg.retention_weight * ret_score +
        reg.velocity_weight * vel_score +
        reg.satisfaction_weight * sat_score;

    const float trust_mult = is_shadowbanned() ? 0.24f : clamp_val(trust_score_, 0.35f, 1.15f);
    return clamp_val(
        weighted * trust_mult * in.consistency_multiplier * in.trend_boost * in.world_pool_multiplier,
        0.02f,
        1.25f);
}

std::pair<std::uint8_t, std::int64_t> RecommendationEngine::plan_hourly_impressions(
    int hour_after_publish,
    std::int64_t subscribers,
    float current_viral_score,
    float publish_hour_mult,
    float world_pool_mult,
    std::int64_t collab_bonus_impressions,
    SeededPrng& prng) const noexcept {
    std::uint8_t max_eligible = 0;
    if (hour_after_publish >= 35) max_eligible = 4;
    else if (hour_after_publish >= 19) max_eligible = 3;
    else if (hour_after_publish >= 9) max_eligible = 2;
    else if (hour_after_publish >= 3) max_eligible = 1;

    if (is_shadowbanned()) max_eligible = 0;

    std::uint8_t unlocked = 0;
    for (std::uint8_t w = 1; w <= max_eligible; ++w) {
        if (current_viral_score >= kWaveThresholds[w]) {
            unlocked = w;
        } else {
            break;
        }
    }

    const double subs = static_cast<double>(std::max<std::int64_t>(0, subscribers));
    const double collab = static_cast<double>(std::max<std::int64_t>(0, collab_bonus_impressions));
    const std::array<double, 5> pools{
        95.0 + subs * 0.16 + collab * 0.25,
        340.0 + subs * 0.24 + collab * 0.35,
        1450.0 + subs * 0.45 + collab * 0.20,
        7800.0 + subs * 0.95,
        36000.0 + subs * 2.10,
    };

    double base = pools[unlocked];
    static constexpr std::array<int, 5> kWaveStartHours{0, 3, 9, 19, 35};

    if (unlocked < max_eligible) {
        const int stall_h = std::max(1, hour_after_publish - kWaveStartHours[unlocked]);
        base *= std::pow(0.86, stall_h);
    } else {
        base *= (0.85 + std::max(0.0, static_cast<double>(current_viral_score) - 0.45) * 1.45);
    }

    if (hour_after_publish <= 4) base *= static_cast<double>(publish_hour_mult);
    if (is_shadowbanned()) base *= 0.18;

    const double jitter = static_cast<double>(prng.range(0.91f, 1.09f));
    const std::int64_t impressions = std::max<std::int64_t>(
        8, std::llround(base * static_cast<double>(world_pool_mult) * jitter));

    return {unlocked, impressions};
}

} // namespace simul::core
