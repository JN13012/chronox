// Standalone production-core probe. Never opens installation data/preferences.
#include "../windows/windows_time_source.h"
#include "../windows/windows_input_source.h"
#include "session_database.h"
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <string_view>

using namespace chronox;
struct Resources {
    std::uint64_t cpu{}, privateBytes{}, workingSet{};
    DWORD handles{};
};
Resources resources() {
    FILETIME creation{},exit{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user);
    const auto number=[](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    DWORD handles{}; GetProcessHandleCount(GetCurrentProcess(),&handles);
    return {number(kernel)+number(user),memory.PrivateUsage,memory.WorkingSetSize,handles};
}
int main(int argc,char** argv) {
    if(argc!=2 || (std::string_view(argv[1])!="v2" && std::string_view(argv[1])!="v3")) return 2;
    const bool enabled=std::string_view(argv[1])=="v3";
    const auto dir=std::filesystem::temp_directory_path()/("chronox-activity-probe-"+std::to_string(GetCurrentProcessId()));
    if(!std::filesystem::create_directory(dir)) return 3;
    {
        SessionDatabase db(dir/"probe.db",enabled?StorageMode::Activity:StorageMode::Measured);
        TemporalEngine engine(false,sampleWindowsTime()); ActivityTracker activity;
        auto* a=enabled?&activity:nullptr;
        if(a) a->advance(engine.snapshot(),engine.snapshot(),engine.snapshot().utc,sampleWindowsInput());
        db.updateTemporal(engine,true,a);
        const auto first=resources(); auto peak=first; auto low=first;
        const auto start=std::chrono::steady_clock::now();
        for(int i=1;i<=60;++i) {
            std::this_thread::sleep_until(start+std::chrono::seconds(i));
            const auto before=engine.snapshot(); const auto civil=engine.current()?engine.current()->utcEnd:before.utc;
            engine.observe(sampleWindowsTime());
            if(a) a->advance(before,engine.snapshot(),civil,sampleWindowsInput());
            db.updateTemporal(engine,i==60,a);
            // Existing widget uses these three queries at 1 Hz in both modes.
            for(auto period:{CalendarPeriod::Day,CalendarPeriod::Week,CalendarPeriod::Month})
                (void)db.queryHistory(period,engine.snapshot().utc,engine.snapshot().utc);
            const auto r=resources(); peak.privateBytes=std::max(peak.privateBytes,r.privateBytes);
            peak.workingSet=std::max(peak.workingSet,r.workingSet); peak.handles=std::max(peak.handles,r.handles);
            low.privateBytes=std::min(low.privateBytes,r.privateBytes); low.handles=std::min(low.handles,r.handles);
        }
        const auto last=resources();
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"{\"mode\":\""<<argv[1]<<"\",\"seconds\":"<<elapsed
            <<",\"cpu_one_core_percent\":"<<(last.cpu-first.cpu)/100000.0/elapsed
            <<",\"private_min\":"<<low.privateBytes<<",\"private_max\":"<<peak.privateBytes
            <<",\"working_set_max\":"<<peak.workingSet<<",\"handles_min\":"<<low.handles
            <<",\"handles_max\":"<<peak.handles<<",\"handles_end\":"<<last.handles
            <<",\"session_us\":"<<engine.snapshot().elapsed<<",\"activity_us\":"
            <<(a?db.queryActivity(CalendarPeriod::Year,engine.snapshot().utc).duration.count():0)<<"}\n";
    }
    std::filesystem::remove_all(dir);
}
