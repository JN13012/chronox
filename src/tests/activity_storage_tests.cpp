#include "session_database.h"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace chronox;
void check(bool v) { if(!v) throw std::runtime_error("Activity storage invariant"); }
struct Sql {
    sqlite3* db{};
    Sql(const std::filesystem::path& p) { auto u=p.u8string(); check(sqlite3_open(reinterpret_cast<const char*>(u.c_str()),&db)==SQLITE_OK); }
    ~Sql() { sqlite3_close(db); }
    bool exec(const char* s) { return sqlite3_exec(db,s,nullptr,nullptr,nullptr)==SQLITE_OK; }
    std::string scalar(const char* s) { sqlite3_stmt* q{}; check(sqlite3_prepare_v2(db,s,-1,&q,nullptr)==SQLITE_OK); check(sqlite3_step(q)==SQLITE_ROW); auto t=sqlite3_column_text(q,0); std::string r=t?reinterpret_cast<const char*>(t):"NULL"; sqlite3_finalize(q); return r; }
};
int main() {
    const auto root=std::filesystem::temp_directory_path()/("chronox-activity-"+std::to_string(SessionTracker::Clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(root)); const auto p=root/"copy.db", backup=root/"backup.db";
    constexpr Timestamp B=1'794'225'600'000'000, S=1'000'000;
    { SessionDatabase db(p); db.setCounting(true,B-10*S); db.setCounting(false,B-5*S); }
    std::string old;
    const char* rows="SELECT group_concat(id||':'||started_at_us||':'||checkpoint_at_us||':'||ended_at_us) FROM session_intervals";
    { Sql q(p); old=q.scalar(rows); }
    { SessionDatabase db(p,StorageMode::ReadOnly); check(!db.queryActivity(CalendarPeriod::Year,B).measured); }
    { SessionDatabase db(p,StorageMode::Measured); TemporalEngine e(false,{B-2*S,0,0}); db.updateTemporal(e,true); e.stop({B-S,S,S}); db.updateTemporal(e,true); }
    { Sql q(p); check(q.scalar("PRAGMA user_version")=="2"); check(q.exec("CREATE TABLE activity_spans(dummy)")); }
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
      check(!q.exec("INSERT INTO activity_spans SELECT run_id,seq+99,epoch,session,utc_start,utc_end,mono_start,mono_end,duration_us,0 FROM activity_spans LIMIT 1"));
    }
    std::filesystem::remove_all(root);
    std::cout << "V1/V2/V3 migration, rollback, checkpoints, recovery, backup and aggregates: PASS\n";
}
