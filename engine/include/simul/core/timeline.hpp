#pragma once

#include "simul/core/types.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace simul::core {

/**
 * Гибридные игровые часы + бинарная мин-куча отложенных последствий O(log N).
 */
class SimulationClock {
public:
    explicit SimulationClock(std::int64_t initial_hours = 10) noexcept
        : total_hours_(initial_hours < 0 ? 0 : initial_hours) {}

    std::int64_t total_hours() const noexcept { return total_hours_; }
    std::int32_t current_day() const noexcept { return static_cast<std::int32_t>(total_hours_ / 24) + 1; }
    int hour_of_day() const noexcept { return static_cast<int>(total_hours_ % 24); }
    std::size_t pending_count() const noexcept { return heap_.size(); }

    void schedule(ScheduledConsequence item) {
        heap_.push_back(std::move(item));
        std::push_heap(heap_.begin(), heap_.end(), HeapCompare{});
    }

    std::vector<ScheduledConsequence> advance_hours(std::int64_t hours) {
        std::vector<ScheduledConsequence> due;
        if (hours <= 0) return due;
        total_hours_ += hours;
        while (!heap_.empty() && heap_.front().trigger_at_hour <= total_hours_) {
            std::pop_heap(heap_.begin(), heap_.end(), HeapCompare{});
            due.push_back(std::move(heap_.back()));
            heap_.pop_back();
        }
        return due;
    }

    const std::vector<ScheduledConsequence>& raw_heap() const noexcept { return heap_; }

    void restore(std::int64_t total_hours, std::vector<ScheduledConsequence> items) {
        total_hours_ = total_hours;
        heap_ = std::move(items);
        std::make_heap(heap_.begin(), heap_.end(), HeapCompare{});
    }

private:
    struct HeapCompare {
        bool operator()(const ScheduledConsequence& a, const ScheduledConsequence& b) const noexcept {
            return a.trigger_at_hour > b.trigger_at_hour; // min-heap
        }
    };

    std::int64_t total_hours_ = 10;
    std::vector<ScheduledConsequence> heap_;
};

} // namespace simul::core
