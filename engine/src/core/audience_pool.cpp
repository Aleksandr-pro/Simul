#include "simul/core/audience_pool.hpp"
#include "simul/core/math_utils.hpp"
#include <cmath>

namespace simul::core {

AudiencePool::AudiencePool(std::size_t capacity)
    : segment(capacity < 512 ? 512 : capacity),
      subscribed(capacity < 512 ? 512 : capacity),
      primary_niche(capacity < 512 ? 512 : capacity),
      loyalty(capacity < 512 ? 512 : capacity),
      patience(capacity < 512 ? 512 : capacity),
      ad_tolerance(capacity < 512 ? 512 : capacity),
      nostalgia(capacity < 512 ? 512 : capacity),
      fatigue(capacity < 512 ? 512 : capacity),
      joined_day(capacity < 512 ? 512 : capacity),
      capacity_(capacity < 512 ? 512 : capacity) {}

void AudiencePool::seed_platform_population(
    SeededPrng& prng,
    NicheId origin_niche,
    std::size_t initial_agents) {
    const std::size_t count = clamp_val<std::size_t>(initial_agents, 512u, capacity_);
    const auto origin_idx = static_cast<std::uint8_t>(origin_niche);

    active_count_ = count;
    macro_subscribers_ = 0;

    auto* __restrict seg_p = segment.data();
    auto* __restrict sub_p = subscribed.data();
    auto* __restrict niche_p = primary_niche.data();
    auto* __restrict loy_p = loyalty.data();
    auto* __restrict pat_p = patience.data();
    auto* __restrict ad_p = ad_tolerance.data();
    auto* __restrict nost_p = nostalgia.data();
    auto* __restrict fat_p = fatigue.data();
    auto* __restrict join_p = joined_day.data();

    for (std::size_t i = 0; i < count; ++i) {
        sub_p[i] = 0;
        const float roll = prng.next_f32();
        if (roll < 0.84f) {
            seg_p[i] = static_cast<std::uint8_t>(ViewerSegment::Casual);
        } else if (roll < 0.92f) {
            seg_p[i] = static_cast<std::uint8_t>(ViewerSegment::Critic);
        } else if (roll < 0.975f) {
            seg_p[i] = static_cast<std::uint8_t>(ViewerSegment::Hater);
        } else {
            seg_p[i] = static_cast<std::uint8_t>(ViewerSegment::BrandScout);
        }

        niche_p[i] = prng.chance(0.42f)
                         ? origin_idx
                         : static_cast<std::uint8_t>(prng.int_range(0, static_cast<int>(kNicheCount) - 1));
        loy_p[i] = prng.range(-0.08f, 0.15f);
        pat_p[i] = clamp_val(prng.gaussian(0.54f, 0.18f), 0.10f, 0.98f);
        ad_p[i] = clamp_val(prng.gaussian(0.50f, 0.20f), 0.05f, 0.95f);
        nost_p[i] = 0.0f;
        fat_p[i] = prng.range(0.0f, 0.08f);
        join_p[i] = 0;
    }
}

void AudiencePool::ensure_growth_capacity(SeededPrng& prng, std::uint8_t target_niche) noexcept {
    if (active_count_ >= capacity_) return;
    const std::size_t add_n = std::min<std::size_t>(1024u, capacity_ - active_count_);
    const std::size_t start = active_count_;

    for (std::size_t i = start; i < start + add_n; ++i) {
        subscribed[i] = 0;
        segment[i] = static_cast<std::uint8_t>(
            prng.chance(0.88f) ? ViewerSegment::Casual : ViewerSegment::Critic);
        primary_niche[i] = prng.chance(0.55f)
                               ? target_niche
                               : static_cast<std::uint8_t>(prng.int_range(0, static_cast<int>(kNicheCount) - 1));
        loyalty[i] = prng.range(-0.05f, 0.12f);
        patience[i] = clamp_val(prng.gaussian(0.55f, 0.17f), 0.12f, 0.96f);
        ad_tolerance[i] = clamp_val(prng.gaussian(0.50f, 0.20f), 0.05f, 0.95f);
        nostalgia[i] = 0.0f;
        fatigue[i] = 0.0f;
        joined_day[i] = 0;
    }
    active_count_ += add_n;
}

WaveEvalReport AudiencePool::evaluate_wave(const WaveEvalParams& params, SeededPrng& prng) noexcept {
    WaveEvalReport report{};
    report.impressions = params.impressions < 0 ? 0 : params.impressions;
    report.observed_retention_curve = params.retention_curve;

    if (report.impressions == 0 || active_count_ == 0) {
        return report;
    }

    const auto& nspec = niche_spec(params.video_niche);
    const auto video_niche_idx = static_cast<std::uint8_t>(params.video_niche);
    const bool is_niche_shifted = params.video_niche != params.channel_origin_niche;

    if (report.impressions > 2'000) {
        ensure_growth_capacity(prng, video_niche_idx);
    }

    const std::size_t max_sample = std::min<std::size_t>(active_count_, 4096u);
    const std::size_t sample_size =
        static_cast<std::size_t>(std::min<std::int64_t>(report.impressions, static_cast<std::int64_t>(max_sample)));
    const double macro_scale = static_cast<double>(report.impressions) / static_cast<double>(sample_size);

    static constexpr std::array<float, 5> kSubShareByWave{0.68f, 0.34f, 0.16f, 0.07f, 0.03f};
    const float desired_sub_prob =
        macro_subscribers_ > 0 ? kSubShareByWave[std::min<std::size_t>(4, params.wave_index)] : 0.0f;

    std::array<std::uint32_t, 10> decile_survivors{};
    std::int64_t sample_clicks = 0;
    double watch_pct_sum = 0.0;
    std::int64_t sample_likes = 0;
    std::int64_t sample_dislikes = 0;
    std::int64_t sample_comments = 0;
    std::int64_t sample_sub_gains = 0;
    std::int64_t sample_sub_losses = 0;

    const float expectation_gap = params.intrinsic_quality - params.clickbait_level * 0.88f;
    const int active_max_idx = static_cast<int>(active_count_) - 1;

    auto* __restrict seg_p = segment.data();
    auto* __restrict sub_p = subscribed.data();
    auto* __restrict niche_p = primary_niche.data();
    auto* __restrict loy_p = loyalty.data();
    const auto* __restrict pat_p = patience.data();
    const auto* __restrict ad_p = ad_tolerance.data();
    auto* __restrict nost_p = nostalgia.data();
    auto* __restrict fat_p = fatigue.data();
    auto* __restrict join_p = joined_day.data();

    for (std::size_t s = 0; s < sample_size; ++s) {
        int idx = prng.int_range(0, active_max_idx);
        const bool want_sub = prng.chance(desired_sub_prob);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const int cand = prng.int_range(0, active_max_idx);
            const bool is_sub = sub_p[cand] == 1;
            const bool same_niche = niche_p[cand] == video_niche_idx;
            if (want_sub && is_sub) {
                idx = cand;
                break;
            }
            if (!want_sub && (params.wave_index >= 3 || same_niche)) {
                idx = cand;
                break;
            }
        }

        const auto seg = static_cast<ViewerSegment>(seg_p[idx]);
        const bool is_sub = sub_p[idx] == 1;
        const bool same_niche = niche_p[idx] == video_niche_idx;
        const float loy = loy_p[idx];
        const float fat = fat_p[idx];
        const float nost = nost_p[idx];

        float click_prob = nspec.baseline_ctr * (0.45f + 1.32f * params.packaging_appeal) * params.trend_ctr_boost;
        click_prob *= same_niche ? 1.24f : (params.wave_index >= 3 ? 0.82f : 0.56f);
        if (is_sub) click_prob *= 1.48f + std::max(0.0f, loy) * 0.55f;
        if (seg == ViewerSegment::CoreFan) click_prob *= 1.35f;
        if (seg == ViewerSegment::Hater) click_prob *= 1.15f;
        if (seg == ViewerSegment::Veteran && is_niche_shifted) {
            click_prob *= std::max(0.45f, 1.0f - nost * 0.45f);
        }
        click_prob *= std::max(0.35f, 1.0f - fat * 0.55f);
        click_prob = clamp_val(click_prob, 0.004f, 0.62f);

        if (!prng.chance(click_prob)) {
            continue;
        }

        ++sample_clicks;
        const float pat = pat_p[idx];
        const float ad_tol = ad_p[idx];
        int watched_deciles = 0;

        for (int d = 0; d < 10; ++d) {
            float keep_prob = clamp_val(
                0.52f + 0.46f * params.retention_curve[static_cast<std::size_t>(d)] +
                    (pat - 0.5f) * 0.16f + loy * 0.07f -
                    std::max(0.0f, params.ad_annoyance - ad_tol * 0.6f) * ((d == 3 || d == 4) ? 0.24f : 0.04f),
                0.12f,
                0.985f);

            if (d <= 2 && expectation_gap < -0.2f) {
                keep_prob += expectation_gap * 0.35f;
            }
            if (prng.chance(keep_prob)) {
                watched_deciles = d + 1;
                ++decile_survivors[static_cast<std::size_t>(d)];
            } else {
                break;
            }
        }

        const float watch_pct = clamp_val((static_cast<float>(watched_deciles) + prng.next_f32() * 0.75f) / 10.5f, 0.03f, 1.0f);
        watch_pct_sum += static_cast<double>(watch_pct);

        fat_p[idx] = clamp_val(fat + nspec.fatigue_rate * 0.65f, 0.0f, 1.0f);
        if (is_sub && is_niche_shifted && (seg == ViewerSegment::Veteran || seg == ViewerSegment::CoreFan)) {
            nost_p[idx] = clamp_val(nost + 0.14f, 0.0f, 1.0f);
        } else if (!is_niche_shifted && nost > 0.0f) {
            nost_p[idx] = std::max(0.0f, nost - 0.04f);
        }

        const float satisfaction =
            watch_pct * 0.55f + params.intrinsic_quality * 0.45f +
            std::min(0.0f, expectation_gap) * 0.65f -
            params.ad_annoyance * (1.0f - ad_tol) * 0.45f;

        const float next_loy = clamp_val(loy + (satisfaction - 0.46f) * 0.22f, -1.0f, 1.0f);
        loy_p[idx] = next_loy;

        if (satisfaction >= 0.62f && seg != ViewerSegment::Hater) {
            if (prng.chance(0.065f + (satisfaction - 0.60f) * 0.18f)) ++sample_likes;
        } else if (satisfaction < 0.34f || seg == ViewerSegment::Hater) {
            if (prng.chance(seg == ViewerSegment::Hater ? 0.22f : 0.075f)) ++sample_dislikes;
        }

        const float comment_prob =
            0.008f + std::fabs(satisfaction - 0.5f) * 0.035f +
            ((seg == ViewerSegment::Critic || seg == ViewerSegment::Veteran) ? 0.018f : 0.0f);
        if (prng.chance(comment_prob)) ++sample_comments;

        if (!is_sub) {
            if (satisfaction >= 0.64f && watch_pct >= 0.55f && seg != ViewerSegment::Hater &&
                prng.chance(0.045f + (satisfaction - 0.60f) * 0.24f)) {
                sub_p[idx] = 1;
                join_p[idx] = params.current_day;
                if (seg == ViewerSegment::Casual) {
                    seg_p[idx] = static_cast<std::uint8_t>(ViewerSegment::Subscriber);
                }
                ++sample_sub_gains;
            } else if (expectation_gap < -0.35f && prng.chance(0.06f)) {
                seg_p[idx] = static_cast<std::uint8_t>(ViewerSegment::Hater);
            }
        } else {
            const int tenure = static_cast<int>(params.current_day) - static_cast<int>(join_p[idx]);
            if (next_loy > 0.65f && seg == ViewerSegment::Subscriber) {
                seg_p[idx] = static_cast<std::uint8_t>(ViewerSegment::CoreFan);
            } else if (tenure >= 45 && seg == ViewerSegment::CoreFan && next_loy > 0.40f) {
                seg_p[idx] = static_cast<std::uint8_t>(ViewerSegment::Veteran);
            }

            const float unsub_prob =
                (next_loy < -0.25f ? 0.14f : 0.0f) +
                (expectation_gap < -0.32f ? 0.05f : 0.0f) +
                (nost_p[idx] > 0.72f ? 0.06f : 0.0f);
            if (unsub_prob > 0.0f && prng.chance(unsub_prob)) {
                sub_p[idx] = 0;
                seg_p[idx] = static_cast<std::uint8_t>(
                    next_loy < -0.45f ? ViewerSegment::Hater : ViewerSegment::Casual);
                ++sample_sub_losses;
            }
        }
    }

