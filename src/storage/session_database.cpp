#include "session_database.h"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace chronox {
namespace {

constexpr int CurrentSchemaVersion = 1;

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

    sqlite3* database{};
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
