#pragma once
#include "temporal_engine.h"
#include <cstdint>

namespace chronox {
// ticks is sampled even if the last-input API fails; valid describes that API.
struct InputSample { std::uint64_t ticks{}; std::uint32_t lastInput{}; bool valid{}; };
struct ActivitySettings {
    bool enabled{true};
    int minutes{5};
    bool operator==(const ActivitySettings&) const = default;
};
bool validActivitySettings(ActivitySettings);
struct ActivitySpan : MeasuredSpan {
    bool detectionEnabled{true};
    int thresholdMinutes{5};
};
struct ActivityCoverage { Timestamp sequence{}, epoch{}, utcStart{}, utcEnd{}; };
enum class ActivityState { Active, Inactive, Locked, Sleeping, Uncertain };
// Only aggregate spans are persisted. Raw input timestamps stay in RAM.
class ActivityTracker {
public:
    explicit ActivityTracker(ActivitySettings settings = {});
    void advance(const TemporalSnapshot& before, const TemporalSnapshot& after,
                 Timestamp civilStart, InputSample input);
    void configure(ActivitySettings settings);
    void setState(ActivityState state);
    // Only an explicit resume/unlock may clear missing power evidence.
    void reconcilePower() { powerUncertain_=false; }
    void stop();
    ActivityState state() const { return state_; }
    ActivitySettings settings() const { return settings_; }
    Timestamp uncertainties() const { return uncertainties_; }
    const std::vector<ActivitySpan>& pending() const { return pending_; }
    const std::optional<ActivitySpan>& current() const { return current_; }
    const std::optional<ActivityCoverage>& coverage() const { return coverage_; }
    const std::vector<ActivityCoverage>& pendingCoverage() const { return pendingCoverage_; }
    void acknowledge() { pending_.clear(); pendingCoverage_.clear(); }
private:
    void close();
    void append(Timestamp from, Timestamp to, Timestamp utc, const TemporalSnapshot& before);
    ActivitySettings settings_;
    ActivityState state_{ActivityState::Inactive};
    std::optional<std::uint64_t> inputTick_, observedTick_;
    std::optional<std::uint32_t> rejectedInput_;
    std::optional<Timestamp> deadline_;
    std::optional<ActivitySpan> current_;
    std::vector<ActivitySpan> pending_;
    std::optional<ActivityCoverage> coverage_;
    std::vector<ActivityCoverage> pendingCoverage_;
    Timestamp coverageSequence_{};
    Timestamp sequence_{}, uncertainties_{};
    bool first_{true}, inputUncertain_{}, powerUncertain_{};
};
}
