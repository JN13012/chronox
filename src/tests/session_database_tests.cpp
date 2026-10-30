#include "calendar_period.h"
#include "session_database.h"

#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
                     ("chronox-tests-" +
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

std::time_t localTimestamp(int year, int month, int day, int hour) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_isdst = -1;
    const auto result = std::mktime(&value);
    require(result != static_cast<std::time_t>(-1), "local test timestamp is valid");
    return result;
}

Timestamp toMicroseconds(std::time_t timestamp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::seconds(timestamp))
        .count();
}

std::tm utcTime(std::time_t timestamp) {
    std::tm result{};
#if defined(_WIN32)
    if (gmtime_s(&result, &timestamp) != 0) {
        throw std::runtime_error("Could not convert UTC diagnostic timestamp");
    }
#else
    if (gmtime_r(&timestamp, &result) == nullptr) {
        throw std::runtime_error("Could not convert UTC diagnostic timestamp");
    }
#endif
    return result;
}

std::string describeTimestamp(
    Timestamp timestamp,
    const chronox::CalendarTimeConverter* converter = nullptr) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto time = static_cast<std::time_t>(seconds.count());
    std::tm local{};
    std::tm utc{};
#if defined(_WIN32)
    if (converter != nullptr) {
        local = converter->toLocal(time);
    } else if (localtime_s(&local, &time) != 0) {
        throw std::runtime_error("Could not format DST diagnostic timestamp");
    }
#else
    if (converter != nullptr) {
        local = converter->toLocal(time);
    } else if (localtime_r(&time, &local) == nullptr) {
        throw std::runtime_error("Could not format DST diagnostic timestamp");
    }
#endif
    utc = utcTime(time);

    std::ostringstream description;
    description << "epoch_us=" << timestamp << " UTC="
                << std::put_time(&utc, "%Y-%m-%d %H:%M:%S") << " local="
                << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
                << " local_tm_isdst=" << local.tm_isdst;
    return description.str();
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
        timestamp - std::chrono::hours{daylight ? 4 : 5}.count() * 60 * 60;
    std::tm local = utcTime(localSeconds);
    local.tm_isdst = daylight ? 1 : 0;
    return local;
}

std::time_t easternTimestamp(std::tm local) {
    const auto month = std::chrono::year{local.tm_year + 1900} /
                       std::chrono::January + std::chrono::months{local.tm_mon};
    const auto day = std::chrono::sys_days{month / std::chrono::day{1}} +
                     std::chrono::days{local.tm_mday - 1};
    const std::time_t localAsUtc =
        std::chrono::duration_cast<std::chrono::seconds>(
            day.time_since_epoch() + std::chrono::hours{local.tm_hour} +
            std::chrono::minutes{local.tm_min} +
            std::chrono::seconds{local.tm_sec})
            .count();
    const std::time_t daylightStart = utcTimestamp(2024, 3, 10, 3);
    const std::time_t daylightEnd = utcTimestamp(2024, 11, 3, 1);
    const bool daylight =
        localAsUtc >= daylightStart && localAsUtc < daylightEnd;
    return localAsUtc +
           std::chrono::hours{daylight ? 4 : 5}.count() * 60 * 60;
}

