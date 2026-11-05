#include "session_database.h"

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace chronox {
namespace {

constexpr int CurrentSchemaVersion = 1;
std::atomic<std::uint64_t> backupSequence{};

std::runtime_error databaseError(sqlite3* database, const char* operation) {
    return std::runtime_error(std::string(operation) + ": " +
                              sqlite3_errmsg(database));
}

void check(sqlite3* database, int result, const char* operation) {
    if (result != SQLITE_OK) {
        throw databaseError(database, operation);
    }
}

void execute(sqlite3* database, const char* sql) {
    char* error = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        const std::string message = error == nullptr ? sqlite3_errmsg(database) : error;
        sqlite3_free(error);
        throw std::runtime_error(std::string("SQLite statement failed: ") + message);
    }
}

class Statement {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        const int result =
            sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            sqlite3_finalize(statement_);
            statement_ = nullptr;
            throw databaseError(database_, "sqlite3_prepare_v2");
        }
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(std::int64_t value, int index) {
        check(database_, sqlite3_bind_int64(statement_, index, value),
              "sqlite3_bind_int64");
    }

    int step() {
        const int result = sqlite3_step(statement_);
        if (result != SQLITE_ROW && result != SQLITE_DONE) {
            throw databaseError(database_, "sqlite3_step");
        }
        return result;
    }

    std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_, column);
    }

    std::string text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        const int length = sqlite3_column_bytes(statement_, column);
        return value == nullptr
                   ? std::string{}
                   : std::string(reinterpret_cast<const char*>(value),
                                 static_cast<std::size_t>(length));
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{};
};

class Transaction {
public:
    explicit Transaction(sqlite3* database) : database_(database) {
        execute(database_, "BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (!committed_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    void commit() {
        execute(database_, "COMMIT");
        committed_ = true;
    }

private:
    sqlite3* database_;
    bool committed_{};
};

} // namespace

struct SessionDatabase::Impl {
    explicit Impl(const std::filesystem::path& path) {
        databasePath = path;
#if defined(_WIN32)
        const int result = sqlite3_open16(path.c_str(), &database);
#else
        const int result = sqlite3_open_v2(path.c_str(), &database,
                                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                               SQLITE_OPEN_FULLMUTEX,
                                           nullptr);
#endif
        if (result != SQLITE_OK) {
            const std::string message = database == nullptr
                                            ? "Unable to open SQLite database"
                                            : sqlite3_errmsg(database);
            sqlite3_close(database);
            database = nullptr;
            throw std::runtime_error("Opening Chronox database failed: " + message);
        }

        try {
            check(database, sqlite3_busy_timeout(database, 5000),
                  "sqlite3_busy_timeout");
            execute(database, "PRAGMA foreign_keys = ON");
            verifyIntegrity();
            execute(database, "PRAGMA journal_mode = DELETE");
            execute(database, "PRAGMA synchronous = FULL");
            migrate();
            recoverInterruptedInterval();
        } catch (...) {
            sqlite3_close(database);
            database = nullptr;
            throw;
        }
    }

    ~Impl() {
        if (database != nullptr) {
            sqlite3_close(database);
        }
    }

    void migrate() {
        std::int64_t version{};
        {
            Statement versionQuery(database, "PRAGMA user_version");
            if (versionQuery.step() != SQLITE_ROW) {
                throw databaseError(database, "Reading SQLite schema version");
            }
            version = versionQuery.integer(0);
        }
        if (version < 0) {
            throw std::runtime_error("Chronox database schema version is invalid");
        }
        if (version > CurrentSchemaVersion) {
            throw std::runtime_error("Chronox database schema is newer than this version");
        }
        if (version == CurrentSchemaVersion) {
            return;
        }

        Transaction transaction(database);
        execute(database,
                "CREATE TABLE session_intervals ("
                "id INTEGER PRIMARY KEY,"
                "started_at_us INTEGER NOT NULL,"
                "checkpoint_at_us INTEGER NOT NULL,"
                "ended_at_us INTEGER,"
                "CHECK(checkpoint_at_us >= started_at_us),"
                "CHECK(ended_at_us IS NULL OR ended_at_us >= checkpoint_at_us)"
                ")");
        execute(database,
                "CREATE UNIQUE INDEX one_open_session_interval "
                "ON session_intervals(ended_at_us) WHERE ended_at_us IS NULL");
        execute(database,
                "CREATE INDEX session_intervals_by_start "
                "ON session_intervals(started_at_us)");
        execute(database,
                "CREATE INDEX session_intervals_by_end "
                "ON session_intervals(ended_at_us)");
        execute(database, "PRAGMA user_version = 1");
        transaction.commit();
    }

