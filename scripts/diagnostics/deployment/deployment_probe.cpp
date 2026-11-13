// Console-only: no application mutex, registry, installed paths or Windows messages.
#include "session_database.h"
#include "dashboard_data.h"
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace chronox;
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
int main(int argc, char** argv) { try {
    require(argc==3,"usage: probe isolated-root now-us");
    const auto root=std::filesystem::canonical(argv[1]);
    require(std::filesystem::is_regular_file(root/"ISOLATED_VALIDATION"),"missing isolation marker");
    const auto path=root/"migration-copy.db";
    require(std::filesystem::is_regular_file(path) && !std::filesystem::is_symlink(path),"missing copy or symlink");
    const Timestamp now=std::stoll(argv[2]);
    sqlite3* sql{};
    auto encoded=path.u8string();
    require(sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()),&sql,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,"open copy");
    sqlite3_stmt* st{};
    require(sqlite3_prepare_v2(sql,"SELECT started_at_us,COALESCE(ended_at_us,checkpoint_at_us) FROM session_intervals",-1,&st,nullptr)==SQLITE_OK,"read legacy anchors");
    std::set<Timestamp> anchors{now,now+15'000'000};
    while(sqlite3_step(st)==SQLITE_ROW) {
        const auto start=sqlite3_column_int64(st,0), end=sqlite3_column_int64(st,1);
        anchors.insert(start); anchors.insert(end);
        // Visit every local day crossed, including variable-length DST days.
        for(auto day=calendarPeriodContaining(start,CalendarPeriod::Day); day.end<end;
            day=calendarPeriodContaining(day.end,CalendarPeriod::Day)) anchors.insert(day.end);
    }
    sqlite3_finalize(st);sqlite3_close(sql);
    std::ofstream reports(root/"reports.tsv");
    std::ofstream buckets(root/"buckets.tsv");
    reports<<"phase\tperiod\tstart\tend\tduration_us\tapi_duration_us\tdays\tactive\n";
    buckets<<"phase\tstart\tend\tseconds\n";
    auto report=[&](SessionDatabase& db,const char* phase,Timestamp civilNow) {
        std::set<std::pair<int,Timestamp>> seen;
        for(auto anchor:anchors) for(int p=0;p<4;++p) {
            auto period=static_cast<CalendarPeriod>(p);
            auto r=db.queryHistory(period,anchor,civilNow);
            if(seen.emplace(p,r.range.start).second) {
                auto dashboard=queryDashboardHistory(db,period,anchor,civilNow,true);
                require(dashboard.duration==std::chrono::duration_cast<std::chrono::seconds>(r.sessionDuration),"dashboard duration");
                require(dashboard.daysWithActivity==r.daysWithDuration,"dashboard active days");
                for(const auto& b:dashboard.buckets)
                    buckets<<phase<<'\t'<<b.range.start<<'\t'<<b.range.end<<'\t'<<b.duration.count()<<'\n';
                int active=0; for(const auto& c:r.contributions) active+=c.active?1:0;
                require(active<=1,"more than one active contribution");
                reports<<phase<<'\t'<<p<<'\t'<<r.range.start<<'\t'<<r.range.end<<'\t'
                       <<r.sessionDuration.count()<<'\t'<<db.duration(period,anchor).count()<<'\t'
                       <<r.daysWithDuration<<'\t'<<active<<'\n';
            }
        }
    };
    { SessionDatabase db(path,StorageMode::ReadOnly);report(db,"v1",now); }
    { SessionDatabase db(path,StorageMode::Measured); report(db,"migrated",now);db.backupTo(root/"migrated-backup.db"); }
    {
        SessionDatabase db(path,StorageMode::Measured);
        TemporalEngine e(false,{now,0,0}); db.updateTemporal(e,true);
        e.observe({now+10'000'000,10'000'000,10'000'000});db.updateTemporal(e,true);
        e.observe({now+15'000'000,15'000'000,15'000'000});db.updateTemporal(e,false);
        report(db,"live",now+15'000'000);
        db.backupTo(root/"active-backup.db");
        // Deliberately omit stop: durable recovery must discard the 5 s RAM tail.
    }
    {
        SessionDatabase db(path,StorageMode::Measured);report(db,"recovered",now);
        db.backupTo(root/"recovered-backup.db");
        TemporalEngine e(false,{now,100'000'000,100'000'000});db.updateTemporal(e,true);
        e.stop({now+7'000'000,107'000'000,107'000'000});db.updateTemporal(e,true);db.updateTemporal(e,true);
        report(db,"stopped",now+7'000'000);
    }
    {SessionDatabase db(path,StorageMode::Measured);report(db,"restarted",now);}
    for(auto name:{"migrated-backup.db","active-backup.db","recovered-backup.db"}) {
        SessionDatabase db(root/name,StorageMode::ReadOnly);report(db,name,now);
    }
    require(bool(reports)&&bool(buckets),"report write");
    std::cout<<"PASS: isolated storage startup, migration, checkpoints, recovery, backups and reports\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
