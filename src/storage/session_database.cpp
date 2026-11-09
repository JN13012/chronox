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

constexpr int CurrentSchemaVersion = 2;
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
    explicit Impl(const std::filesystem::path& path, StorageMode selected) : mode(selected) {
        databasePath = path;
        const auto encoded = path.u8string();
        const int flags = mode == StorageMode::ReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        const int result = sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()), &database,
                                         flags | SQLITE_OPEN_FULLMUTEX, nullptr);
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
            if (mode == StorageMode::ReadOnly) execute(database, "PRAGMA query_only=ON");
            else {
                execute(database, "PRAGMA journal_mode = DELETE");
                execute(database, "PRAGMA synchronous = FULL");
            }
            migrate();
            if (hasMeasured) {
                Statement q(database,"SELECT COALESCE(SUM(uncertainty_count),0) FROM temporal_runs");q.step();priorUncertainties=q.integer(0);
            }
            if (mode == StorageMode::Measured) execute(database, "UPDATE measured_spans SET active=0 WHERE active=1");
            else if (mode == StorageMode::Legacy && !hasMeasured) recoverInterruptedInterval();
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
        hasMeasured = version == 2;
        if (mode == StorageMode::ReadOnly) {
            if (version == 0) throw std::runtime_error("Not a Chronox database");
            return;
        }
        if (version == 2) return;
        if (version == 1) {
            if (mode == StorageMode::Measured) migrateMeasured();
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
        if (mode == StorageMode::Measured) migrateMeasured();
    }

    void migrateMeasured() {
        Transaction tx(database);
        execute(database, "CREATE TABLE temporal_runs(id INTEGER PRIMARY KEY, started_utc_us INTEGER NOT NULL, uncertainty_count INTEGER NOT NULL DEFAULT 0 CHECK(uncertainty_count>=0))");
        execute(database, "CREATE TABLE measured_spans("
            "run_id INTEGER NOT NULL REFERENCES temporal_runs(id), seq INTEGER NOT NULL,"
            "epoch INTEGER NOT NULL, session INTEGER NOT NULL, utc_start INTEGER NOT NULL, utc_end INTEGER NOT NULL,"
            "mono_start INTEGER NOT NULL, mono_end INTEGER NOT NULL, duration_us INTEGER NOT NULL, active INTEGER NOT NULL CHECK(active IN(0,1)),"
            "PRIMARY KEY(run_id,seq), CHECK(mono_end>=mono_start), CHECK(duration_us=mono_end-mono_start),"
            "CHECK(utc_end>=utc_start), CHECK(utc_end-utc_start=duration_us))");
        execute(database, "CREATE UNIQUE INDEX one_measured_active ON measured_spans((1)) WHERE active=1");
        execute(database, "CREATE INDEX measured_civil_end ON measured_spans(utc_end)");
        execute(database, "CREATE TRIGGER measured_no_overlap_insert BEFORE INSERT ON measured_spans "
            "WHEN EXISTS(SELECT 1 FROM measured_spans WHERE run_id=NEW.run_id AND seq<>NEW.seq "
            "AND mono_start<NEW.mono_end AND mono_end>NEW.mono_start) BEGIN SELECT RAISE(ABORT,'monotonic overlap'); END");
        execute(database, "CREATE TRIGGER measured_no_overlap_update BEFORE UPDATE ON measured_spans "
            "WHEN EXISTS(SELECT 1 FROM measured_spans WHERE run_id=NEW.run_id AND seq<>NEW.seq "
            "AND mono_start<NEW.mono_end AND mono_end>NEW.mono_start) BEGIN SELECT RAISE(ABORT,'monotonic overlap'); END");
        execute(database, "CREATE TRIGGER measured_no_regression BEFORE UPDATE ON measured_spans "
            "WHEN NEW.duration_us<OLD.duration_us OR NEW.mono_start<>OLD.mono_start OR NEW.utc_start<>OLD.utc_start "
            "OR NEW.epoch<>OLD.epoch OR NEW.session<>OLD.session OR (OLD.active=0 AND (NEW.duration_us<>OLD.duration_us OR NEW.active<>0)) "
            "BEGIN SELECT RAISE(ABORT,'measured checkpoint regression'); END");
        execute(database, "PRAGMA user_version=2");
        tx.commit(); hasMeasured = true;
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

    StorageMode mode;
    bool hasMeasured{};
    Timestamp run{}, priorUncertainties{}, liveUncertainties{};
    const TemporalEngine* owner{};
    std::optional<MeasuredSpan> live;
    sqlite3* database{};
    std::filesystem::path databasePath;
    std::optional<std::int64_t> activeInterval;
};