    void verifyIntegrity() {
        Statement integrity(database, "PRAGMA quick_check");
        bool foundResult = false;
        while (integrity.step() == SQLITE_ROW) {
            foundResult = true;
            const std::string result = integrity.text(0);
            if (result != "ok") {
                throw std::runtime_error(
                    "Chronox database integrity check failed: " + result);
            }
        }
        if (!foundResult) {
            throw std::runtime_error(
                "Chronox database integrity check returned no result");
        }
    }

    void recoverInterruptedInterval() {
        Transaction transaction(database);
        execute(database,
                "UPDATE session_intervals "
                "SET ended_at_us = checkpoint_at_us "
                "WHERE ended_at_us IS NULL AND checkpoint_at_us > started_at_us");
        execute(database,
                "DELETE FROM session_intervals "
                "WHERE ended_at_us IS NULL AND checkpoint_at_us = started_at_us");
        transaction.commit();
    }

    void backupTo(const std::filesystem::path& destination) const {
        std::error_code error;
        const auto absoluteSource = std::filesystem::absolute(databasePath, error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "Could not resolve the Chronox database path", databasePath, error);
        }
        const auto absoluteDestination =
            std::filesystem::absolute(destination, error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "Could not resolve the Chronox backup path", destination, error);
        }
        if (absoluteSource.lexically_normal() ==
            absoluteDestination.lexically_normal()) {
            throw std::invalid_argument(
                "Chronox backup destination must differ from the open database");
        }
        const bool destinationExists = std::filesystem::exists(destination, error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "Could not inspect the Chronox backup destination", destination,
                error);
        }
        if (destinationExists) {
            throw std::runtime_error(
                "Chronox backup destination already exists");
        }

        auto temporaryPath = destination;
        temporaryPath +=
            ".tmp-" +
            std::to_string(std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()) +
            "-" + std::to_string(backupSequence.fetch_add(1));

        const auto encodedPath = temporaryPath.u8string();
        const auto* utf8Path =
            reinterpret_cast<const char*>(encodedPath.c_str());
        sqlite3* backupDatabase = nullptr;
        const int openResult =
            sqlite3_open_v2(utf8Path, &backupDatabase,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                SQLITE_OPEN_EXCLUSIVE | SQLITE_OPEN_FULLMUTEX,
                            nullptr);
        if (openResult != SQLITE_OK) {
            const std::string message =
                backupDatabase == nullptr
                    ? sqlite3_errstr(openResult)
                    : sqlite3_errmsg(backupDatabase);
            sqlite3_close(backupDatabase);
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            throw std::runtime_error("Creating Chronox backup failed: " + message);
        }

        sqlite3_backup* backup =
            sqlite3_backup_init(backupDatabase, "main", database, "main");
        if (backup == nullptr) {
            const std::string message = sqlite3_errmsg(backupDatabase);
            sqlite3_close(backupDatabase);
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            throw std::runtime_error(
                "Starting Chronox backup failed: " + message +
                (cleanupError ? "; temporary file cleanup failed: " +
                                    cleanupError.message()
                              : std::string{}));
        }

        int stepResult = SQLITE_OK;
        do {
            stepResult = sqlite3_backup_step(backup, -1);
            if (stepResult == SQLITE_BUSY || stepResult == SQLITE_LOCKED) {
                sqlite3_sleep(10);
            }
        } while (stepResult == SQLITE_BUSY || stepResult == SQLITE_LOCKED);
        const int finishResult = sqlite3_backup_finish(backup);
        const std::string stepError =
            stepResult == SQLITE_DONE ? std::string{}
                                      : sqlite3_errmsg(backupDatabase);
        const std::string finishError =
            finishResult == SQLITE_OK ? std::string{}
                                      : sqlite3_errmsg(backupDatabase);
        const int closeResult = sqlite3_close(backupDatabase);
        const std::string closeError =
            closeResult == SQLITE_OK ? std::string{} : sqlite3_errstr(closeResult);

        if (stepResult != SQLITE_DONE || finishResult != SQLITE_OK ||
            closeResult != SQLITE_OK) {
            if (closeResult != SQLITE_OK) {
                sqlite3_close_v2(backupDatabase);
            }
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            std::string message = stepError.empty()
                                      ? (finishError.empty() ? closeError
                                                             : finishError)
                                      : stepError;
            if (cleanupError) {
                message += "; temporary file cleanup failed: " +
                           cleanupError.message();
            }
            throw std::runtime_error(
                "Completing Chronox backup failed: " + message);
        }

        std::filesystem::rename(temporaryPath, destination, error);
        if (error) {
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            throw std::runtime_error(
                "Publishing Chronox backup failed: " + error.message() +
                (cleanupError ? "; temporary file cleanup failed: " +
                                    cleanupError.message()
                              : std::string{}));
        }
    }

    sqlite3* database{};
    std::filesystem::path databasePath;
    std::optional<std::int64_t> activeInterval;
};

