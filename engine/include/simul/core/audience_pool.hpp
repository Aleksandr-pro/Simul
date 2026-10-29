#pragma once

#include "simul/core/prng.hpp"
#include "simul/core/types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

namespace simul::core {

/**
 * Выровненный по 64-байтной кэш-линии CPU непрерывный буфер для SIMD (ARM NEON / AVX2).
 */
template <typename T>
class AlignedBuffer {
public:
    AlignedBuffer() noexcept = default;

    explicit AlignedBuffer(std::size_t count) { allocate(count); }

    ~AlignedBuffer() { release(); }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept
        : ptr_(other.ptr_), size_(other.size_) {
        other.ptr_ = nullptr;
        other.size_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            size_ = other.size_;
            other.ptr_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void allocate(std::size_t count) {
        release();
        if (count == 0) return;
        const std::size_t bytes = ((count * sizeof(T) + 63u) / 64u) * 64u;
        ptr_ = static_cast<T*>(::operator new[](bytes, std::align_val_t{64}));
        size_ = count;
        for (std::size_t i = 0; i < count; ++i) {
            ptr_[i] = T{};
        }
    }

    void release() noexcept {
        if (ptr_) {
            ::operator delete[](ptr_, std::align_val_t{64});
            ptr_ = nullptr;
            size_ = 0;
        }
    }

    T* data() noexcept { return ptr_; }
    const T* data() const noexcept { return ptr_; }
    std::size_t size() const noexcept { return size_; }

    T& operator[](std::size_t idx) noexcept { return ptr_[idx]; }
    const T& operator[](std::size_t idx) const noexcept { return ptr_[idx]; }

private:
    T* ptr_ = nullptr;
    std::size_t size_ = 0;
};

struct WaveEvalParams {
    std::uint8_t wave_index = 0;
    std::int64_t impressions = 0;
    NicheId video_niche = NicheId::Tech;
    NicheId channel_origin_niche = NicheId::Tech;
    std::uint16_t current_day = 1;
    float packaging_appeal = 0.7f;
    float clickbait_level = 0.15f;
    float intrinsic_quality = 0.7f;
    std::array<float, 10> retention_curve{};
    float trend_ctr_boost = 1.0f;
    float ad_annoyance = 0.0f;
};

struct WaveEvalReport {
    std::int64_t impressions = 0;
    std::int64_t views = 0;
    float ctr = 0.0f;
    float avg_watch_pct = 0.0f;
    std::int64_t likes = 0;
    std::int64_t dislikes = 0;
    std::int64_t comments = 0;
    std::int64_t sub_gains = 0;
    std::int64_t sub_losses = 0;
    std::array<float, 10> observed_retention_curve{};
};

/**
 * Живой SoA-пул зрителей (до 100 000+ агентов в ~2.4 МБ непрерывной памяти).
 */
class AudiencePool {
public:
    explicit AudiencePool(std::size_t capacity = 50'000);

    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t active_count() const noexcept { return active_count_; }
    std::int64_t subscribers() const noexcept { return macro_subscribers_; }

    void seed_platform_population(SeededPrng& prng, NicheId origin_niche, std::size_t initial_agents);

    WaveEvalReport evaluate_wave(const WaveEvalParams& params, SeededPrng& prng) noexcept;

    void advance_days(int days, std::uint16_t current_day) noexcept;

    void apply_global_impulse(
        float loyalty_delta,
        float nostalgia_delta,
        std::int64_t flat_subscriber_delta = 0) noexcept;

    AudienceSummary summarize() const noexcept;

    // Прямой доступ для быстрой бинарной сериализации .simsave
    void set_counts_raw(std::size_t active_cnt, std::int64_t macro_subs) noexcept {
        active_count_ = active_cnt <= capacity_ ? active_cnt : capacity_;
        macro_subscribers_ = macro_subs < 0 ? 0 : macro_subs;
    }

    AlignedBuffer<std::uint8_t> segment;
    AlignedBuffer<std::uint8_t> subscribed;
    AlignedBuffer<std::uint8_t> primary_niche;
    AlignedBuffer<float> loyalty;
    AlignedBuffer<float> patience;
    AlignedBuffer<float> ad_tolerance;
    AlignedBuffer<float> nostalgia;
    AlignedBuffer<float> fatigue;
    AlignedBuffer<std::uint16_t> joined_day;

private:
    void ensure_growth_capacity(SeededPrng& prng, std::uint8_t target_niche) noexcept;

    std::size_t capacity_ = 0;
    std::size_t active_count_ = 0;
    std::int64_t macro_subscribers_ = 0;
};

} // namespace simul::core
