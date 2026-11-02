#pragma once

#include "calendar_period.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace chronox {

struct HistoryReport {
    CalendarPeriod period;
    TimeRange range;
    std::chrono::microseconds sessionDuration{};
    std::vector<TimeRange> sessionIntervals;
    std::optional<std::chrono::microseconds> declaredWorkDuration;
};

class SessionDatabase {
public:
    explicit SessionDatabase(const std::filesystem::path& path);
    ~SessionDatabase();

    SessionDatabase(const SessionDatabase&) = delete;
    SessionDatabase& operator=(const SessionDatabase&) = delete;
    SessionDatabase(SessionDatabase&&) noexcept;
    SessionDatabase& operator=(SessionDatabase&&) noexcept;

    void setCounting(bool counting, Timestamp timestamp);
    void checkpoint(Timestamp timestamp);
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
    std::chrono::microseconds duration(TimeRange range, Timestamp now) const;
    HistoryReport queryHistory(TimeRange range,
                               CalendarPeriod period,
                               Timestamp now) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
