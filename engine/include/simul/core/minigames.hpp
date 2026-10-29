#pragma once

#include "simul/core/types.hpp"

namespace simul::core {

struct PackagingEvaluation {
    float appeal_score = 0.0f;
    float predicted_ctr = 0.0f;
    float clickbait_risk = 0.0f;
};

struct StreamMiniGameInput {
    int duration_hours = 2; // 1..4
    float chat_responsiveness = 0.80f;
    float moderation_accuracy = 0.85f;
    float hype_moments = 0.78f;
};

struct StreamMiniGameResult {
    std::int64_t peak_concurrent_viewers = 0;
    double donations_usd = 0.0;
    std::int64_t new_subscribers = 0;
    float energy_spent = 0.0f;
    float burnout_added = 0.0f;
};

EditingMiniGameResult solve_editing_minigame(
    const EditingMiniGameInput& input,
    const CreatorSkills& skills,
    int equipment_tier,
    float burnout) noexcept;

PackagingEvaluation evaluate_packaging_variant(
    const PackagingVariant& variant,
    NicheId niche,
    float analytics_skill) noexcept;

StreamMiniGameResult solve_stream_minigame(
    const StreamMiniGameInput& input,
    std::int64_t subscribers,
    const CreatorSkills& skills) noexcept;

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
    bool had_variant_b);

} // namespace simul::core
