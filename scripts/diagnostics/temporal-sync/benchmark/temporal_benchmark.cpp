#include "session_database.h"
#include "session_tracker.h"
#include "utc_timestamp_clock.h"
#ifndef CHRONOX_TEMPORAL_BASELINE
#include "temporal_engine.h"
#include "temporal_diagnostics.h"
#include "windows_time_source.h"
#endif
#include <windows.h>
#include <psapi.h>
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>
using namespace chronox;
double cpu(){FILETIME a,b,k,u;GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);ULARGE_INTEGER x,y;x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return (x.QuadPart+y.QuadPart)/1e7;}
SIZE_T memory(){PROCESS_MEMORY_COUNTERS_EX p{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p));return p.PrivateUsage;}
int main(int argc,char** argv){try{
    const std::string profile=argc>1?argv[1]:"hot";const int seconds=argc>2?std::stoi(argv[2]):30;
    const bool logging=argc>3 && std::string(argv[3])=="logging";
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto dir=root/("chronox-benchmark-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(SessionTracker::Clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup{std::filesystem::path root,dir;~Cleanup(){if(dir.is_absolute()&&dir.parent_path()==root){std::error_code e;std::filesystem::remove_all(dir,e);}}} cleanup{root,dir};
#ifdef CHRONOX_TEMPORAL_BASELINE
    const char* flavor="baseline-d6db29d";
    SessionDatabase database(dir/"fixture.db");UtcTimestampClock clock;
    SessionTracker tracker(false,SessionTracker::Clock::now());
    database.setCounting(true,clock.at(SessionTracker::Clock::now()));
    const auto frame=[&](bool durable,bool reports){auto now=SessionTracker::Clock::now();const auto utc=clock.at(now);(void)tracker.elapsed(now);if(durable)database.checkpoint(utc);
#else
    const char* flavor=logging?"measured-logging":"measured";
    SessionDatabase database(dir/"fixture.db",StorageMode::Measured);
    TemporalEngine engine(false,sampleWindowsTime());database.updateTemporal(engine,true);
    TemporalDiagnostics diagnostics(logging?dir/"logs":std::filesystem::path{});
    const auto frame=[&](bool durable,bool reports){engine.observe(sampleWindowsTime());if(reports||durable)database.updateTemporal(engine,durable);diagnostics.record(engine.snapshot(),durable?"checkpoint":"sample");const auto utc=engine.snapshot().utc;
#endif
        if(reports)for(auto period:{CalendarPeriod::Day,CalendarPeriod::Week,CalendarPeriod::Month,CalendarPeriod::Year})(void)database.queryHistory(period,utc,utc);
    };
    if(profile=="history"){
        sqlite3* db{};auto encoded=(dir/"fixture.db").u8string();sqlite3_open(reinterpret_cast<const char*>(encoded.c_str()),&db);
        const auto rc=sqlite3_exec(db,"WITH RECURSIVE n(x) AS(VALUES(0) UNION ALL SELECT x+1 FROM n WHERE x<99999) INSERT INTO session_intervals(started_at_us,checkpoint_at_us,ended_at_us) SELECT 1200000000000000+x*3600000000,1200000060000000+x*3600000000,1200000060000000+x*3600000000 FROM n",nullptr,nullptr,nullptr);sqlite3_close(db);if(rc!=SQLITE_OK)throw std::runtime_error("history fixture failed");
    }
    for(int i=0;i<20;++i)frame(false,profile!="hot");
    std::vector<double> micros;micros.reserve(profile=="hot"?200000:1000);
    DWORD handles0{},handles1{};GetProcessHandleCount(GetCurrentProcess(),&handles0);
    SIZE_T maximum=memory();const double initialCpu=cpu();const auto start=SessionTracker::Clock::now();
    const int count=profile=="hot"?200000:profile=="history"?200:seconds;
    for(int i=0;i<count;++i){
        if(profile=="cadence")Sleep(1000);
        const auto a=SessionTracker::Clock::now();frame(profile=="cadence"&&i%60==59,profile!="hot");
        const auto b=SessionTracker::Clock::now();micros.push_back(std::chrono::duration<double,std::micro>(b-a).count());
        if(profile!="hot")maximum=std::max(maximum,memory());
    }
    const auto end=SessionTracker::Clock::now();const double used=cpu()-initialCpu;
    const double elapsed=std::chrono::duration<double>(end-start).count();
    GetProcessHandleCount(GetCurrentProcess(),&handles1);maximum=std::max(maximum,memory());
    std::sort(micros.begin(),micros.end());
    std::cout<<"{\"flavor\":\""<<flavor<<"\",\"profile\":\""<<profile<<"\",\"samples\":"<<count
        <<",\"wall_s\":"<<elapsed<<",\"cpu_s\":"<<used<<",\"cpu_one_core_percent\":"<<100*used/elapsed
        <<",\"private_max_bytes\":"<<maximum<<",\"handles_before\":"<<handles0<<",\"handles_after\":"<<handles1
        <<",\"median_us\":"<<micros[micros.size()/2]<<",\"p95_us\":"<<micros[static_cast<size_t>(0.95*(micros.size()-1))]<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