    report.views = std::min<std::int64_t>(report.impressions, std::llround(static_cast<double>(sample_clicks) * macro_scale));
    report.likes = std::llround(static_cast<double>(sample_likes) * macro_scale);
    report.dislikes = std::llround(static_cast<double>(sample_dislikes) * macro_scale);
    report.comments = std::llround(static_cast<double>(sample_comments) * macro_scale);
    report.sub_gains = std::llround(static_cast<double>(sample_sub_gains) * macro_scale);
    report.sub_losses = std::min<std::int64_t>(
        macro_subscribers_ + report.sub_gains,
        std::llround(static_cast<double>(sample_sub_losses) * macro_scale));

    macro_subscribers_ = std::max<std::int64_t>(0, macro_subscribers_ + report.sub_gains - report.sub_losses);
    report.ctr = report.impressions > 0 ? static_cast<float>(static_cast<double>(report.views) / static_cast<double>(report.impressions)) : 0.0f;
    report.avg_watch_pct = sample_clicks > 0 ? static_cast<float>(watch_pct_sum / static_cast<double>(sample_clicks)) : 0.0f;

    if (sample_clicks > 0) {
        for (std::size_t d = 0; d < 10; ++d) {
            report.observed_retention_curve[d] = clamp_val(
                static_cast<float>(decile_survivors[d]) / static_cast<float>(sample_clicks),
                0.0f,
                1.0f);
        }
    }
    return report;
}

