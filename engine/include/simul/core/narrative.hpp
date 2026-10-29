#pragma once

#include "simul/core/prng.hpp"
#include "simul/core/types.hpp"
#include <functional>
#include <string>
#include <vector>

namespace simul::core {

class TrendSystem {
public:
    void seed_initial_trends(SeededPrng& prng, std::int32_t current_day);
    TrendCard spawn_trend(SeededPrng& prng, std::int32_t current_day);
    void advance_to_day(std::int32_t current_day, SeededPrng& prng);

    const std::vector<TrendCard>& active_trends() const noexcept { return trends_; }
    const TrendCard* find_by_id(std::uint32_t id) const noexcept;

    std::uint32_t next_id() const noexcept { return next_id_; }
    void restore_raw(std::vector<TrendCard> cards, std::uint32_t next_id) {
        trends_ = std::move(cards);
        next_id_ = next_id;
    }

private:
    std::vector<TrendCard> trends_;
    std::uint32_t next_id_ = 1;
};

class DilemmaEngine {
public:
    DilemmaEngine();

    std::vector<const DilemmaDefinition*> available_dilemmas(ChannelStage current_stage) const;

    struct Resolution {
        DilemmaChoice choice;
        bool has_scheduled = false;
        ScheduledConsequence scheduled;
    };

    Resolution resolve_choice(
        const std::string& dilemma_id,
        const std::string& choice_id,
        std::int64_t current_total_hours,
        SeededPrng& prng);

    const std::vector<std::string>& resolved_ids() const noexcept { return resolved_ids_; }
    std::uint32_t next_seq() const noexcept { return next_seq_; }

    void restore_raw(std::vector<std::string> resolved, std::uint32_t next_seq) {
        resolved_ids_ = std::move(resolved);
        next_seq_ = next_seq;
    }

private:
    bool is_resolved(const std::string& id) const noexcept;

    std::vector<DilemmaDefinition> catalog_;
    std::vector<std::string> resolved_ids_;
    std::uint32_t next_seq_ = 1;
};

class CreatorAndTeamSystem {
public:
    CreatorState creator;

    ChannelStage current_stage() const noexcept { return current_stage_; }
    const StageSpec& stage_spec() const noexcept {
        return kStageSpecs[static_cast<std::size_t>(current_stage_)];
    }
    const std::vector<TeamMember>& members() const noexcept { return team_; }

    CreatorSkills effective_skills() const noexcept;
    bool sync_stage(std::int64_t subscribers) noexcept;
    bool hire_member(TeamRole role, float skill, SeededPrng& prng, std::string& out_error);
    bool upgrade_equipment(double& out_cost, std::string& out_error);
    void apply_video_effort(bool delegated_to_team) noexcept;
    std::vector<TeamMember> advance_days(int days, std::int32_t prev_day, std::int32_t next_day, SeededPrng& prng);

    std::uint32_t next_member_id() const noexcept { return next_member_id_; }
    void restore_raw(CreatorState c, ChannelStage st, std::vector<TeamMember> m, std::uint32_t next_id) {
        creator = std::move(c);
        current_stage_ = st;
        team_ = std::move(m);
        next_member_id_ = next_id;
    }

private:
    ChannelStage current_stage_ = ChannelStage::Novice;
    std::vector<TeamMember> team_;
    std::uint32_t next_member_id_ = 1;
};

class HybridCommentGenerator {
public:
    using LLMBatchHook = std::function<void(const PublishedVideoRecord&, std::vector<GeneratedComment>&)>;

    void set_llm_hook(LLMBatchHook hook) { llm_hook_ = std::move(hook); }

    std::vector<GeneratedComment> generate_for_video(
        const PublishedVideoRecord& video,
        bool is_niche_shifted,
        bool has_sketchy_sponsor,
        float clickbait_gap,
        int weakest_decile_index,
        SeededPrng& prng,
        std::size_t count = 6);

private:
    std::uint32_t seq_ = 1;
    LLMBatchHook llm_hook_;
};

} // namespace simul::core