SessionDatabase::SessionDatabase(const std::filesystem::path& path, StorageMode mode)
    : impl_(std::make_unique<Impl>(path, mode)) {}

SessionDatabase::~SessionDatabase() = default;
SessionDatabase::SessionDatabase(SessionDatabase&&) noexcept = default;
SessionDatabase& SessionDatabase::operator=(SessionDatabase&&) noexcept = default;

void SessionDatabase::setCounting(bool counting, Timestamp timestamp) {
    if (impl_->mode != StorageMode::Legacy || impl_->hasMeasured) throw std::logic_error("Legacy writes are disabled for measured/read-only storage");
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
    if (impl_->mode != StorageMode::Legacy || impl_->hasMeasured) throw std::logic_error("Legacy writes are disabled for measured/read-only storage");
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

void SessionDatabase::updateTemporal(TemporalEngine& engine, bool durable) {
    if (impl_->mode != StorageMode::Measured) throw std::logic_error("Measured writer required");
    if (impl_->owner && impl_->owner != &engine) throw std::logic_error("One engine per database writer");
    impl_->owner = &engine;
    impl_->liveUncertainties = engine.snapshot().uncertainties;
    if (durable || !engine.pending().empty() || !impl_->run) {
        Transaction tx(impl_->database);
        if (!impl_->run) {
            Statement q(impl_->database, "INSERT INTO temporal_runs(started_utc_us) VALUES(?1)");
            q.bind(engine.snapshot().utc, 1); q.step();
        }
        const Timestamp run = impl_->run ? impl_->run : sqlite3_last_insert_rowid(impl_->database);
        const auto save = [&](const MeasuredSpan& span) {
            Statement q(impl_->database, "INSERT INTO measured_spans VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
                "ON CONFLICT(run_id,seq) DO UPDATE SET utc_end=excluded.utc_end,mono_end=excluded.mono_end,"
                "duration_us=excluded.duration_us,active=excluded.active");
            const Timestamp values[]{run,span.sequence,span.epoch,span.session,span.utcStart,span.utcEnd,
                span.monoStart,span.monoEnd,span.duration,span.active ? 1 : 0};
            for (int i=0;i<10;++i) q.bind(values[i],i+1);
            q.step();
        };
        for (const auto& span : engine.pending()) save(span);
        if (engine.current()) save(*engine.current());
        Statement quality(impl_->database,"UPDATE temporal_runs SET uncertainty_count=?1 WHERE id=?2");
        quality.bind(engine.snapshot().uncertainties,1);quality.bind(run,2);quality.step();
        tx.commit(); impl_->run = run; engine.acknowledge();
    }
    impl_->live = engine.current();
}

Timestamp SessionDatabase::uncertaintyCount() const {
    return impl_->priorUncertainties + impl_->liveUncertainties;
}

std::chrono::microseconds contributionWithin(const SessionContribution& c, TimeRange range) {
    const auto span = c.interval.end-c.interval.start;
    if (span <= 0) return {};
    const auto from = std::clamp(range.start-c.interval.start, Timestamp{0}, span);
    const auto to = std::clamp(range.end-c.interval.start, Timestamp{0}, span);
    if (to <= from) return {};
    // V2 currently maps each epoch at unit rate. Cumulative apportionment also
    // conserves the total if a future source supplies a calibrated civil rate.
    const auto prefix = [&](Timestamp offset) -> Timestamp {
        if (offset == span) return c.duration.count();
        if (c.duration.count() == span) return offset;
        return static_cast<Timestamp>(static_cast<long double>(c.duration.count()) * offset / span);
    };
    return std::chrono::microseconds(prefix(to)-prefix(from));
}

HistoryReport SessionDatabase::queryMeasuredHistory(TimeRange range, CalendarPeriod period,
    const CalendarTimeConverter* converter) const {
    HistoryReport report{period,range};
    const auto append = [&](SessionContribution c) {
        const auto duration = contributionWithin(c,range);
        if (duration.count() <= 0) return;
        c.interval = {std::max(c.interval.start,range.start),std::min(c.interval.end,range.end)};
        c.duration = duration;
        report.sessionDuration += duration;
        report.contributions.push_back(c);
    };
    Statement legacy(impl_->database, "SELECT started_at_us,COALESCE(ended_at_us,checkpoint_at_us) "
        "FROM session_intervals WHERE started_at_us<?2 AND COALESCE(ended_at_us,checkpoint_at_us)>?1 ORDER BY started_at_us,id");
    legacy.bind(range.start,1); legacy.bind(range.end,2);
    while (legacy.step()==SQLITE_ROW) {
        const auto start=legacy.integer(0), end=legacy.integer(1);
        append({{start,end},std::chrono::microseconds(end-start),false,0});
    }
    if (impl_->hasMeasured) {
        Statement q(impl_->database, "SELECT run_id,seq,utc_start,utc_end,duration_us,active,epoch "
            "FROM measured_spans WHERE utc_end>?1 AND utc_start<?2 ORDER BY utc_start,run_id,seq");
        q.bind(range.start,1);q.bind(range.end,2);
        while(q.step()==SQLITE_ROW) {
            if(impl_->live && q.integer(0)==impl_->run && q.integer(1)==impl_->live->sequence) continue;
            append({{q.integer(2),q.integer(3)},std::chrono::microseconds(q.integer(4)),
                impl_->mode==StorageMode::Measured && q.integer(5)!=0,q.integer(6)});
        }
        if(impl_->live) {
            const auto& s=*impl_->live;
            append({{s.utcStart,s.utcEnd},std::chrono::microseconds(s.duration),true,s.epoch});
        }
    }
    std::stable_sort(report.contributions.begin(),report.contributions.end(),[](const auto& a,const auto& b){return a.interval.start<b.interval.start;});
    for(const auto& c:report.contributions) report.sessionIntervals.push_back(c.interval);
    for (Timestamp day=range.start; day<range.end;) {
        const auto bounds=converter ? calendarPeriodContaining(day,CalendarPeriod::Day,*converter) : calendarPeriodContaining(day,CalendarPeriod::Day);
        if(bounds.end<=day) throw std::runtime_error("Invalid calendar day");
        if(std::any_of(report.contributions.begin(),report.contributions.end(),[&](const auto& c){return contributionWithin(c,bounds).count()>0;})) ++report.daysWithDuration;
        day=bounds.end;
    }
    return report;
}

void SessionDatabase::backupTo(
    const std::filesystem::path& destination) const {
    impl_->backupTo(destination);
}

Timestamp SessionDatabase::latestRecordedTimestamp() const {
    if (impl_->hasMeasured || impl_->mode == StorageMode::ReadOnly) {
        Statement durable(impl_->database, impl_->hasMeasured
            ? "SELECT MAX(value) FROM (SELECT COALESCE(MAX(COALESCE(ended_at_us,checkpoint_at_us)),0) value FROM session_intervals UNION ALL SELECT COALESCE(MAX(utc_end),0) FROM measured_spans)"
            : "SELECT COALESCE(MAX(COALESCE(ended_at_us,checkpoint_at_us)),0) FROM session_intervals");
        durable.step(); return durable.integer(0);
    }
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
    if (impl_->hasMeasured || impl_->mode == StorageMode::ReadOnly)
        return queryMeasuredHistory(range,period,converter);
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
        report.contributions.push_back({{start,end},std::chrono::microseconds(end-start),
            impl_->activeInterval.has_value() && end == now,0});
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