template <typename Rep, typename Period>
Timestamp toTimestampTicks(std::chrono::duration<Rep, Period> duration) {
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

void transitionsAreDurableAndIdempotent() {
    TemporaryDatabase file;
    const auto today =
        chronox::calendarPeriodContaining(toMicroseconds(std::time(nullptr)),
                                          CalendarPeriod::Day);
    const Timestamp start = today.start + toTimestampTicks(60s);
    {
        SessionDatabase database(file.path());
        database.setCounting(true, start);
        database.setCounting(true, start + toTimestampTicks(30s));
        database.setCounting(false, start + toTimestampTicks(60s));
        database.setCounting(false, start + toTimestampTicks(90s));
        database.setCounting(true, start + toTimestampTicks(10min));
        database.setCounting(false, start + toTimestampTicks(20min));
        require(database.duration(CalendarPeriod::Day,
                                  start + toTimestampTicks(21min)) == 11min,
                "lock and wake intervals are accumulated without duplicates");
        require(database.latestRecordedTimestamp() == start + toTimestampTicks(20min),
                "the latest persisted timestamp is available for clock adjustments");
    }
    {
        SessionDatabase database(file.path());
        require(database.duration(CalendarPeriod::Day,
                                  start + toTimestampTicks(21min)) == 11min,
                "completed intervals survive database reopening");
        require(database.latestRecordedTimestamp() == start + toTimestampTicks(20min),
                "the timestamp floor survives a process restart");
    }
}

void interruptedIntervalsStopAtTheirLastCheckpoint() {
    TemporaryDatabase file;
    const auto today =
        chronox::calendarPeriodContaining(toMicroseconds(std::time(nullptr)),
                                          CalendarPeriod::Day);
    const Timestamp start = today.start + toTimestampTicks(60s);
    {
        SessionDatabase database(file.path());
        database.setCounting(true, start);
        database.checkpoint(start + toTimestampTicks(45s));
    }
    {
        SessionDatabase recovered(file.path());
        require(recovered.duration(CalendarPeriod::Day,
                                   start + toTimestampTicks(3h)) == 45s,
                "crashed sessions stop at the most recent durable checkpoint");
        recovered.setCounting(true, start + toTimestampTicks(3h));
        recovered.setCounting(false, start + toTimestampTicks(4h));
    }
    {
        SessionDatabase restarted(file.path());
        require(restarted.duration(CalendarPeriod::Day,
                                   start + toTimestampTicks(5h)) == 1h + 45s,
                "time between a crash and restart is never counted");
    }
}

void sessionsCrossingMidnightBelongToBothCalendarDays() {
    TemporaryDatabase file;
    const auto today =
        chronox::calendarPeriodContaining(toMicroseconds(std::time(nullptr)),
                                          CalendarPeriod::Day);
    const Timestamp midnight = today.start;
    {
        SessionDatabase database(file.path());
        database.setCounting(true, midnight - toTimestampTicks(10min));
        database.setCounting(false, midnight + toTimestampTicks(10min));
        require(database.duration(CalendarPeriod::Day,
                                  midnight + toTimestampTicks(12h)) == 10min,
                "the part after midnight belongs to the new day");
        require(database.duration(CalendarPeriod::Day, midnight - 1) == 10min,
                "the part before midnight belongs to the previous day");
    }
}

void recentStatisticsIgnoreLargeHistoricalIntervals() {
    TemporaryDatabase file;
    const auto today =
        chronox::calendarPeriodContaining(toMicroseconds(std::time(nullptr)),
                                          CalendarPeriod::Day);
    SessionDatabase database(file.path());

    sqlite3* rawDatabase = nullptr;
#if defined(_WIN32)
    int result = sqlite3_open16(file.path().c_str(), &rawDatabase);
#else
    int result = sqlite3_open_v2(file.path().c_str(), &rawDatabase,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr);
#endif
    require(result == SQLITE_OK, "historical test database opens");
    result = sqlite3_exec(rawDatabase, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    require(result == SQLITE_OK, "historical test transaction begins");
    sqlite3_stmt* insert = nullptr;
    result = sqlite3_prepare_v2(
        rawDatabase,
        "WITH RECURSIVE sequence(value) AS ("
        "VALUES(0) UNION ALL SELECT value + 1 FROM sequence WHERE value < 99999"
        ") "
        "INSERT INTO session_intervals(started_at_us, checkpoint_at_us, ended_at_us) "
        "SELECT ?1 + value * ?2 + ?3, ?1 + value * ?2 + ?4, "
        "?1 + value * ?2 + ?4 FROM sequence",
        -1, &insert, nullptr);
    require(result == SQLITE_OK, "historical intervals can be inserted");
    const auto intervalStep = toTimestampTicks(1h);
    const auto intervalDuration = toTimestampTicks(5min);
    const auto firstStart = today.start - 100'000 * intervalStep;
    require(sqlite3_bind_int64(insert, 1, firstStart) == SQLITE_OK &&
                sqlite3_bind_int64(insert, 2, intervalStep) == SQLITE_OK &&
                sqlite3_bind_int64(insert, 3, 0) == SQLITE_OK &&
                sqlite3_bind_int64(insert, 4, intervalDuration) == SQLITE_OK,
            "historical interval parameters bind");
    require(sqlite3_step(insert) == SQLITE_DONE, "historical intervals are inserted");
    sqlite3_finalize(insert);
    result = sqlite3_exec(rawDatabase, "COMMIT", nullptr, nullptr, nullptr);
    require(result == SQLITE_OK, "historical test transaction commits");
    require(sqlite3_close(rawDatabase) == SQLITE_OK, "historical test database closes");

    const auto start = today.start + toTimestampTicks(60s);
    database.setCounting(true, start);
    database.checkpoint(start + toTimestampTicks(45s));
    require(database.duration(CalendarPeriod::Day, start + toTimestampTicks(45s)) == 45s,
            "daily reports include the active interval and exclude 100,000 older ones");
}

void newerDatabaseSchemasAreNotOverwritten() {
    TemporaryDatabase file;
    sqlite3* database = nullptr;
#if defined(_WIN32)
    int result = sqlite3_open16(file.path().c_str(), &database);
#else
    int result = sqlite3_open_v2(file.path().c_str(), &database,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
#endif
    require(result == SQLITE_OK, "future-schema test database opens");
    result = sqlite3_exec(database, "PRAGMA user_version = 99", nullptr, nullptr, nullptr);
    require(result == SQLITE_OK, "future-schema version is written");
    require(sqlite3_close(database) == SQLITE_OK, "future-schema database closes");

    bool rejected = false;
    try {
        SessionDatabase unsupported(file.path());
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "a database from a newer application version is rejected");

#if defined(_WIN32)
    result = sqlite3_open16(file.path().c_str(), &database);
#else
    result = sqlite3_open_v2(file.path().c_str(), &database, SQLITE_OPEN_READWRITE, nullptr);
#endif
    require(result == SQLITE_OK, "future-schema database can be reopened");
    sqlite3_stmt* statement = nullptr;
    result = sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &statement, nullptr);
    require(result == SQLITE_OK, "future-schema version can be queried");
    require(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 99,
            "rejected schema version remains unchanged");
    sqlite3_finalize(statement);
    require(sqlite3_close(database) == SQLITE_OK, "future-schema database closes again");
}

void allCalendarPeriodsAggregateTheirFullRange() {
    constexpr std::array periods{
        CalendarPeriod::Day,
        CalendarPeriod::Week,
        CalendarPeriod::Month,
        CalendarPeriod::Year,
    };
    const Timestamp reference = toMicroseconds(localTimestamp(2024, 6, 15, 12));
    for (const auto period : periods) {
        TemporaryDatabase file;
        const auto range = chronox::calendarPeriodContaining(reference, period);
        SessionDatabase database(file.path());
        database.setCounting(true, range.start);
        database.setCounting(false, range.end);
        require(database.duration(period, reference) ==
                    std::chrono::microseconds(range.end - range.start),
                "day, week, month and year reports cover their full local period");
    }
}

void deterministicEasternConverterPreservesDstPeriods() {
    const chronox::CalendarTimeConverter converter{
        easternLocalTime, easternTimestamp};
    for (const auto& test : {
             std::pair{std::array{2024, 3, 10}, 23h},
             std::pair{std::array{2024, 11, 3}, 25h},
         }) {
        std::tm localNoon{};
        localNoon.tm_year = test.first[0] - 1900;
        localNoon.tm_mon = test.first[1] - 1;
        localNoon.tm_mday = test.first[2];
        localNoon.tm_hour = 12;
        const Timestamp midday = toMicroseconds(easternTimestamp(localNoon));
        const auto day = chronox::calendarPeriodContaining(
            midday, CalendarPeriod::Day, converter);
        const auto week = chronox::calendarPeriodContaining(
            midday, CalendarPeriod::Week, converter);
        const auto expectedWeek = test.second + 6 * 24h;
        require(std::chrono::microseconds(day.end - day.start) == test.second,
                "deterministic DST day has its 23-hour or 25-hour boundary");
        require(std::chrono::microseconds(week.end - week.start) == expectedWeek,
                "deterministic DST week has its 167-hour or 169-hour boundary");

        TemporaryDatabase dailyFile;
        SessionDatabase dailyDatabase(dailyFile.path());
        dailyDatabase.setCounting(true, day.start);
        dailyDatabase.setCounting(false, day.end);
        require(dailyDatabase.duration(CalendarPeriod::Day, midday, converter) ==
                    test.second,
                "deterministic DST day report includes its full local day");

        TemporaryDatabase weeklyFile;
        SessionDatabase weeklyDatabase(weeklyFile.path());
        weeklyDatabase.setCounting(true, week.start);
        weeklyDatabase.setCounting(false, week.end);
        require(weeklyDatabase.duration(CalendarPeriod::Week, midday, converter) ==
                    expectedWeek,
                "deterministic DST week report includes its full local week");
    }
}

void daylightSavingUsesLocalCalendarBoundaries() {
#if defined(_WIN32)
    const chronox::CalendarTimeConverter deterministicEastern{
        easternLocalTime, easternTimestamp};
    const auto* converter = &deterministicEastern;
#else
    require(setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1) == 0,
            "test time zone can be set");
    tzset();
    const chronox::CalendarTimeConverter* converter = nullptr;
#endif

    const auto calendarRange = [converter](Timestamp timestamp, CalendarPeriod period) {
        if (converter != nullptr) {
            return chronox::calendarPeriodContaining(timestamp, period, *converter);
        }
        return chronox::calendarPeriodContaining(timestamp, period);
    };
    const auto periodDuration = [converter](const SessionDatabase& database,
                                            CalendarPeriod period,
                                            Timestamp timestamp) {
        if (converter != nullptr) {
            return database.duration(period, timestamp, *converter);
        }
        return database.duration(period, timestamp);
    };

    for (const auto& test : {
             std::pair{std::array{2024, 3, 10}, 23h},
             std::pair{std::array{2024, 11, 3}, 25h},
         }) {
        TemporaryDatabase file;
#if defined(_WIN32)
        std::tm localNoon{};
        localNoon.tm_year = test.first[0] - 1900;
        localNoon.tm_mon = test.first[1] - 1;
        localNoon.tm_mday = test.first[2];
        localNoon.tm_hour = 12;
        const Timestamp midday = toMicroseconds(easternTimestamp(localNoon));
#else
        const Timestamp midday = toMicroseconds(
            localTimestamp(test.first[0], test.first[1], test.first[2], 12));
#endif
        const auto range = calendarRange(midday, CalendarPeriod::Day);
        const auto dayDuration = std::chrono::microseconds(range.end - range.start);
        const auto week = calendarRange(midday, CalendarPeriod::Week);
        const auto weekDuration = std::chrono::microseconds(week.end - week.start);
        SessionDatabase database(file.path());
        database.setCounting(true, range.start);
        database.setCounting(false, range.end);
        const auto dayReport = periodDuration(database, CalendarPeriod::Day, midday);
        TemporaryDatabase weeklyFile;
        SessionDatabase weeklyDatabase(weeklyFile.path());
        weeklyDatabase.setCounting(true, week.start);
        weeklyDatabase.setCounting(false, week.end);
        const auto weekReport = periodDuration(weeklyDatabase, CalendarPeriod::Week, midday);
        const auto expectedWeek = test.second + 6 * 24h;

        if (dayDuration != test.second || weekDuration != expectedWeek ||
            dayReport != test.second || weekReport != expectedWeek) {
#if defined(_WIN32)
            std::cerr << "DST diagnostics: timezone_source=deterministic-US-Eastern-2024";
#else
            const char* configuredTimezone = std::getenv("TZ");
            std::cerr << "DST diagnostics: TZ="
                      << (configuredTimezone == nullptr ? "<unset>" : configuredTimezone);
#endif
            std::cerr << " requested_local_date=" << test.first[0] << '-'
                      << std::setfill('0') << std::setw(2) << test.first[1] << '-'
                      << std::setw(2) << test.first[2] << std::setfill(' ')
                      << " expected_day_seconds="
                      << std::chrono::duration_cast<std::chrono::seconds>(test.second).count()
                      << " actual_day_seconds="
                      << std::chrono::duration_cast<std::chrono::seconds>(dayDuration).count()
                      << " expected_week_seconds="
                      << std::chrono::duration_cast<std::chrono::seconds>(expectedWeek).count()
                      << " actual_week_seconds="
                      << std::chrono::duration_cast<std::chrono::seconds>(weekDuration).count()
                      << " day_report_seconds="
                      << std::chrono::duration_cast<std::chrono::seconds>(dayReport).count()
                      << " week_report_seconds="
                      << std::chrono::duration_cast<std::chrono::seconds>(weekReport).count()
                      << '\n'
                      << "  midday: " << describeTimestamp(midday, converter) << '\n'
                      << "  day start: " << describeTimestamp(range.start, converter) << '\n'
                      << "  day end: " << describeTimestamp(range.end, converter) << '\n'
                      << "  week start: " << describeTimestamp(week.start, converter) << '\n'
                      << "  week end: " << describeTimestamp(week.end, converter) << '\n';
        }

        require(dayDuration == test.second,
                "DST day follows its 23-hour or 25-hour local boundary");
        require(weekDuration == expectedWeek,
                "DST week follows its 167-hour or 169-hour local boundary");
        require(dayReport == test.second,
                "DST day report includes exactly the local day's elapsed time");
        require(weekReport == expectedWeek,
                "DST week report includes exactly the local week's elapsed time");
    }

#if !defined(_WIN32)
    unsetenv("TZ");
    tzset();
#endif
}

} // namespace

int main() {
    transitionsAreDurableAndIdempotent();
    interruptedIntervalsStopAtTheirLastCheckpoint();
    sessionsCrossingMidnightBelongToBothCalendarDays();
    recentStatisticsIgnoreLargeHistoricalIntervals();
    newerDatabaseSchemasAreNotOverwritten();
    allCalendarPeriodsAggregateTheirFullRange();
    deterministicEasternConverterPreservesDstPeriods();
    daylightSavingUsesLocalCalendarBoundaries();
    std::cout << "All session database tests passed.\n";
}