SessionDatabase::SessionDatabase(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {}

SessionDatabase::~SessionDatabase() = default;
SessionDatabase::SessionDatabase(SessionDatabase&&) noexcept = default;
SessionDatabase& SessionDatabase::operator=(SessionDatabase&&) noexcept = default;

void SessionDatabase::setCounting(bool counting, Timestamp timestamp) {
    if (counting == impl_->activeInterval.has_value()) {
        return;
    }

    Transaction transaction(impl_->database);
    std::optional<std::int64_t> nextActiveInterval;
    if (impl_->activeInterval.has_value()) {
        Statement closeInterval(
            impl_->database,
            "UPDATE session_intervals "
            "SET ended_at_us = ?1, checkpoint_at_us = ?1 "
            "WHERE id = ?2 AND ended_at_us IS NULL AND checkpoint_at_us <= ?1");
        closeInterval.bind(timestamp, 1);
        closeInterval.bind(*impl_->activeInterval, 2);
        closeInterval.step();
        if (sqlite3_changes(impl_->database) != 1) {
            throw std::runtime_error(
                "Could not close the active Chronox interval at a monotonic timestamp");
        }
    }

    if (counting) {
        Statement openInterval(
            impl_->database,
            "INSERT INTO session_intervals(started_at_us, checkpoint_at_us) "
            "VALUES(?1, ?1)");
        openInterval.bind(timestamp, 1);
        openInterval.step();
        nextActiveInterval = sqlite3_last_insert_rowid(impl_->database);
    }

    transaction.commit();
    impl_->activeInterval = nextActiveInterval;
}

void SessionDatabase::checkpoint(Timestamp timestamp) {
    if (!impl_->activeInterval.has_value()) {
        return;
    }

    Statement update(impl_->database,
                     "UPDATE session_intervals SET checkpoint_at_us = ?1 "
                     "WHERE id = ?2 AND ended_at_us IS NULL AND checkpoint_at_us < ?1");
    update.bind(timestamp, 1);
    update.bind(*impl_->activeInterval, 2);
    update.step();
}

void SessionDatabase::backupTo(
    const std::filesystem::path& destination) const {
    impl_->backupTo(destination);
}

Timestamp SessionDatabase::latestRecordedTimestamp() const {
    Statement query(impl_->database,
                    "SELECT COALESCE(MAX(ended_at_us), 0) FROM session_intervals");
    if (query.step() != SQLITE_ROW) {
        throw databaseError(impl_->database, "Reading latest Chronox interval");
    }
    return query.integer(0);
}

std::chrono::microseconds SessionDatabase::duration(CalendarPeriod period,
                                                     Timestamp now) const {
    const TimeRange range = calendarPeriodContaining(now, period);
    return duration(range, now);
}

std::chrono::microseconds SessionDatabase::duration(
    CalendarPeriod period,
    Timestamp now,
    const CalendarTimeConverter& converter) const {
    const TimeRange range = calendarPeriodContaining(now, period, converter);
    return duration(range, now);
}

HistoryReport SessionDatabase::queryHistory(CalendarPeriod period,
                                            Timestamp dateAnchor,
                                            Timestamp now) const {
    return queryHistory(calendarPeriodContaining(dateAnchor, period), period, now,
                        nullptr);
}

HistoryReport SessionDatabase::queryHistory(
    CalendarPeriod period,
    Timestamp dateAnchor,
    Timestamp now,
    const CalendarTimeConverter& converter) const {
    return queryHistory(calendarPeriodContaining(dateAnchor, period, converter),
                        period, now, &converter);
}

HistoryReport SessionDatabase::queryHistory(TimeRange range,
                                            CalendarPeriod period,
                                            Timestamp now,
                                            const CalendarTimeConverter* converter) const {
    const Timestamp effectiveEnd = std::min(range.end, now);
    Statement query(
        impl_->database,
        "SELECT max(started_at_us, ?1), "
        "min(COALESCE(ended_at_us, ?2), ?3) "
        "FROM session_intervals "
        "WHERE started_at_us < ?3 "
        "AND (ended_at_us IS NULL OR ended_at_us > ?1) "
        "ORDER BY started_at_us, id");
    query.bind(range.start, 1);
    query.bind(now, 2);
    query.bind(effectiveEnd, 3);

    HistoryReport report{period, range};
    while (query.step() == SQLITE_ROW) {
        const Timestamp start = query.integer(0);
        const Timestamp end = query.integer(1);
        if (end <= start) {
            continue;
        }
        report.sessionIntervals.push_back({start, end});
        report.sessionDuration += std::chrono::microseconds(end - start);
    }

    Timestamp dayStart = range.start;
    std::size_t intervalIndex = 0;
    while (dayStart < effectiveEnd) {
        const TimeRange dayRange =
            converter == nullptr
                ? calendarPeriodContaining(dayStart, CalendarPeriod::Day)
                : calendarPeriodContaining(dayStart, CalendarPeriod::Day,
                                           *converter);
        while (intervalIndex < report.sessionIntervals.size() &&
               report.sessionIntervals[intervalIndex].end <= dayRange.start) {
            ++intervalIndex;
        }
        const bool hasDuration =
            intervalIndex < report.sessionIntervals.size() &&
            report.sessionIntervals[intervalIndex].start < dayRange.end &&
            report.sessionIntervals[intervalIndex].end > dayRange.start;
        if (hasDuration) {
            ++report.daysWithDuration;
        }
        if (dayRange.end <= dayStart) {
            throw std::runtime_error(
                "Calendar day did not advance while counting Chronox days");
        }
        dayStart = dayRange.end;
    }
    return report;
}

std::chrono::microseconds SessionDatabase::duration(TimeRange range,
                                                     Timestamp now) const {
    Statement query(
        impl_->database,
        "SELECT "
        "(SELECT COALESCE(SUM(min(ended_at_us, ?3) - max(started_at_us, ?2)), 0) "
        " FROM session_intervals "
        " WHERE ended_at_us IS NOT NULL AND ended_at_us > ?2 AND started_at_us < ?3) "
        "+ "
        "(SELECT COALESCE(SUM("
        " min(max(checkpoint_at_us, ?1), ?3) - max(started_at_us, ?2)), 0) "
        " FROM session_intervals "
        " WHERE ended_at_us IS NULL AND started_at_us < ?3 "
        " AND max(checkpoint_at_us, ?1) > ?2)");
    query.bind(now, 1);
    query.bind(range.start, 2);
    query.bind(range.end, 3);
    if (query.step() != SQLITE_ROW) {
        throw databaseError(impl_->database, "Calculating Chronox duration");
    }
    return std::chrono::microseconds(query.integer(0));
}

} // namespace chronox
