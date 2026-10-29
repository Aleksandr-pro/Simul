#pragma once

#include "simul/core/prng.hpp"
#include "simul/core/types.hpp"
#include <array>
#include <cstdint>
#include <utility>

namespace simul::core {

inline constexpr std::array<AlgorithmRegime, 5> kAlgorithmRegimes{{
    {AlgorithmRegimeId::BalancedV1, "balanced_v1", "Баланс 1.0",
     "Платформа равномерно ценит CTR обложки и среднее удержание.",
     0.30f, 0.36f, 0.18f, 0.16f, 1.00f, 1.00f},
    {AlgorithmRegimeId::ClickbaitRush, "clickbait_rush", "Битва за клик",
     "Лента агрессивно продвигает обложки с высоким CTR в первые часы.",
     0.44f, 0.26f, 0.18f, 0.12f, 0.65f, 0.85f},
    {AlgorithmRegimeId::DeepRetention, "deep_retention", "Глубокий досмотр",
     "Алгоритм жёстко режет кликбейт и выводит в топ длинное удержание.",
     0.20f, 0.48f, 0.14f, 0.18f, 1.45f, 1.00f},
    {AlgorithmRegimeId::CommunitySpark, "community_spark", "Резонанс комьюнити",
     "Комментарии, лайки и подписки на первых волнах дают главный буст.",
     0.24f, 0.28f, 0.34f, 0.14f, 1.05f, 0.95f},
    {AlgorithmRegimeId::AdSafeGuard, "ad_safe_guard", "Щит брендов",
     "Сомнительная реклама и обман ожиданий стремительно роняют траст.",
     0.26f, 0.34f, 0.16f, 0.24f, 1.60f, 1.75f},
}};

inline constexpr std::array<float, 5> kWaveThresholds{0.0f, 0.44f, 0.56f, 0.67f, 0.78f};

struct ViralScoreInput {
    NicheId niche = NicheId::Tech;
    std::uint8_t wave_index = 0;
    float observed_ctr = 0.0f;
    float observed_retention = 0.0f;
    std::int64_t views = 0;
    std::int64_t likes = 0;
    std::int64_t dislikes = 0;
    std::int64_t comments = 0;
    std::int64_t sub_gains = 0;
    float clickbait_level = 0.0f;
    float intrinsic_quality = 0.5f;
    float sketchy_sponsor_level = 0.0f;
    float trend_boost = 1.0f;
    float consistency_multiplier = 1.0f;
    float world_pool_multiplier = 1.0f;
};

class RecommendationEngine {
public:
    const AlgorithmRegime& regime() const noexcept {
        return kAlgorithmRegimes[static_cast<std::size_t>(regime_id_)];
    }

    AlgorithmRegimeId regime_id() const noexcept { return regime_id_; }
    float trust_score() const noexcept { return trust_score_; }
    bool is_shadowbanned() const noexcept { return trust_score_ < 0.35f; }
    std::int32_t last_publish_day() const noexcept { return last_publish_day_; }
    int streak() const noexcept { return streak_; }

    void set_regime(AlgorithmRegimeId id) noexcept { regime_id_ = id; }
    const AlgorithmRegime& mutate_regime(SeededPrng& prng) noexcept;

    /** Возвращает true, если статус шадоубана изменился */
    bool adjust_trust(float delta) noexcept;

    float register_upload_on_day(std::int32_t current_day) noexcept;
    float compute_publish_hour_multiplier(int hour_of_day) const noexcept;
    float compute_viral_score(const ViralScoreInput& in) const noexcept;

    std::pair<std::uint8_t, std::int64_t> plan_hourly_impressions(
        int hour_after_publish,
        std::int64_t subscribers,
        float current_viral_score,
        float publish_hour_mult,
        float world_pool_mult,
        std::int64_t collab_bonus_impressions,
        SeededPrng& prng) const noexcept;

    void restore_raw(AlgorithmRegimeId id, float trust, std::int32_t last_day, int streak) noexcept {
        regime_id_ = id;
        trust_score_ = trust;
        last_publish_day_ = last_day;
        streak_ = streak;
    }

private:
    AlgorithmRegimeId regime_id_ = AlgorithmRegimeId::BalancedV1;
    float trust_score_ = 1.0f;
    std::int32_t last_publish_day_ = -1;
    int streak_ = 0;
};

} // namespace simul::core
