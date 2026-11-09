#pragma once
#include "calendar_period.h"
#include "session_tracker.h"
#include <optional>
#include <vector>

namespace chronox {
// All counters are microseconds. steady includes sleep; awake excludes it.
// Inject the entire sample in tests; never read a host clock in this class.
struct TimeSample { Timestamp utc{}, steady{}, awake{}, uncertainty{}; };
struct MeasuredSpan {
    Timestamp sequence{}, epoch{}, session{};
    Timestamp utcStart{}, utcEnd{}, monoStart{}, monoEnd{}, duration{};
    bool active{};
};
struct TemporalSnapshot {
    TimeSample sample;
    Timestamp utc{}, elapsed{}, continuous{}, epoch{}, corrections{}, uncertainties{};
    bool counting{}, uncertain{};
};
class TemporalEngine {
public:
    TemporalEngine(bool locked, TimeSample initial);
    void observe(TimeSample sample);
    void event(SessionEvent event, TimeSample sample);
    void reconcile(bool locked, TimeSample sample);
    void stop(TimeSample sample);
    TemporalSnapshot snapshot() const;
    const std::optional<MeasuredSpan>& current() const { return current_; }
    const std::vector<MeasuredSpan>& pending() const { return pending_; }
    void acknowledge() { pending_.clear(); }
private:
    void open();
    void close();
    TimeSample last_;
    SessionTracker::TimePoint logical_{};
    SessionTracker tracker_;
    std::optional<MeasuredSpan> current_;
    std::vector<MeasuredSpan> pending_;
    Timestamp next_{}, epoch_{}, session_{}, corrections_{}, uncertainties_{};
    bool uncertain_{}, stopped_{};
};
} // namespace chronox