void AudiencePool::advance_days(int days, std::uint16_t current_day) noexcept {
    if (days <= 0) return;
    const float recovery = std::min(0.85f, static_cast<float>(days) * 0.045f);
    auto* __restrict fat_p = fatigue.data();
    auto* __restrict seg_p = segment.data();
    const auto* __restrict sub_p = subscribed.data();
    const auto* __restrict join_p = joined_day.data();
    const std::size_t n = active_count_;

    // Векторизуемый проход остывания усталости (SIMD NEON / AVX2)
    for (std::size_t i = 0; i < n; ++i) {
        const float next_f = fat_p[i] - recovery;
        fat_p[i] = next_f > 0.0f ? next_f : 0.0f;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (sub_p[i] == 1 && seg_p[i] == static_cast<std::uint8_t>(ViewerSegment::CoreFan)) {
            if (static_cast<int>(current_day) - static_cast<int>(join_p[i]) >= 45) {
                seg_p[i] = static_cast<std::uint8_t>(ViewerSegment::Veteran);
            }
        }
    }
}

void AudiencePool::apply_global_impulse(
    float loyalty_delta,
    float nostalgia_delta,
    std::int64_t flat_subscriber_delta) noexcept {
    auto* __restrict loy_p = loyalty.data();
    auto* __restrict nost_p = nostalgia.data();
    const auto* __restrict sub_p = subscribed.data();
    const std::size_t n = active_count_;

    if (loyalty_delta != 0.0f) {
        for (std::size_t i = 0; i < n; ++i) {
            loy_p[i] = clamp_val(loy_p[i] + loyalty_delta, -1.0f, 1.0f);
        }
    }
    if (nostalgia_delta != 0.0f) {
        for (std::size_t i = 0; i < n; ++i) {
            if (sub_p[i] == 1) {
                nost_p[i] = clamp_val(nost_p[i] + nostalgia_delta, 0.0f, 1.0f);
            }
        }
    }
    macro_subscribers_ = std::max<std::int64_t>(0, macro_subscribers_ + flat_subscriber_delta);
}

