#include "session_database.h"
#include <filesystem>
#include <iostream>
#include <string_view>
int main(int argc,char** argv) try {
    if(argc!=2 && !(argc==3 && std::string_view(argv[2])=="--read-only")) return 2;
    const auto copy=std::filesystem::canonical(argv[1]);
    const auto allowed=std::filesystem::canonical(std::filesystem::current_path()/"build"/"activity-audit");
    if(copy.parent_path()!=allowed || copy.filename()!="copy-v3.db" || std::filesystem::is_symlink(argv[1])) return 3;
    chronox::SessionDatabase db(copy,argc==3?chronox::StorageMode::ReadOnly:chronox::StorageMode::Activity);
    const auto anchor=db.latestRecordedTimestamp();
    std::cout<<"{\"durations_us\":[";
    bool comma=false;
    for(auto p:{chronox::CalendarPeriod::Day,chronox::CalendarPeriod::Week,chronox::CalendarPeriod::Month,chronox::CalendarPeriod::Year}) {
        if(comma) std::cout<<','; comma=true; std::cout<<db.duration(p,anchor).count();
    }
    std::cout<<"]}\n";
}
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
