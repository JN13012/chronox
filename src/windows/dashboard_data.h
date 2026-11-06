#pragma once

#include "../core/calendar_period.h"

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
    bool trackingActive{};
};

DashboardSnapshot queryDashboardSnapshot(
    const SessionDatabase& database, Timestamp now, bool trackingActive,
    const CalendarTimeConverter* converter = nullptr);

} // namespace chronox