AudienceSummary AudiencePool::summarize() const noexcept {
    AudienceSummary s{};
    s.active_agents = active_count_;
    s.capacity = capacity_;
    s.subscribers = macro_subscribers_;
    if (active_count_ == 0) return s;

    double loy_sum = 0.0;
    double fat_sum = 0.0;
    double nost_sum = 0.0;

    const auto* __restrict seg_p = segment.data();
    const auto* __restrict loy_p = loyalty.data();
    const auto* __restrict fat_p = fatigue.data();
    const auto* __restrict nost_p = nostalgia.data();

    for (std::size_t i = 0; i < active_count_; ++i) {
        const auto seg_idx = seg_p[i];
        if (seg_idx < kSegmentCount) {
            ++s.segment_counts[seg_idx];
        }
        loy_sum += static_cast<double>(loy_p[i]);
        fat_sum += static_cast<double>(fat_p[i]);
        nost_sum += static_cast<double>(nost_p[i]);
    }

    const double inv = 1.0 / static_cast<double>(active_count_);
    s.avg_loyalty = static_cast<float>(loy_sum * inv);
    s.avg_fatigue = static_cast<float>(fat_sum * inv);
    s.avg_nostalgia = static_cast<float>(nost_sum * inv);
    return s;
}

} // namespace simul::core
