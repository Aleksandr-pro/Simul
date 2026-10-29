#pragma once

#include "simul/core/audience_pool.hpp"
#include "simul/core/minigames.hpp"
#include "simul/core/narrative.hpp"
#include "simul/core/prng.hpp"
#include "simul/core/recommender.hpp"
#include "simul/core/timeline.hpp"
#include "simul/core/types.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace simul::core {

struct EngineConfig {
    std::uint32_t seed = 20261029u;
    std::string handle = "@viral_pioneer";
    std::string display_name = "ВИРАЛ Студия";
    NicheId origin_niche = NicheId::Tech;
    double starting_cash_usd = 700.0;
    std::size_t audience_capacity = 50'000;
    std::size_t initial_platform_agents = 18'000;
};

class Engine {
public:
    explicit Engine(const EngineConfig& cfg = {});

    std::uint32_t initial_seed() const noexcept { return initial_seed_; }
    std::int64_t subscribers() const noexcept { return audience.subscribers(); }
    std::int64_t total_views() const noexcept;
    bool is_live_video_running() const noexcept { return live_ctx_.has_value(); }
    float world_pool_multiplier() const noexcept { return world_pool_multiplier_; }
    void set_world_pool_multiplier(float m) noexcept;

    const std::vector<PublishedVideoRecord>& published_videos() const noexcept { return videos_; }
    const std::vector<EngineEvent>& event_log() const noexcept { return events_; }

    /** Запускает 48-часовое окно живого релиза (почасовая анимация в UI) */
    bool begin_video_release(const VideoReleaseInput& input, std::string& out_error);

    /** Продвигает активный релиз ровно на 1 час (1..48) */
    const VideoWavePoint* step_live_video_hour();

    /** Мгновенный полный расчёт всех 48 часов релиза */
    bool publish_and_simulate_full_video(const VideoReleaseInput& input, std::string& out_error);

    /** Межрелизная перемотка дней (восстановление энергии, старение трендов, зарплаты, отложенные последствия) */
    std::vector<ScheduledConsequence> advance_days(int days);

    /** Стрим-сессия */
    StreamMiniGameResult run_live_stream(const StreamMiniGameInput& input);

    /** Принудительная смена редакции алгоритма */
    const AlgorithmRegime& trigger_algorithm_update();

    /** Выбор в нарративной дилемме */
    bool choose_dilemma_option(const std::string& dilemma_id, const std::string& choice_id);

    // Прямой доступ к модулям ядра
    SeededPrng prng;
    SimulationClock clock;
    AudiencePool audience;
    RecommendationEngine recommender;
    TrendSystem trends;
    DilemmaEngine dilemmas;
    CreatorAndTeamSystem creator_system;
    HybridCommentGenerator comments;

    void replace_videos_raw(std::vector<PublishedVideoRecord> vids) {
        live_ctx_.reset();
        videos_ = std::move(vids);
    }

private:
    struct LiveContext {
        std::size_t video_index = 0;
        VideoReleaseInput input;
        EditingMiniGameResult edit_res;
        PackagingEvaluation eval_a;
        PackagingEvaluation eval_b;
        bool has_b = false;
        float intrinsic_quality = 0.5f;
        float trend_boost = 1.0f;
        float consistency_mult = 1.0f;
        float publish_hour_mult = 1.0f;
        std::int64_t collab_bonus_impressions = 0;
        float ad_annoyance = 0.0f;
        float sketchy_level = 0.0f;
        float current_viral_score = 0.46f;
        std::int64_t a_impr = 0;
        std::int64_t a_views = 0;
        std::int64_t b_impr = 0;
        std::int64_t b_views = 0;
        char locked_variant = 0;
    };

    std::vector<ScheduledConsequence> advance_hours_internal(std::int64_t hours);
    void apply_consequence(const ScheduledConsequence& c);
    void finalize_live_video(LiveContext& ctx);
    void push_event(EventType type, const std::string& msg, std::uint8_t wave = 0);

    std::uint32_t initial_seed_ = 20261029u;
    float world_pool_multiplier_ = 1.0f;
    std::vector<PublishedVideoRecord> videos_;
    std::optional<LiveContext> live_ctx_;
    std::vector<EngineEvent> events_;
};

} // namespace simul::core
