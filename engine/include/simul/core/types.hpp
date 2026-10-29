#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace simul::core {

enum class NicheId : std::uint8_t {
    Gaming = 0,
    Tech = 1,
    Lifestyle = 2,
    Edutainment = 3,
    Entertainment = 4,
};

inline constexpr std::size_t kNicheCount = 5;

struct NicheSpec {
    NicheId id;
    const char* code;
    const char* title_ru;
    float base_cpm_usd;
    float baseline_ctr;
    float baseline_retention;
    float fatigue_rate;
};

inline constexpr std::array<NicheSpec, kNicheCount> kNicheSpecs{{
    {NicheId::Gaming, "gaming", "Гейминг и стримы", 2.20f, 0.068f, 0.48f, 0.045f},
    {NicheId::Tech, "tech", "Техно и гаджеты", 6.40f, 0.058f, 0.52f, 0.032f},
    {NicheId::Lifestyle, "lifestyle", "Лайфстайл и влоги", 3.40f, 0.062f, 0.45f, 0.038f},
    {NicheId::Edutainment, "edutainment", "Научпоп и разборы", 5.10f, 0.055f, 0.56f, 0.025f},
    {NicheId::Entertainment, "entertainment", "Шоу и челленджи", 3.90f, 0.082f, 0.44f, 0.055f},
}};

inline constexpr const NicheSpec& niche_spec(NicheId id) noexcept {
    const auto idx = static_cast<std::size_t>(id);
    return kNicheSpecs[idx < kNicheCount ? idx : 0];
}

enum class ViewerSegment : std::uint8_t {
    Casual = 0,
    Subscriber = 1,
    CoreFan = 2,
    Veteran = 3,
    Critic = 4,
    Hater = 5,
    BrandScout = 6,
};

inline constexpr std::size_t kSegmentCount = 7;

inline constexpr const char* segment_label_ru(ViewerSegment seg) noexcept {
    switch (seg) {
        case ViewerSegment::Casual: return "Случайные";
        case ViewerSegment::Subscriber: return "Подписчики";
        case ViewerSegment::CoreFan: return "Ядро фанатов";
        case ViewerSegment::Veteran: return "Олды";
        case ViewerSegment::Critic: return "Критики";
        case ViewerSegment::Hater: return "Хейтеры";
        case ViewerSegment::BrandScout: return "Скауты брендов";
    }
    return "Зрители";
}

enum class ChannelStage : std::uint8_t {
    Novice = 0,
    Growing = 1,
    Known = 2,
    Star = 3,
    Empire = 4,
};

struct StageSpec {
    ChannelStage id;
    const char* code;
    const char* title_ru;
    std::int64_t min_subscribers;
    bool monetization_unlocked;
    int team_slots;
    float sponsorship_multiplier;
};

