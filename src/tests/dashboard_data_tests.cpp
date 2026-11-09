#include "../windows/dashboard_data.h"

#include "../core/calendar_period.h"
#include "../storage/session_database.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using namespace std::chrono_literals;
using chronox::CalendarPeriod;
using chronox::SessionDatabase;
using chronox::Timestamp;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        directory_ = std::filesystem::temp_directory_path() /
                     ("chronox-dashboard-tests-" +
                      std::to_string(std::chrono::steady_clock::now()
                                         .time_since_epoch()
                                         .count()));
        std::filesystem::create_directories(directory_);
        path_ = directory_ / "chronox.db";
    }

    ~TemporaryDatabase() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

std::time_t localTimestamp(int year, int month, int day, int hour,
                           int minute = 0) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_isdst = -1;
    const auto result = std::mktime(&value);
    require(result != static_cast<std::time_t>(-1), "local timestamp is valid");
    return result;
}

Timestamp toMicroseconds(std::time_t timestamp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::seconds(timestamp))
        .count();
}

Timestamp localTime(int year, int month, int day, int hour, int minute = 0) {
    return toMicroseconds(localTimestamp(year, month, day, hour, minute));
}

template <typename Rep, typename Period>
Timestamp durationTicks(std::chrono::duration<Rep, Period> duration) {
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

void recordInterval(SessionDatabase& database, Timestamp start, Timestamp end) {
    database.setCounting(true, start);
    database.setCounting(false, end);
}

void emptyDatabaseProducesEmptyOverview() {
    TemporaryDatabase file;
    SessionDatabase database(file.path());
    const Timestamp now = localTime(2024, 6, 15, 12);
    const auto snapshot =
        chronox::queryDashboardSnapshot(database, now, false);

    require(snapshot.todayDuration == 0s && snapshot.weekDuration == 0s &&
                snapshot.monthDuration == 0s && snapshot.yearDuration == 0s,
            "an empty database produces zero totals for all periods");
    require(snapshot.todayIntervals.empty() && snapshot.recentSessions.empty(),
            "an empty database produces no chart segments or recent sessions");
    require(snapshot.weekDays == std::array<std::chrono::seconds, 7>{},
            "an empty database produces seven zero-valued daily bars");
    require(!snapshot.trackingActive,
            "empty overview preserves the inactive tracker state");
}

void periodsSessionsAndCrossMidnightAreAggregatedFromHistory() {
    TemporaryDatabase file;
    SessionDatabase database(file.path());

    recordInterval(database, localTime(2023, 12, 31, 22),
                   localTime(2023, 12, 31, 23));
    recordInterval(database, localTime(2024, 5, 31, 10),
                   localTime(2024, 5, 31, 11));
    recordInterval(database, localTime(2024, 6, 9, 23, 30),
                   localTime(2024, 6, 10, 0, 30));
    recordInterval(database, localTime(2024, 6, 10, 9),
                   localTime(2024, 6, 10, 11));
    recordInterval(database, localTime(2024, 6, 11, 14),
                   localTime(2024, 6, 11, 15) + durationTicks(30min));
    recordInterval(database, localTime(2024, 6, 14, 23, 30),
                   localTime(2024, 6, 15, 0, 30));
    recordInterval(database, localTime(2024, 6, 15, 9),
                   localTime(2024, 6, 15, 10) + durationTicks(30min));

    const Timestamp now = localTime(2024, 6, 15, 12);
    const Timestamp activeStart = localTime(2024, 6, 15, 11, 30);
    database.setCounting(true, activeStart);
    database.checkpoint(localTime(2024, 6, 15, 11, 45));
    const Timestamp latestRecordedBeforeQuery = database.latestRecordedTimestamp();
    const auto snapshot =
        chronox::queryDashboardSnapshot(database, now, true);

    require(snapshot.todayDuration == 150min,
            "today includes only its half of a cross-midnight session plus today's work");
    require(snapshot.weekDuration == 420min,
            "weekly total includes the Monday-to-Sunday counted intervals");
    require(snapshot.monthDuration == 450min,
            "monthly total is read from the database for the current local month");
    require(snapshot.yearDuration == 510min,
            "year total includes this year's history and excludes the prior year");
    require(snapshot.todayIntervals.size() == 3,
            "today's timeline contains clipped and active counted intervals");
    const auto todayHistory = chronox::queryDashboardHistory(
        database, CalendarPeriod::Day, now, now, true);
    require(snapshot.todayDuration == todayHistory.duration,
            "overview and history use the same current-time boundary for open intervals");
    require(snapshot.weekDays[0] == 150min &&
                snapshot.weekDays[1] == 90min &&
                snapshot.weekDays[4] == 30min &&
                snapshot.weekDays[5] == 150min,
            "weekly bars split intervals across the correct Monday-first days");
    require(snapshot.recentSessions.size() == 7,
            "recent history includes the latest sessions from the current year");
    require(snapshot.recentSessions.front().active,
            "the latest session is identified as active");
    require(snapshot.recentSessions.front().interval.start == activeStart &&
                snapshot.recentSessions.front().interval.end == now,
            "the active session has bounds through the snapshot capture time");
    require(snapshot.recentSessions[1].interval.start ==
                localTime(2024, 6, 15, 9) &&
                snapshot.recentSessions[1].duration == 90min,
            "the active session duration and previous closed session are distinct");
    require(snapshot.recentSessions[5].interval.start ==
                localTime(2024, 6, 9, 23, 30) &&
                snapshot.recentSessions[5].duration == 1h,
            "recent session rows retain full starts and durations across Monday");
    require(database.latestRecordedTimestamp() == latestRecordedBeforeQuery,
            "building the overview does not write SQLite data");
}

std::tm utcTime(std::time_t timestamp) {
    std::tm result{};
#if defined(_WIN32)
    if (gmtime_s(&result, &timestamp) != 0) {
        throw std::runtime_error("Could not convert UTC timestamp");
    }
#else
    if (gmtime_r(&timestamp, &result) == nullptr) {
        throw std::runtime_error("Could not convert UTC timestamp");
    }
#endif
    return result;
}

std::time_t utcTimestamp(int year, unsigned month, unsigned day, int hour) {
    const auto date = std::chrono::sys_days{
        std::chrono::year{year} / std::chrono::month{month} /
        std::chrono::day{day}};
    return std::chrono::duration_cast<std::chrono::seconds>(
               date.time_since_epoch() + std::chrono::hours{hour})
        .count();
}

bool easternDaylightAtUtc(std::time_t timestamp) {
    return timestamp >= utcTimestamp(2024, 3, 10, 7) &&
           timestamp < utcTimestamp(2024, 11, 3, 6);
}

std::tm easternLocalTime(std::time_t timestamp) {
    const bool daylight = easternDaylightAtUtc(timestamp);
    const std::time_t localSeconds =
        timestamp - (daylight ? 4 : 5) * 60 * 60;
    std::tm local = utcTime(localSeconds);
    local.tm_isdst = daylight ? 1 : 0;
    return local;
}

std::time_t easternTimestamp(std::tm local) {
    const auto month = std::chrono::year{local.tm_year + 1900} /
                       std::chrono::January +
                       std::chrono::months{local.tm_mon};
    const auto day = std::chrono::sys_days{month / std::chrono::day{1}} +
                     std::chrono::days{local.tm_mday - 1};
    const std::time_t localAsUtc =
        std::chrono::duration_cast<std::chrono::seconds>(
            day.time_since_epoch() + std::chrono::hours{local.tm_hour} +
            std::chrono::minutes{local.tm_min} +
            std::chrono::seconds{local.tm_sec})
            .count();
    const bool daylight =
        localAsUtc >= utcTimestamp(2024, 3, 10, 3) &&
        localAsUtc < utcTimestamp(2024, 11, 3, 1);
    return localAsUtc + (daylight ? 4 : 5) * 60 * 60;
}

void weeklyAggregationUsesLocalDstDayBoundaries() {
    const chronox::CalendarTimeConverter converter{
        easternLocalTime, easternTimestamp};
    const auto verifyDstDay = [&](int month, int date,
                                  std::chrono::hours expectedLength) {
        std::tm localNoon{};
        localNoon.tm_year = 2024 - 1900;
        localNoon.tm_mon = month - 1;
        localNoon.tm_mday = date;
        localNoon.tm_hour = 12;
        const Timestamp midday = toMicroseconds(easternTimestamp(localNoon));
        const auto day = chronox::calendarPeriodContaining(
            midday, CalendarPeriod::Day, converter);
        const Timestamp now = day.end - durationTicks(1s);

        TemporaryDatabase file;
        SessionDatabase database(file.path());
        database.setCounting(true, day.start);
        database.setCounting(false, now);
        const auto snapshot =
            chronox::queryDashboardSnapshot(database, now, false, &converter);
        const auto expectedDuration = expectedLength - 1s;
        require(snapshot.todayDuration == expectedDuration,
                "DST day total uses its 23-hour or 25-hour local boundary");
        require(snapshot.weekDays[6] == expectedDuration,
                "weekly chart assigns DST time to Sunday using local day bounds");
        require(snapshot.weekDuration == expectedDuration,
                "weekly total matches the DST session duration");
    };
    verifyDstDay(3, 10, 23h);
    verifyDstDay(11, 3, 25h);
}

void historyPresentationUsesRealPeriodsAndSplitsMidnightSessions() {
    TemporaryDatabase file;
    SessionDatabase database(file.path());
    recordInterval(database, localTime(2024, 6, 11, 23, 30),
                   localTime(2024, 6, 12, 0, 30));
    const Timestamp now = localTime(2024, 6, 12, 12);
    database.setCounting(true, localTime(2024, 6, 12, 11));

    const auto week = chronox::queryDashboardHistory(
        database, CalendarPeriod::Week, localTime(2024, 6, 11, 12), now, true);
    require(week.buckets.size() == 7 && week.daysWithActivity == 2,
            "weekly history exposes seven daily buckets and active-day count");
    require(week.buckets[2].duration == 90min &&
                week.buckets[1].duration == 30min &&
                week.duration == 2h,
            "history splits cross-midnight time and includes the active interval once");
    require(week.sessions.size() == 2 && week.sessions[0].duration == 1h &&
                week.sessions[1].duration == 1h && week.sessions[1].active,
            "history preserves session rows and identifies an open interval");
    require(database.latestRecordedTimestamp() == localTime(2024, 6, 12, 0, 30),
            "querying dashboard history does not write to SQLite");

    const auto overview =
        chronox::queryDashboardSnapshot(database, now, true);
    const auto selectedDay = chronox::queryDashboardHistory(
        database, CalendarPeriod::Day, now, now, true);
    require(selectedDay.duration == overview.todayDuration &&
                selectedDay.sessions.size() == overview.todayIntervals.size() &&
                selectedDay.duration == week.buckets[2].duration,
            "overview and history agree on today's counted duration and intervals");

    const Timestamp future = localTime(2024, 6, 20, 12);
    const auto empty = chronox::queryDashboardHistory(
        database, CalendarPeriod::Day, future, now);
    require(empty.duration == 0s && empty.sessions.empty() &&
                empty.daysWithActivity == 0 && empty.buckets.size() == 1,
            "future empty dates are represented with a zero-valued day bucket");
}

void historyNavigationUsesCalendarBoundaries() {
    const Timestamp march31 = localTime(2024, 3, 31, 12);
    const Timestamp previousMonth = chronox::shiftCalendarPeriod(
        march31, CalendarPeriod::Month, -1);
    const auto previousMonthRange = chronox::calendarPeriodContaining(
        previousMonth, CalendarPeriod::Month);
    require(previousMonthRange.start == localTime(2024, 2, 1, 0),
            "month navigation clamps end-of-month dates into the previous month");

    const Timestamp monday = localTime(2024, 6, 10, 12);
    const auto previousWeek = chronox::calendarPeriodContaining(
        chronox::shiftCalendarPeriod(monday, CalendarPeriod::Week, -1),
        CalendarPeriod::Week);
    require(previousWeek.start == localTime(2024, 6, 3, 0),
            "week navigation advances by full local calendar weeks");
    const auto nextYear = chronox::calendarPeriodContaining(
        chronox::shiftCalendarPeriod(monday, CalendarPeriod::Year, 1),
        CalendarPeriod::Year);
    require(nextYear.start == localTime(2025, 1, 1, 0),
            "year navigation selects the next calendar year");
}

void historyBucketsRespectDstDayLengths() {
    const chronox::CalendarTimeConverter converter{
        easternLocalTime, easternTimestamp};
    std::tm localNoon{};
    localNoon.tm_year = 2024 - 1900;
    localNoon.tm_mon = 3 - 1;
    localNoon.tm_mday = 10;
    localNoon.tm_hour = 12;
    const Timestamp anchor = toMicroseconds(easternTimestamp(localNoon));
    const auto day = chronox::calendarPeriodContaining(
        anchor, CalendarPeriod::Day, converter);
    TemporaryDatabase file;
    SessionDatabase database(file.path());
    database.setCounting(true, day.start);
    database.setCounting(false, day.end);
    const auto report = chronox::queryDashboardHistory(
        database, CalendarPeriod::Day, anchor, day.end, false, &converter);
    require(report.duration == 23h && report.buckets.size() == 1 &&
                report.buckets.front().range.end -
                        report.buckets.front().range.start ==
                    durationTicks(23h),
            "daily history uses the real 23-hour DST calendar day");
}

void measuredOverlapsUseExplicitDurationAndActiveIdentity() {
    TemporaryDatabase file;
    SessionDatabase db(file.path(),chronox::StorageMode::Measured);
    const auto start=localTime(2024,6,15,12);
    chronox::TemporalEngine e(false,{start,0,0});db.updateTemporal(e,true);
    e.observe({start+durationTicks(10min),durationTicks(10min),durationTicks(10min)});
    e.observe({start+durationTicks(5min),durationTicks(10min),durationTicks(10min)});db.updateTemporal(e,true);
    e.observe({start+durationTicks(15min),durationTicks(20min),durationTicks(20min)});db.updateTemporal(e,false);
    const auto snapshot=chronox::queryDashboardSnapshot(db,e.snapshot().utc,true);
    const auto history=chronox::queryDashboardHistory(db,CalendarPeriod::Day,start,e.snapshot().utc,true);
    require(snapshot.todayDuration==20min && history.duration==20min && history.buckets.front().duration==20min,
        "overlapping civil epochs preserve measured contributions in every view");
    require(snapshot.recentSessions.front().active && snapshot.recentSessions.front().duration==10min,
        "active identity survives civil rollback");
}

} // namespace

int main() {
    measuredOverlapsUseExplicitDurationAndActiveIdentity();
    emptyDatabaseProducesEmptyOverview();
    periodsSessionsAndCrossMidnightAreAggregatedFromHistory();
    weeklyAggregationUsesLocalDstDayBoundaries();
    historyPresentationUsesRealPeriodsAndSplitsMidnightSessions();
    historyNavigationUsesCalendarBoundaries();
    historyBucketsRespectDstDayLengths();
    std::cout << "Dashboard data tests passed\n";
    return EXIT_SUCCESS;
}
