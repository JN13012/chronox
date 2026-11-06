#include "dashboard_data.h"

#include "../storage/session_database.h"

#include <algorithm>
#include <stdexcept>

namespace chronox {
namespace {

std::chrono::seconds asSeconds(std::chrono::microseconds duration) {
    return std::chrono::duration_cast<std::chrono::seconds>(duration);
}

HistoryReport queryHistory(const SessionDatabase& database,
                           CalendarPeriod period, Timestamp now,
                           const CalendarTimeConverter* converter) {
    if (converter == nullptr) {
        return database.queryHistory(period, now, now);
    }
    return database.queryHistory(period, now, now, *converter);
}

std::chrono::microseconds queryDuration(const SessionDatabase& database,
                                        CalendarPeriod period, Timestamp now,
                                        const CalendarTimeConverter* converter) {
    if (converter == nullptr) {
        return database.duration(period, now);
    }
    return database.duration(period, now, *converter);
}

TimeRange dayContaining(Timestamp timestamp,
                       const CalendarTimeConverter* converter) {
    if (converter == nullptr) {
        return calendarPeriodContaining(timestamp, CalendarPeriod::Day);
    }
    return calendarPeriodContaining(timestamp, CalendarPeriod::Day, *converter);
}

} // namespace

DashboardSnapshot queryDashboardSnapshot(
    const SessionDatabase& database, Timestamp now, bool trackingActive,
    const CalendarTimeConverter* converter) {
    const HistoryReport day =
        queryHistory(database, CalendarPeriod::Day, now, converter);
    const HistoryReport week =
        queryHistory(database, CalendarPeriod::Week, now, converter);
    const HistoryReport year =
        queryHistory(database, CalendarPeriod::Year, now, converter);

    DashboardSnapshot snapshot;
    snapshot.todayDuration = asSeconds(day.sessionDuration);
    snapshot.weekDuration = asSeconds(week.sessionDuration);
    snapshot.monthDuration =
        asSeconds(queryDuration(database, CalendarPeriod::Month, now, converter));
    snapshot.yearDuration = asSeconds(year.sessionDuration);
    snapshot.todayRange = day.range;
    snapshot.capturedAt = now;
    snapshot.todayIntervals = day.sessionIntervals;
    snapshot.trackingActive = trackingActive;

    Timestamp dayStart = week.range.start;
    for (std::size_t index = 0; index < snapshot.weekDays.size(); ++index) {
        const TimeRange calendarDay = dayContaining(dayStart, converter);
        const Timestamp dayEnd = std::min(calendarDay.end, week.range.end);
        if (dayEnd <= dayStart) {
            throw std::runtime_error(
                "Calendar day did not advance while building dashboard data");
        }

        std::chrono::microseconds duration{};
        for (const auto& interval : week.sessionIntervals) {
            const Timestamp start = std::max(dayStart, interval.start);
            const Timestamp end = std::min(dayEnd, interval.end);
            if (end > start) {
                duration += std::chrono::microseconds(end - start);
            }
        }
        snapshot.weekDays[index] = asSeconds(duration);
        dayStart = dayEnd;
    }
    if (dayStart != week.range.end) {
        throw std::runtime_error(
            "Dashboard weekly aggregation did not cover the calendar week");
    }

    constexpr std::size_t RecentSessionLimit = 8;
    const std::size_t firstRecent =
        year.sessionIntervals.size() > RecentSessionLimit
            ? year.sessionIntervals.size() - RecentSessionLimit
            : 0;
    snapshot.recentSessions.reserve(year.sessionIntervals.size() - firstRecent);
    for (std::size_t index = year.sessionIntervals.size();
         index > firstRecent; --index) {
        const TimeRange interval = year.sessionIntervals[index - 1];
        const bool active = trackingActive && interval.end == now &&
                            index == year.sessionIntervals.size();
        snapshot.recentSessions.push_back(
            {interval,
             asSeconds(std::chrono::microseconds(interval.end - interval.start)),
             active});
    }
    return snapshot;
}

} // namespace chronox
