#pragma once

#include "../core/calendar_period.h"
#include "../core/activity_tracker.h"

#include <array>
#include <chrono>
#include <vector>

namespace chronox {

class SessionDatabase;

struct DashboardSession {
    TimeRange interval{};
    std::chrono::seconds duration{};
    bool active{};
};

struct DashboardSnapshot {
    std::chrono::seconds todayDuration{};
    std::chrono::seconds weekDuration{};
    std::chrono::seconds monthDuration{};
    std::chrono::seconds yearDuration{};
    TimeRange todayRange{};
    Timestamp capturedAt{};
    std::vector<TimeRange> todayIntervals;
    std::array<std::chrono::seconds, 7> weekDays{};
    std::vector<DashboardSession> recentSessions;
    std::array<std::chrono::seconds,4> activityDurations{};
    std::array<bool,4> activityMeasured{};
    ActivityState activityState{ActivityState::Inactive};
    bool activityDetectionEnabled{true};
    bool trackingActive{};
    bool timeUncertain{};
};

struct DashboardHistoryBucket {
    TimeRange range{};
    std::chrono::seconds duration{};
};

struct DashboardHistorySnapshot {
    CalendarPeriod period{CalendarPeriod::Day};
    Timestamp anchor{};
    TimeRange range{};
    Timestamp capturedAt{};
    std::chrono::seconds duration{};
    std::chrono::seconds activityDuration{};
    bool activityMeasured{};
    std::size_t daysWithActivity{};
    std::vector<DashboardHistoryBucket> buckets;
    std::vector<DashboardSession> sessions;
    bool trackingActive{};
    bool timeUncertain{};
};

DashboardSnapshot queryDashboardSnapshot(
    const SessionDatabase& database, Timestamp now, bool trackingActive,
    const CalendarTimeConverter* converter = nullptr);

DashboardHistorySnapshot queryDashboardHistory(
    const SessionDatabase& database, CalendarPeriod period, Timestamp anchor,
    Timestamp now, bool trackingActive = false,
    const CalendarTimeConverter* converter = nullptr);

Timestamp shiftCalendarPeriod(Timestamp anchor, CalendarPeriod period,
                              int delta,
                              const CalendarTimeConverter* converter = nullptr);

} // namespace chronox
