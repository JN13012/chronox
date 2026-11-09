#include "session_database.h"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace chronox;
constexpr Timestamp base = 1'794'225'600'000'000;
void check(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
struct Temp {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path());
    std::filesystem::path dir=root/("chronox-v2-test-"+std::to_string(SessionTracker::Clock::now().time_since_epoch().count()));
    Temp(){check(std::filesystem::create_directory(dir),"temp directory");}
    ~Temp(){if(dir.is_absolute() && dir.parent_path()==root){std::error_code e;std::filesystem::remove_all(dir,e);}}
    auto file(const char* name) const {return dir/name;}
};
struct Sql {
    sqlite3* db{};
    Sql(const std::filesystem::path& path){auto s=path.u8string();check(sqlite3_open(reinterpret_cast<const char*>(s.c_str()),&db)==SQLITE_OK,"fixture open");}
    ~Sql(){sqlite3_close(db);}
    std::string scalar(const char* sql){sqlite3_stmt* st{};check(sqlite3_prepare_v2(db,sql,-1,&st,nullptr)==SQLITE_OK,"prepare");check(sqlite3_step(st)==SQLITE_ROW,"row");auto v=sqlite3_column_text(st,0);std::string result=v?reinterpret_cast<const char*>(v):"NULL";sqlite3_finalize(st);return result;}
    bool execute(const char* sql){return sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK;}
};
const char* rowsSql="SELECT group_concat(id||':'||started_at_us||':'||checkpoint_at_us||':'||COALESCE(ended_at_us,'NULL'),'|') FROM session_intervals";
int main(){try {
    Temp t;
    const auto original=t.file("original-v1.db"), copy=t.file("copy.db"), backup=t.file("backup.db");
    {
        SessionDatabase v1(original);
        v1.setCounting(true,base);v1.setCounting(false,base+10'000'000);
        v1.setCounting(true,base+120'000'000);v1.checkpoint(base+165'000'000);
        v1.backupTo(copy);
    }
    std::string before;
    {Sql q(original);before=q.scalar(rowsSql);}
    {SessionDatabase reader(original,StorageMode::ReadOnly);check(reader.queryHistory(CalendarPeriod::Day,base,base).sessionDuration.count()==55'000'000,"read-only V1 durable history");}
    {
        SessionDatabase v2(copy,StorageMode::Measured);
        TemporalEngine engine(false,{base+60'000'000,0,0});v2.updateTemporal(engine,true);
        engine.observe({base+80'000'000,20'000'000,20'000'000});v2.updateTemporal(engine,false);
        check(v2.queryHistory(CalendarPeriod::Day,base,engine.snapshot().utc).sessionDuration.count()==75'000'000,"live state replaces durable span exactly once, future legacy retained");
        v2.updateTemporal(engine,true);v2.updateTemporal(engine,true);
        v2.backupTo(backup);
        engine.observe({base+85'000'000,25'000'000,25'000'000});v2.updateTemporal(engine,false);
    }
    {Sql q(copy);check(q.scalar(rowsSql)==before,"V1 rows unchanged by migration/writer");check(q.scalar("PRAGMA user_version")=="2","schema v2");}
    {
        SessionDatabase recovered(copy,StorageMode::Measured);
        check(recovered.queryHistory(CalendarPeriod::Day,base,base).sessionDuration.count()==75'000'000,"recovery only counts durable checkpoint, not live tail");
        TemporalEngine engine(false,{base+30'000'000,100'000'000,100'000'000});
        recovered.updateTemporal(engine,true);
        engine.stop({base+40'000'000,110'000'000,110'000'000});recovered.updateTemporal(engine,true);
        check(recovered.queryHistory(CalendarPeriod::Day,base,base).sessionDuration.count()==85'000'000,"new civil overlap adds only new measured duration");
    }
    {SessionDatabase reader(backup,StorageMode::ReadOnly);check(reader.queryHistory(CalendarPeriod::Day,base,base).sessionDuration.count()==75'000'000,"V2 online backup is durable snapshot");}
    {SessionDatabase reader(copy);check(reader.queryHistory(CalendarPeriod::Day,base,base).sessionDuration.count()==85'000'000,"default reader understands V2");bool denied=false;try{reader.setCounting(true,base);}catch(const std::logic_error&){denied=true;}check(denied,"legacy writes to V2 denied");}
    {Sql q(copy);check(q.scalar(rowsSql)==before,"V1 rows unchanged after V2 recovery");
        check(!q.execute("UPDATE measured_spans SET duration_us=duration_us-1"),"negative/regressing durations rejected");
        check(!q.execute("INSERT INTO measured_spans SELECT run_id,999,epoch,session,utc_start,utc_end,mono_start,mono_end,duration_us,0 FROM measured_spans WHERE duration_us>0 LIMIT 1"),"overlap rejected by database");
        check(q.scalar("PRAGMA quick_check")=="ok","integrity");}
    {Sql q(original);check(q.scalar(rowsSql)==before,"original fixture untouched");check(q.scalar("PRAGMA user_version")=="1","original V1 not migrated");}
    const auto failed=t.file("failed-migration.db");std::filesystem::copy_file(original,failed);
    {Sql q(failed);check(q.execute("CREATE TABLE measured_spans(conflict)"),"conflict fixture");}
    bool rejected=false;try{SessionDatabase bad(failed,StorageMode::Measured);}catch(const std::exception&){rejected=true;}
    check(rejected,"migration failure rejected");
    {Sql q(failed);check(q.scalar("PRAGMA user_version")=="1","migration rolls back version");check(q.scalar("SELECT COUNT(*) FROM sqlite_master WHERE name='temporal_runs'")=="0","migration rolls back new tables");check(q.scalar(rowsSql)==before,"failed migration preserves rows");}
    const auto uncertainFile=t.file("uncertain.db");
    {
        SessionDatabase db(uncertainFile,StorageMode::Measured);
        TemporalEngine e(false,{base,0,0});db.updateTemporal(e,true);
        e.observe({base+3600'000'000,3600'000'000,0});db.updateTemporal(e,true);
        check(e.snapshot().uncertain && db.uncertaintyCount()==1,"missing power evidence is flagged and durable");
        e.observe({base+3601'000'000,3601'000'000,1'000'000});db.updateTemporal(e,true);
        check(e.snapshot().elapsed==0 && e.snapshot().uncertain,"no silent resume after uncertain power state");
    }
    {SessionDatabase db(uncertainFile,StorageMode::ReadOnly);check(db.uncertaintyCount()==1,"uncertainty survives restart");}
    {
        const auto failedWrite=t.file("failed-checkpoint.db");
        SessionDatabase db(failedWrite,StorageMode::Measured);
        TemporalEngine e(false,{base,0,0});db.updateTemporal(e,true);
        e.stop({base+10'000'000,10'000'000,10'000'000});
        {Sql q(failedWrite);check(q.execute("CREATE TRIGGER injected_failure BEFORE UPDATE ON measured_spans BEGIN SELECT RAISE(ABORT,'fixture failure'); END"),"failure fixture");}
        bool failed=false;try{db.updateTemporal(e,true);}catch(const std::runtime_error&){failed=true;}
        check(failed&&!e.pending().empty(),"failed durable write preserves pending accounting");
        {Sql q(failedWrite);check(q.scalar("SELECT SUM(duration_us) FROM measured_spans")=="0","failed checkpoint rolls back");check(q.execute("DROP TRIGGER injected_failure"),"remove fixture failure");}
        e.stop({base+10'000'000,10'000'000,10'000'000});
        db.updateTemporal(e,true);db.updateTemporal(e,true);
        {Sql q(failedWrite);check(q.scalar("SELECT SUM(duration_us) FROM measured_spans")=="10000000","retry persists exactly once");}
    }
    {
        const auto randomFile=t.file("random.db");
        SessionDatabase db(randomFile,StorageMode::Measured);
        TimeSample sample{base,0,0};TemporalEngine e(false,sample);db.updateTemporal(e,true);
        bool locked=false,asleep=false;Timestamp expected=0;unsigned random=12345;
        for(int i=0;i<10000;++i){
            random=random*1664525u+1013904223u;
            const Timestamp delta=1000+(random%100000);
            sample.utc+=delta;sample.steady+=delta;if(!asleep)sample.awake+=delta;
            if(!locked&&!asleep)expected+=delta;
            if(random%7==0)sample.utc+=(random&1?1:-1)*120'000'000LL;
            const auto event=static_cast<SessionEvent>((random>>8)%4);
            e.event(event,sample);
            switch(event){case SessionEvent::Locked:locked=true;break;case SessionEvent::Unlocked:locked=false;break;
                case SessionEvent::Suspended:asleep=true;break;case SessionEvent::Resumed:asleep=false;break;}
            check(e.snapshot().elapsed==expected,"random transitions preserve physical awake/unlocked duration");
            if(i%100==0)db.updateTemporal(e,true);
        }
        e.stop(sample);db.updateTemporal(e,true);
        Sql q(randomFile);
        check(q.scalar("SELECT COALESCE(SUM(duration_us),0) FROM measured_spans")==std::to_string(expected),"every measured microsecond persisted exactly once");
        check(q.scalar("SELECT COUNT(*) FROM measured_spans a JOIN measured_spans b ON a.run_id=b.run_id AND a.seq<b.seq AND a.mono_start<b.mono_end AND a.mono_end>b.mono_start")=="0","no monotonic overlaps after random corrections");
    }
    std::cout<<"V1/V2 migration, recovery, backup and invariants passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
