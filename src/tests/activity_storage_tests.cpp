#include "session_database.h"
#include "../windows/dashboard_data.h"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <source_location>
using namespace chronox;
void check(bool v, const std::source_location location=std::source_location::current()) { if(!v) { std::cerr<<"FAIL "<<location.function_name()<<":"<<location.line()<<std::endl; throw std::runtime_error("Activity storage invariant"); } }
struct Sql {
    sqlite3* db{};
    Sql(const std::filesystem::path& p) { auto u=p.u8string(); check(sqlite3_open(reinterpret_cast<const char*>(u.c_str()),&db)==SQLITE_OK); }
    ~Sql() { sqlite3_close(db); }
    bool exec(const char* s) { return sqlite3_exec(db,s,nullptr,nullptr,nullptr)==SQLITE_OK; }
    std::string scalar(const char* s) { sqlite3_stmt* q{}; check(sqlite3_prepare_v2(db,s,-1,&q,nullptr)==SQLITE_OK); check(sqlite3_step(q)==SQLITE_ROW); auto t=sqlite3_column_text(q,0); std::string r=t?reinterpret_cast<const char*>(t):"NULL"; sqlite3_finalize(q); return r; }
};

Timestamp transition{}; int afterOffset{};
std::time_t naive(std::tm t) {
    using namespace std::chrono;
    const auto month=year{t.tm_year+1900}/January+months{t.tm_mon};
    return duration_cast<seconds>((sys_days{month/day{1}}+days{t.tm_mday-1}+hours{t.tm_hour}+minutes{t.tm_min}+seconds{t.tm_sec}).time_since_epoch()).count();
}
std::tm injectedLocal(std::time_t t) { t+=(t>=transition && t<transition+120*86400)?afterOffset:0; return *std::gmtime(&t); }
std::time_t injectedTimestamp(std::tm t) { auto n=naive(t); return n-(n>=transition+afterOffset && n<transition+120*86400?afterOffset:0); }
void calendarTests(const std::filesystem::path& root) {
    constexpr Timestamp S=1'000'000;
    using namespace std::chrono;
    const auto midnight=duration_cast<seconds>(sys_days{year{2024}/March/10}.time_since_epoch()).count();
    transition=midnight+7200;
    const CalendarTimeConverter converter{injectedLocal,injectedTimestamp};
    for(int offset:{3600,-3600}) {
        afterOffset=offset;
        const auto range=calendarPeriodContaining((midnight+43200)*S,CalendarPeriod::Day,converter);
        const Timestamp expected=(86400-offset)*S;
        check(range.end-range.start==expected);
        SessionDatabase db(root/(offset>0?"spring.db":"fall.db"),StorageMode::Activity);
        TemporalEngine e(false,{range.start,0,0}); ActivityTracker a;
        a.advance(e.snapshot(),e.snapshot(),range.start,{0,0,true}); db.updateTemporal(e,true,&a);
        // Actual periodic input, independent of the civil DST transition.
        for(Timestamp time=60*S;time<=expected;time+=60*S) {
            auto b=e.snapshot(); auto civil=e.current()->utcEnd; e.observe({range.start+time,time,time});
            a.advance(b,e.snapshot(),civil,{static_cast<std::uint64_t>(time/1000),static_cast<std::uint32_t>(time/1000),true});
        }
        a.stop(); e.stop(e.snapshot().sample); db.updateTemporal(e,true,&a);
        for(auto period:{CalendarPeriod::Day,CalendarPeriod::Week,CalendarPeriod::Month,CalendarPeriod::Year}) {
            const auto activity=db.queryActivity(period,range.start,&converter);
            check(activity.measured && activity.duration.count()==expected);
            check(activity.duration==db.queryHistory(period,range.start,range.end,converter).sessionDuration);
        }
    }
    afterOffset=0;
    const auto boundary=duration_cast<microseconds>(sys_days{year{2024}/January/1}.time_since_epoch()).count();
    SessionDatabase db(root/"midnight.db",StorageMode::Activity);
    TemporalEngine e(false,{boundary-1'000'001,0,0}); ActivityTracker a;
    a.advance(e.snapshot(),e.snapshot(),e.snapshot().utc,{0,0,true}); db.updateTemporal(e,true,&a);
    const Timestamp d=3'000'004; const auto b=e.snapshot();
    e.observe({boundary+2'000'003,d,d}); a.advance(b,e.snapshot(),b.utc,{3000,0,true}); db.updateTemporal(e,true,&a);
    for(auto period:{CalendarPeriod::Day,CalendarPeriod::Week,CalendarPeriod::Month,CalendarPeriod::Year}) {
        check(db.queryActivity(period,boundary-1,&converter).duration.count()==1'000'001);
        check(db.queryActivity(period,boundary,&converter).duration.count()==2'000'003);
    }
    const auto filteredActivity=db.queryActivity(TimeRange{boundary,boundary+1'000'000});
    check(filteredActivity.measured && filteredActivity.duration.count()==1'000'000);
    check(!db.queryActivity(TimeRange{boundary-20*S,boundary-10*S}).measured);
    check(!db.queryActivity(TimeRange{boundary+2'000'003,boundary+3'000'003}).measured); // touching coverage is not an overlap
    // A UTC correction overlaps civil timestamps but never monotone durations.
    auto before=e.snapshot(); auto civil=e.current()->utcEnd;
    e.observe({boundary-60*S,d+S,d+S}); a.advance(before,e.snapshot(),civil,{4000,0,true}); db.updateTemporal(e,true,&a);
    before=e.snapshot(); civil=e.current()->utcEnd;
    e.observe({boundary-59*S,d+2*S,d+2*S}); a.advance(before,e.snapshot(),civil,{5000,0,true}); db.updateTemporal(e,true,&a);
    Sql q(root/"midnight.db");
    check(q.scalar("SELECT SUM(duration_us) FROM activity_spans")==std::to_string(d+2*S));
    check(q.scalar("SELECT count(*) FROM activity_spans a JOIN activity_spans b ON a.run_id=b.run_id AND a.seq<b.seq AND a.mono_start<b.mono_end AND b.mono_start<a.mono_end")=="0");
}
void retryTests(const std::filesystem::path& root) {
    constexpr Timestamp S=1'000'000, B=1'794'225'600'000'000;
    const auto path=root/"retry.db";
    SessionDatabase db(path,StorageMode::Activity); TemporalEngine e(false,{B,0,0}); ActivityTracker a;
    a.advance(e.snapshot(),e.snapshot(),B,{0,0,true}); db.updateTemporal(e,true,&a);
    auto b=e.snapshot(); e.observe({B+60*S,60*S,60*S}); a.advance(b,e.snapshot(),B,{60000,0,true}); db.updateTemporal(e,true,&a);
    b=e.snapshot(); e.observe({B+120*S,120*S,120*S}); a.advance(b,e.snapshot(),B+60*S,{120000,0,true});
    { Sql q(path); check(q.exec("CREATE TRIGGER failure BEFORE UPDATE ON activity_spans BEGIN SELECT RAISE(ABORT,'injected'); END")); }
    bool rejected=false; try { db.updateTemporal(e,true,&a); } catch(const std::exception&) { rejected=true; } check(rejected);
    { Sql q(path); check(q.scalar("SELECT SUM(duration_us) FROM measured_spans")=="60000000"); check(q.scalar("SELECT SUM(duration_us) FROM activity_spans")=="60000000"); check(q.exec("DROP TRIGGER failure")); }
    db.updateTemporal(e,true,&a); db.updateTemporal(e,true,&a);
    check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==120*S);
    a.configure({false,5}); a.advance(e.snapshot(),e.snapshot(),B+120*S,{120000,0,true}); db.updateTemporal(e,true,&a);
    b=e.snapshot(); e.observe({B+1000*S,1000*S,1000*S}); a.advance(b,e.snapshot(),B+120*S,{1000000,0,true}); db.updateTemporal(e,true,&a);
    check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==1000*S);
    a.configure({true,1}); a.advance(e.snapshot(),e.snapshot(),B+1000*S,{1000000,0,true}); db.updateTemporal(e,true,&a);
    b=e.snapshot(); e.observe({B+1010*S,1010*S,1010*S}); a.advance(b,e.snapshot(),B+1000*S,{1010000,0,false}); db.updateTemporal(e,true,&a);
    check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==1000*S);
    check(db.activityUncertaintyCount()==1);
    { Sql q(path); check(q.scalar("SELECT SUM(duration_us) FROM activity_spans WHERE detection_enabled=0")=="880000000"); check(q.scalar("SELECT SUM(duration_us) FROM activity_spans WHERE detection_enabled=1")=="120000000");
      check(!q.exec("UPDATE activity_spans SET detection_enabled=1 WHERE detection_enabled=0"));
      check(!q.exec("INSERT INTO activity_spans SELECT run_id,9999,epoch,session,utc_end+20000000,utc_end+20000001,mono_end+20000000,mono_end+20000001,1,0,1,5 FROM activity_spans ORDER BY mono_end DESC LIMIT 1"));
    }
}
int main() try {
    constexpr Timestamp B=1'794'225'600'000'000, S=1'000'000;
    const auto root=std::filesystem::temp_directory_path()/("chronox-activity-"+std::to_string(SessionTracker::Clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(root)); const auto p=root/"copy.db", backup=root/"backup.db";
    { SessionDatabase db(p); db.setCounting(true,B-10*S); db.setCounting(false,B-5*S); }
    calendarTests(root); retryTests(root);
    {
        SessionDatabase db(root/"locked-only.db",StorageMode::Activity);
        TemporalEngine e(true,{B,0,0}); ActivityTracker a;
        a.advance(e.snapshot(),e.snapshot(),B,{0,0,true}); db.updateTemporal(e,true,&a);
        const auto before=e.snapshot(); e.observe({B+10*S,10*S,10*S});
        a.advance(before,e.snapshot(),B,{10000,0,true}); db.updateTemporal(e,true,&a);
        const auto report=db.queryActivity(CalendarPeriod::Day,B);
        check(report.measured && report.duration.count()==0);
    }
    { SessionDatabase reader(root/"retry.db",StorageMode::ReadOnly); check(reader.activityUncertaintyCount()==1); }
    std::string old, oldMeasured;
    const char* rows="SELECT group_concat(id||':'||started_at_us||':'||checkpoint_at_us||':'||ended_at_us) FROM session_intervals";
    { Sql q(p); old=q.scalar(rows); }
    { SessionDatabase db(p,StorageMode::ReadOnly); check(!db.queryActivity(CalendarPeriod::Year,B).measured); }
    { SessionDatabase db(p,StorageMode::Measured); TemporalEngine e(false,{B-2*S,0,0}); db.updateTemporal(e,true); e.stop({B-S,S,S}); db.updateTemporal(e,true); }
    { Sql q(p); oldMeasured=q.scalar("SELECT group_concat(run_id||':'||seq||':'||epoch||':'||session||':'||utc_start||':'||utc_end||':'||mono_start||':'||mono_end||':'||duration_us||':'||active) FROM measured_spans"); check(q.scalar("PRAGMA user_version")=="2"); check(q.exec("CREATE TABLE activity_spans(dummy)")); }
    bool rejected=false; try { SessionDatabase fail(p,StorageMode::Activity); } catch(const std::exception&) { rejected=true; } check(rejected);
    { Sql q(p); check(q.scalar("PRAGMA user_version")=="2"); check(q.scalar("SELECT count(*) FROM sqlite_master WHERE name='activity_tracking_runs'")=="0"); check(q.exec("DROP TABLE activity_spans")); }
    {
        SessionDatabase db(p,StorageMode::Activity); TemporalEngine e(false,{B,0,0}); ActivityTracker a;
        a.advance(e.snapshot(),e.snapshot(),B,{0,0,true}); db.updateTemporal(e,true,&a);
        auto step=[&](Timestamp t,bool durable) { auto b=e.snapshot(); auto c=e.current()?e.current()->utcEnd:b.utc; e.observe({B+t,t,t}); a.advance(b,e.snapshot(),c,{static_cast<std::uint64_t>(t/1000),0,true}); db.updateTemporal(e,durable,&a); };
        step(60*S,true); step(120*S,false);
        check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==120*S);
        step(1800*S,true);
        for(auto period:{CalendarPeriod::Day,CalendarPeriod::Week,CalendarPeriod::Month,CalendarPeriod::Year}) {
            auto r=db.queryActivity(period,B); check(r.measured && r.duration.count()==300*S);
            Timestamp sum{}; for(auto c:r.contributions) sum+=c.duration.count(); check(sum==r.duration.count());
        }
        const auto dashboard=queryDashboardSnapshot(db,B,false);
        for(std::size_t i=0;i<4;++i) check(dashboard.activityMeasured[i] && dashboard.activityDurations[i].count()==300);
        const auto history=queryDashboardHistory(db,CalendarPeriod::Day,B,B,false);
        check(history.activityMeasured && history.activityDuration.count()==300);
        check(!db.queryActivity(CalendarPeriod::Year,B-400LL*86400*S).measured);
        db.backupTo(backup);
    }
    { SessionDatabase db(backup,StorageMode::ReadOnly); check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==300*S); }
    {
        SessionDatabase db(p,StorageMode::Activity);
        check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==300*S);
        TemporalEngine e(false,{B+2000*S,0,0}); ActivityTracker a;
        a.advance(e.snapshot(),e.snapshot(),B+2000*S,{2000000,0,true}); db.updateTemporal(e,true,&a);
        auto b=e.snapshot(); e.observe({B+2010*S,10*S,10*S}); a.advance(b,e.snapshot(),b.utc,{2010000,0,true}); db.updateTemporal(e,true,&a);
        check(db.queryActivity(CalendarPeriod::Year,B).duration.count()==300*S); // measured zero after restart
    }
    { Sql q(p); check(q.scalar(rows)==old); check(q.scalar("PRAGMA user_version")=="3"); check(q.scalar("PRAGMA quick_check")=="ok");
      check(!q.exec("UPDATE activity_spans SET duration_us=-1"));
      check(!q.exec("INSERT INTO activity_spans SELECT run_id,seq+99,epoch,session,utc_start,utc_end,mono_start,mono_end,duration_us,0,detection_enabled,threshold_minutes FROM activity_spans LIMIT 1"));
    }
    { Sql q(p); check(q.scalar("SELECT group_concat(run_id||':'||seq||':'||epoch||':'||session||':'||utc_start||':'||utc_end||':'||mono_start||':'||mono_end||':'||duration_us||':'||active) FROM measured_spans WHERE run_id=1")==oldMeasured); }
    std::filesystem::remove_all(root);
    std::cout << "V1/V2/V3 migration, rollback, checkpoints, recovery, backup and aggregates: PASS\n";
}

catch(const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
