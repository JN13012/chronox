// End-to-end diagnostic: production engine + temporary SQLite, never run the app.
// Failing CTest cases represent failed product requirements, not build failures.
#include "session_database.h"
#include "session_tracker.h"
#include "utc_timestamp_clock.h"
#include "temporal_engine.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;
using chronox::Timestamp;
using chronox::SessionEvent;
using chronox::SessionTracker;
using chronox::CalendarPeriod;
using Clock = chronox::UtcTimestampClock;
constexpr Timestamp second = 1'000'000;
constexpr Timestamp base = 1'794'225'600 * second; // 2026-11-09 12:00:00 UTC

std::tm utcLocal(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &value);
#else
    gmtime_r(&value, &result);
#endif
    return result;
}
std::time_t utcTimestamp(std::tm value) {
#ifdef _WIN32
    return _mkgmtime(&value);
#else
    return timegm(&value);
#endif
}
const chronox::CalendarTimeConverter utcConverter{utcLocal, utcTimestamp};

struct Temp {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path());
    std::filesystem::path directory = root /
        ("chronox-temporal-diagnostic-" + std::to_string(Clock::SteadyClock::now().time_since_epoch().count()));
    Temp() {
        if (!directory.is_absolute() || directory.parent_path() != root ||
            !std::filesystem::create_directory(directory)) {
            throw std::runtime_error("Could not create an isolated temporary database directory");
        }
    }
    ~Temp() {
        if (directory.is_absolute() && directory.parent_path() == root) {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
        }
    }
    std::filesystem::path file() const { return directory / "temporary.db"; }
};

struct Checks {
    int failed{};
    void equal(const char* name, Timestamp actual, Timestamp expected) {
        const bool pass = actual == expected;
        std::cout << (pass ? "PASS " : "FAIL ") << name << " actual_us=" << actual
                  << " expected_us=" << expected << '\n';
        if (!pass) ++failed;
    }
};

struct Simulation {
    Temp temp;
    Timestamp wall, steady{}, awake{};
    bool suspended{};
    chronox::TemporalEngine engine;
    chronox::SessionDatabase database{temp.file(),chronox::StorageMode::Measured};
    chronox::TimeSample sample() const {return {wall,steady,awake};}
    explicit Simulation(Timestamp start=base, Timestamp offset=0)
        : wall(start), engine(false,{start+offset,0,0}) {
        // A stale startup projection is corrected from the authoritative source
        // before any active duration is recorded. Historical rows are immutable.
        engine.observe(sample()); database.updateTemporal(engine,true);
    }
    void advance(std::chrono::seconds duration, bool qpcAdvances=true) {
        const auto d=std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        wall+=d;if(qpcAdvances) steady+=d;if(!suspended) awake+=d;
    }
    void correction(std::chrono::seconds duration){wall+=std::chrono::duration_cast<std::chrono::microseconds>(duration).count();}
    Timestamp now(){engine.observe(sample());database.updateTemporal(engine,false);return engine.snapshot().utc;}
    void tick(){engine.observe(sample());database.updateTemporal(engine,true);}
    void event(SessionEvent event){
        engine.event(event,sample());
        if(event==SessionEvent::Suspended) suspended=true;
        if(event==SessionEvent::Resumed) suspended=false;
        database.updateTemporal(engine,true);
    }
    Timestamp total(){const auto t=now();return database.queryHistory(CalendarPeriod::Month,base,t,utcConverter).sessionDuration.count();}
    Timestamp elapsed(){now();return engine.snapshot().elapsed;}
};

