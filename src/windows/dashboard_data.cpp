#include "dashboard_data.h"

#include "../storage/session_database.h"

#include <algorithm>
#include <ctime>
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

TimeRange dayContaining(Timestamp timestamp,
                       const CalendarTimeConverter* converter) {
    if (converter == nullptr) {
        return calendarPeriodContaining(timestamp, CalendarPeriod::Day);
    }
    return calendarPeriodContaining(timestamp, CalendarPeriod::Day, *converter);
}

std::tm localTime(std::time_t timestamp) {
    std::tm result{};
#if defined(_WIN32)
    if (localtime_s(&result, &timestamp) != 0) {
        throw std::runtime_error("Could not read the local calendar date");
    }
#else
    if (localtime_r(&timestamp, &result) == nullptr) {
        throw std::runtime_error("Could not read the local calendar date");
    }
#endif
    return result;
}

std::time_t localTimestamp(std::tm value,
                           const CalendarTimeConverter* converter) {
    if (converter != nullptr) {
        return converter->toTimestamp(value);
    }
    value.tm_isdst = -1;
    const std::time_t result = std::mktime(&value);
    if (result == static_cast<std::time_t>(-1)) {
        throw std::runtime_error("Could not navigate the local calendar");
    }
    return result;
}

Timestamp toMicroseconds(std::time_t timestamp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::seconds(timestamp))
        .count();
}

} // namespace

DashboardSnapshot queryDashboardSnapshot(
    const SessionDatabase& database, Timestamp now, bool trackingActive,
    const CalendarTimeConverter* converter) {
    const HistoryReport day =
        queryHistory(database, CalendarPeriod::Day, now, converter);
    const HistoryReport week =
        queryHistory(database, CalendarPeriod::Week, now, converter);
    const HistoryReport month =
        queryHistory(database, CalendarPeriod::Month, now, converter);
    const HistoryReport year =
        queryHistory(database, CalendarPeriod::Year, now, converter);

    DashboardSnapshot snapshot;
    snapshot.todayDuration = asSeconds(day.sessionDuration);
    snapshot.weekDuration = asSeconds(week.sessionDuration);
    snapshot.monthDuration = asSeconds(month.sessionDuration);
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
        for (const auto& contribution : week.contributions) duration += contributionWithin(contribution,{dayStart,dayEnd});
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
        const auto& contribution = year.contributions[index - 1];
        const bool active = trackingActive && contribution.active;
        snapshot.recentSessions.push_back(
            {interval,
             asSeconds(contribution.duration),
             active});
    }
    return snapshot;
}

DashboardHistorySnapshot queryDashboardHistory(
    const SessionDatabase& database, CalendarPeriod period, Timestamp anchor,
    Timestamp now, bool trackingActive,
    const CalendarTimeConverter* converter) {
    const HistoryReport report =
        converter == nullptr
            ? database.queryHistory(period, anchor, now)
            : database.queryHistory(period, anchor, now, *converter);
    DashboardHistorySnapshot snapshot;
    snapshot.period = period;
    snapshot.anchor = anchor;
    snapshot.range = report.range;
    snapshot.capturedAt = now;
    snapshot.duration = asSeconds(report.sessionDuration);
    snapshot.daysWithActivity = report.daysWithDuration;
    snapshot.trackingActive = trackingActive;
    snapshot.sessions.reserve(report.sessionIntervals.size());
    for (std::size_t index = 0; index < report.sessionIntervals.size(); ++index) {
        const TimeRange interval = report.sessionIntervals[index];
        const auto& contribution = report.contributions[index];
        const bool active = trackingActive && contribution.active;
        snapshot.sessions.push_back(
            {interval,
             asSeconds(contribution.duration),
             active});
    }

    Timestamp bucketStart = report.range.start;
    const CalendarPeriod bucketPeriod =
        period == CalendarPeriod::Year ? CalendarPeriod::Month
                                       : CalendarPeriod::Day;
    while (bucketStart < report.range.end) {
        TimeRange bucket = converter == nullptr
                               ? calendarPeriodContaining(bucketStart, bucketPeriod)
                               : calendarPeriodContaining(bucketStart, bucketPeriod,
                                                          *converter);
        bucket.start = bucketStart;
        bucket.end = std::min(bucket.end, report.range.end);
        std::chrono::microseconds duration{};
        for (const auto& contribution : report.contributions) duration += contributionWithin(contribution,bucket);
        snapshot.buckets.push_back({bucket, asSeconds(duration)});
        if (bucket.end <= bucketStart) {
            throw std::runtime_error(
                "Calendar bucket did not advance while building history");
        }
        bucketStart = bucket.end;
    }
    return snapshot;
}

Timestamp shiftCalendarPeriod(Timestamp anchor, CalendarPeriod period, int delta,
                              const CalendarTimeConverter* converter) {
    if (converter != nullptr &&
        (converter->toLocal == nullptr || converter->toTimestamp == nullptr)) {
        throw std::invalid_argument("Calendar time converter is incomplete");
    }
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(anchor));
    std::tm local = converter == nullptr
                        ? localTime(static_cast<std::time_t>(seconds.count()))
                        : converter->toLocal(static_cast<std::time_t>(seconds.count()));
    switch (period) {
    case CalendarPeriod::Day:
        local.tm_mday += delta;
        break;
    case CalendarPeriod::Week:
        local.tm_mday += delta * 7;
        break;
    case CalendarPeriod::Month:
        local.tm_mday = 1;
        local.tm_mon += delta;
        break;
    case CalendarPeriod::Year:
        local.tm_mday = 1;
        local.tm_mon = 0;
        local.tm_year += delta;
        break;
    }
    local.tm_hour = 12;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return toMicroseconds(localTimestamp(local, converter));
}

} // namespace chronox
