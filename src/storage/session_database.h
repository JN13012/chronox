#pragma once

#include "calendar_period.h"
#include "temporal_engine.h"
#include "activity_tracker.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace chronox {

enum class StorageMode { Legacy, Measured, ReadOnly, Activity };
struct SessionContribution {
    TimeRange interval;
    std::chrono::microseconds duration{};
    bool active{};
    Timestamp epoch{};
};
std::chrono::microseconds contributionWithin(const SessionContribution&, TimeRange);

struct ActivityReport {
    std::chrono::microseconds duration{};
    bool measured{};
    std::vector<SessionContribution> contributions;
};

struct HistoryReport {
    CalendarPeriod period;
    TimeRange range;
    std::chrono::microseconds sessionDuration{};
    std::vector<TimeRange> sessionIntervals;
    std::size_t daysWithDuration{};
    std::optional<std::chrono::microseconds> declaredWorkDuration;
    std::vector<SessionContribution> contributions;
};

class SessionDatabase {
public:
    explicit SessionDatabase(const std::filesystem::path& path, StorageMode mode = StorageMode::Legacy);
    ~SessionDatabase();

    SessionDatabase(const SessionDatabase&) = delete;
    SessionDatabase& operator=(const SessionDatabase&) = delete;
    SessionDatabase(SessionDatabase&&) noexcept;
    SessionDatabase& operator=(SessionDatabase&&) noexcept;

    void setCounting(bool counting, Timestamp timestamp);
    void checkpoint(Timestamp timestamp);
    // Commits pending boundaries atomically; otherwise refreshes RAM only.
    void updateTemporal(TemporalEngine& engine, bool durable, ActivityTracker* activity = nullptr);
    ActivityReport queryActivity(CalendarPeriod period, Timestamp anchor, const CalendarTimeConverter* converter = nullptr) const;
    Timestamp uncertaintyCount() const;
    Timestamp activityUncertaintyCount() const;
    void backupTo(const std::filesystem::path& destination) const;
    Timestamp latestRecordedTimestamp() const;
    std::chrono::microseconds duration(CalendarPeriod period, Timestamp now) const;
    std::chrono::microseconds duration(CalendarPeriod period,
                                       Timestamp now,
                                       const CalendarTimeConverter& converter) const;
    HistoryReport queryHistory(CalendarPeriod period,
                               Timestamp dateAnchor,
                               Timestamp now) const;
    HistoryReport queryHistory(CalendarPeriod period,
                               Timestamp dateAnchor,
                               Timestamp now,
                               const CalendarTimeConverter& converter) const;

private:
    HistoryReport queryMeasuredHistory(TimeRange, CalendarPeriod, const CalendarTimeConverter*) const;
    std::chrono::microseconds legacyDuration(TimeRange range, Timestamp now) const;
    HistoryReport queryHistory(TimeRange range,
                               CalendarPeriod period,
                               Timestamp now,
                               const CalendarTimeConverter* converter) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