int run(const std::string& name) {
    Checks c;
    if (name == "baseline" || name == "forward" || name == "backward") {
        Simulation s;
        s.advance(60s); s.tick();
        if (name == "forward") s.correction(120s);
        if (name == "backward") s.correction(-120s);
        s.advance(60s); s.tick();
        c.equal("UTC follows corrected wall clock", s.now(), s.wall);
        s.event(SessionEvent::Locked);
        c.equal("SQLite counts only real active time", s.total(), 120 * second);
        c.equal("tracker counts only real active time", s.elapsed(), 120 * second);
    } else if (name == "midnight" || name == "midnight-offset") {
        const Timestamp midnight = base + 12 * 3600 * second;
        Simulation s(midnight - 10 * second, name == "midnight-offset" ? 86 * second : 0);
        s.advance(20s); s.event(SessionEvent::Locked);
        c.equal("day before midnight", s.database.queryHistory(CalendarPeriod::Day, midnight - 1, s.now(), utcConverter).sessionDuration.count(), 10 * second);
        c.equal("day after midnight", s.database.queryHistory(CalendarPeriod::Day, midnight, s.now(), utcConverter).sessionDuration.count(), 10 * second);
        c.equal("continuous session spans midnight", s.elapsed(), 20 * second);
        c.equal("two days sum without duplicates", s.total(), 20 * second);
    } else if (name == "sleep-qpc-advances" || name == "sleep-qpc-pauses" || name == "resume-forward" || name == "resume-backward") {
        Simulation s;
        s.advance(10s); s.event(SessionEvent::Suspended);
        s.advance(name == "resume-backward" ? 30s : 3600s, name != "sleep-qpc-pauses");
        if (name == "resume-forward") s.correction(120s);
        if (name == "resume-backward") s.correction(-120s);
        s.event(SessionEvent::Resumed);
        c.equal("UTC at wake follows wall", s.now(), s.wall);
        s.advance(20s); s.event(SessionEvent::Locked);
        c.equal("SQLite excludes sleep and correction", s.total(), 30 * second);
        c.equal("tracker excludes sleep and correction", s.elapsed(), 30 * second);
    } else if (name == "restart" || name == "restart-future") {
        Temp temp;
        const Timestamp origin = name == "restart-future" ? base + 120 * second : base;
        {
            chronox::SessionDatabase legacy(temp.file());
            legacy.setCounting(true,origin);
            legacy.checkpoint(origin+45*second);
        }
        chronox::SessionDatabase db(temp.file(),chronox::StorageMode::Measured);
        const Timestamp wall = base + (name == "restart-future" ? 60 : 3600) * second;
        chronox::TemporalEngine engine(false,{wall,10000*second,10000*second});
        c.equal("restart UTC follows system despite historical anchor", engine.snapshot().utc, wall);
        c.equal("interrupted V1 durable bound remains unchanged", db.latestRecordedTimestamp(), origin + 45 * second);
        db.updateTemporal(engine,true);
        engine.stop({wall+20*second,10020*second,10020*second});db.updateTemporal(engine,true);
        const auto report=db.queryHistory(CalendarPeriod::Day,base,engine.snapshot().utc,utcConverter);
        c.equal("crash gap excluded and no double counting",report.sessionDuration.count(),65*second);
        // Civil overlap across different epochs/runs is allowed. The database
        // enforces disjoint monotone spans; no global UTC ordering is asserted.
        c.equal("both recorded contributions retained",report.contributions.size(),2);
    } else if (name == "duplicates") {
        Simulation s;
        s.advance(10s); s.event(SessionEvent::Locked); s.event(SessionEvent::Locked);
        s.advance(10s); s.event(SessionEvent::Suspended); s.event(SessionEvent::Suspended);
        s.advance(1h); s.event(SessionEvent::Resumed); s.event(SessionEvent::Resumed);
        s.advance(10s); s.event(SessionEvent::Unlocked); s.event(SessionEvent::Unlocked);
        s.advance(20s); s.event(SessionEvent::Locked); s.event(SessionEvent::Locked);
        c.equal("duplicate events add no SQLite duration", s.total(), 30 * second);
        c.equal("duplicate events add no tracker duration", s.elapsed(), 30 * second);
    } else { throw std::runtime_error("Unknown scenario"); }
    return c.failed == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Provide one named scenario");
        return run(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 2;
    }
}