inline constexpr std::array<StageSpec, 5> kStageSpecs{{
    {ChannelStage::Novice, "novice", "Новичок", 0, false, 0, 0.0f},
    {ChannelStage::Growing, "growing", "Растущий канал", 1'000, true, 2, 1.0f},
    {ChannelStage::Known, "known", "Известный блогер", 50'000, true, 4, 2.6f},
    {ChannelStage::Star, "star", "Звезда", 500'000, true, 6, 6.5f},
    {ChannelStage::Empire, "empire", "Империя", 5'000'000, true, 10, 15.0f},
}};

inline constexpr const StageSpec& resolve_stage_spec(std::int64_t subscribers) noexcept {
    const StageSpec* best = &kStageSpecs[0];
    for (const auto& s : kStageSpecs) {
        if (subscribers >= s.min_subscribers) {
            best = &s;
        }
    }
    return *best;
}

enum class AlgorithmRegimeId : std::uint8_t {
    BalancedV1 = 0,
    ClickbaitRush = 1,
    DeepRetention = 2,
    CommunitySpark = 3,
    AdSafeGuard = 4,
};

struct AlgorithmRegime {
    AlgorithmRegimeId id;
    const char* code;
    const char* title_ru;
    const char* public_hint_ru;
    float ctr_weight;
    float retention_weight;
    float velocity_weight;
    float satisfaction_weight;
    float clickbait_penalty_scale;
    float ad_safety_sensitivity;
};

enum class TrendPhase : std::uint8_t {
    Emerging = 0,
    Peaking = 1,
    Saturated = 2,
    Fading = 3,
};

inline constexpr const char* trend_phase_label_ru(TrendPhase p) noexcept {
    switch (p) {
        case TrendPhase::Emerging: return "Зарождается";
        case TrendPhase::Peaking: return "На пике!";
        case TrendPhase::Saturated: return "Перегрет";
        case TrendPhase::Fading: return "Угасает";
    }
    return "";
}

struct TrendCard {
    std::uint32_t id = 0;
    std::string title_ru;
    NicheId niche = NicheId::Tech;
    TrendPhase phase = TrendPhase::Emerging;
    float virality_boost = 1.25f;
    float controversy_risk = 0.1f;
    std::int32_t created_day = 1;
    std::int32_t expires_day = 12;
};

struct EditingMiniGameInput {
    float hook_strength = 0.75f;
    float pacing_tightness = 0.72f;
    float payoff_strength = 0.74f;
    float ad_placement_ratio = -1.0f; // < 0 если без рекламы
};

struct EditingMiniGameResult {
    float overall_edit_score = 0.0f;
    std::array<float, 10> retention_curve{};
    int weakest_decile_index = 0;
};

struct PackagingVariant {
    char id = 'A';
    std::string title;
    float visual_contrast = 0.75f;
    float curiosity_gap = 0.72f;
    float clickbait_level = 0.15f;
};

struct PackagingMiniGameInput {
    PackagingVariant variant_a;
    bool has_variant_b = false;
    PackagingVariant variant_b;
};

struct SponsorDeal {
    bool active = false;
    std::string brand_name;
    double payout_usd = 0.0;
    float sketchiness = 0.0f; // 0..1
};

struct VideoReleaseInput {
    std::string title;
    NicheId niche = NicheId::Tech;
    std::uint32_t trend_id = 0; // 0 если вне тренда
    double production_budget_usd = 100.0;
    int publish_hour_of_day = 19;
    SponsorDeal sponsor;
    std::int64_t collab_partner_subs = 0;
    float collab_synergy = 1.0f;
    EditingMiniGameInput editing;
    PackagingMiniGameInput packaging;
    bool delegated_to_team = false;
};

struct VideoWavePoint {
    int hour_after_publish = 1;
    std::uint8_t wave_index = 0;
    std::int64_t impressions = 0;
    std::int64_t views = 0;
    std::int64_t cumulative_views = 0;
    float ctr = 0.0f;
    float avg_watch_pct = 0.0f;
    std::int64_t likes = 0;
    std::int64_t dislikes = 0;
    std::int64_t comments = 0;
    std::int64_t sub_gains = 0;
    std::int64_t sub_losses = 0;
    float viral_score = 0.0f;
    char active_variant = 'A';
};

enum class FactorKind : std::uint8_t {
    Positive = 0,
    Neutral = 1,
    Negative = 2,
};

struct PostMortemFactor {
    FactorKind kind = FactorKind::Neutral;
    std::string label_ru;
    std::string detail_ru;
};

struct VideoPostMortem {
    bool ready = false;
    std::string verdict_title_ru;
    std::string summary_ru;
    std::uint8_t max_wave_reached = 0;
    char winning_variant = 'A';
    float ab_test_lift_pct = 0.0f;
    std::vector<PostMortemFactor> factors;
};

struct GeneratedComment {
    std::uint32_t id = 0;
    std::string author_handle;
    ViewerSegment segment = ViewerSegment::Casual;
    std::string text;
    std::int64_t likes = 0;
    bool is_toxic = false;
};

struct PublishedVideoRecord {
    std::uint32_t sequence_number = 0;
    std::string title;
    NicheId niche = NicheId::Tech;
    std::int32_t published_day = 1;
    int published_hour = 19;
    bool is_live_simulating = false;
    int simulated_hours = 0;
    std::uint8_t max_wave_reached = 0;
    std::int64_t total_impressions = 0;
    std::int64_t total_views = 0;
    float avg_ctr = 0.0f;
    float avg_retention = 0.0f;
    std::int64_t likes = 0;
    std::int64_t dislikes = 0;
    std::int64_t comments_count = 0;
    std::int64_t net_subscribers = 0;
    double adsense_revenue_usd = 0.0;
    double sponsor_revenue_usd = 0.0;
    double production_cost_usd = 0.0;
    std::array<float, 10> retention_curve{};
    std::vector<VideoWavePoint> hourly_timeline;
    std::vector<GeneratedComment> top_comments;
    VideoPostMortem post_mortem;
};

struct CreatorSkills {
    float scriptwriting = 18.0f;
    float editing = 16.0f;
    float charisma = 20.0f;
    float analytics = 14.0f;
};

struct CreatorState {
    std::string handle = "@viral_creator";
    std::string display_name = "Мой Канал";
    NicheId origin_niche = NicheId::Tech;
    NicheId current_niche = NicheId::Tech;
    int equipment_tier = 1;
    float energy = 100.0f;
    float burnout = 8.0f;
    float personal_life = 82.0f;
    float reputation = 50.0f;
    double cash_usd = 650.0;
    CreatorSkills skills;
};

enum class TeamRole : std::uint8_t {
    Editor = 0,
    Scriptwriter = 1,
    Cameraman = 2,
    Manager = 3,
};

struct TeamMember {
    std::uint32_t id = 0;
    std::string name_ru;
    TeamRole role = TeamRole::Editor;
    float skill = 60.0f;
    float morale = 84.0f;
    double monthly_salary_usd = 1200.0;
    float quit_risk = 0.04f;
};

struct ScheduledConsequence {
    std::uint32_t id = 0;
    std::string source_dilemma_id;
    std::int64_t trigger_at_hour = 0;
    std::string headline_ru;
    std::string description_ru;
    float trust_delta = 0.0f;
    float reputation_delta = 0.0f;
    double cash_delta_usd = 0.0;
    float loyalty_delta = 0.0f;
    float nostalgia_delta = 0.0f;
    float burnout_delta = 0.0f;
};

struct DilemmaChoice {
    std::string id;
    std::string label_ru;
    std::string preview_ru;
    double immediate_cash_usd = 0.0;
    float immediate_energy_delta = 0.0f;
    float immediate_reputation_delta = 0.0f;
    float immediate_trust_delta = 0.0f;
    bool has_delayed = false;
    int delay_days_min = 20;
    int delay_days_max = 45;
    float delayed_probability = 0.75f;
    ScheduledConsequence delayed_template;
};

struct DilemmaDefinition {
    std::string id;
    ChannelStage min_stage = ChannelStage::Growing;
    std::string title_ru;
    std::string situation_ru;
    std::vector<DilemmaChoice> choices;
};

struct AudienceSummary {
    std::size_t active_agents = 0;
    std::size_t capacity = 0;
    std::int64_t subscribers = 0;
    float avg_loyalty = 0.0f;
    float avg_fatigue = 0.0f;
    float avg_nostalgia = 0.0f;
    std::array<std::int64_t, kSegmentCount> segment_counts{};
};

enum class EventType : std::uint8_t {
    VideoPublished = 0,
    WaveBreakthrough = 1,
    VideoCompleted = 2,
    StageUpgraded = 3,
    AlgorithmMutated = 4,
    ConsequenceTriggered = 5,
    ShadowbanChanged = 6,
    TeamMemberQuit = 7,
    StreamFinished = 8,
};

struct EngineEvent {
    EventType type = EventType::VideoPublished;
    std::int32_t day = 1;
    int hour = 10;
    std::uint8_t wave_index = 0;
    std::string message_ru;
};

} // namespace simul::core
