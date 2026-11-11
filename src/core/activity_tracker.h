#pragma once
#include "temporal_engine.h"
#include <cstdint>

namespace chronox {
struct InputSample { std::uint64_t ticks{}; std::uint32_t lastInput{}; bool valid{}; };
struct ActivitySettings {
    bool enabled{true};
    int minutes{5};
    bool operator==(const ActivitySettings&) const = default;
};
bool validActivitySettings(ActivitySettings);
enum class ActivityState { Active, Inactive, Locked, Sleeping, Uncertain };
// Only aggregate spans are persisted. Raw input timestamps stay in RAM.
class ActivityTracker {
public:
    explicit ActivityTracker(ActivitySettings settings = {});
    void advance(const TemporalSnapshot& before, const TemporalSnapshot& after,
                 Timestamp civilStart, InputSample input);
    void configure(ActivitySettings settings);
    void setState(ActivityState state);
    void stop();
    ActivityState state() const { return state_; }
    ActivitySettings settings() const { return settings_; }
    Timestamp uncertainties() const { return uncertainties_; }
    const std::vector<MeasuredSpan>& pending() const { return pending_; }
    const std::optional<MeasuredSpan>& current() const { return current_; }
    void acknowledge() { pending_.clear(); }
private:
    void close();
    void append(Timestamp from, Timestamp to, Timestamp utc, const TemporalSnapshot& before);
    ActivitySettings settings_;
    ActivityState state_{ActivityState::Inactive};
    std::optional<std::uint64_t> inputTick_, observedTick_;
    std::optional<Timestamp> deadline_;
    std::optional<MeasuredSpan> current_;
    std::vector<MeasuredSpan> pending_;
    Timestamp sequence_{}, uncertainties_{};
    bool first_{true}, inputUncertain_{};
};
}
