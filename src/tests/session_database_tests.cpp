#include "calendar_period.h"
#include "session_database.h"

#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <stdexcept>
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

void daylightSavingUsesLocalCalendarBoundaries() {
#if defined(_WIN32)
    require(_putenv_s("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2") == 0,
            "test time zone can be set");
    _tzset();
#else
    require(setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1) == 0,
            "test time zone can be set");
    tzset();
#endif

    for (const auto& test : {
             std::pair{std::array{2024, 3, 10}, 23h},
             std::pair{std::array{2024, 11, 3}, 25h},
         }) {
        TemporaryDatabase file;
        const Timestamp midday = toMicroseconds(
            localTimestamp(test.first[0], test.first[1], test.first[2], 12));
        const auto range = chronox::calendarPeriodContaining(midday, CalendarPeriod::Day);
        require(std::chrono::microseconds(range.end - range.start) == test.second,
                "DST day follows its 23-hour or 25-hour local boundary");
        const auto week = chronox::calendarPeriodContaining(midday, CalendarPeriod::Week);
        require(std::chrono::microseconds(week.end - week.start) ==
                    test.second + 6 * 24h,
                "DST week follows its 167-hour or 169-hour local boundary");
        SessionDatabase database(file.path());
        database.setCounting(true, range.start);
        database.setCounting(false, range.end);
        require(database.duration(CalendarPeriod::Day, midday) == test.second,
                "DST day report includes exactly the local day's elapsed time");
        TemporaryDatabase weeklyFile;
        const auto weekRange =
            chronox::calendarPeriodContaining(midday, CalendarPeriod::Week);
        SessionDatabase weeklyDatabase(weeklyFile.path());
        weeklyDatabase.setCounting(true, weekRange.start);
        weeklyDatabase.setCounting(false, weekRange.end);
        require(weeklyDatabase.duration(CalendarPeriod::Week, midday) ==
                    test.second + 6 * 24h,
                "DST week report includes exactly the local week's elapsed time");
    }

#if defined(_WIN32)
    _putenv_s("TZ", "");
    _tzset();
#else
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
    daylightSavingUsesLocalCalendarBoundaries();
    std::cout << "All session database tests passed.\n";
}
