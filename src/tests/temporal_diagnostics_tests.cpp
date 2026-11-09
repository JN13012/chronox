#include "temporal_diagnostics.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace chronox;
void check(bool ok){if(!ok) throw std::runtime_error("diagnostics invariant failed");}
int main(){try{
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto dir=root/("chronox-diagnostics-test-"+std::to_string(SessionTracker::Clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path root,dir;~Cleanup(){if(dir.is_absolute()&&dir.parent_path()==root){std::error_code e;std::filesystem::remove_all(dir,e);}}} cleanup{root,dir};
    TemporalEngine engine(false,{123,0,0});
    TemporalDiagnostics disabled;disabled.record(engine.snapshot(),"startup");check(disabled.retained()==0);
    TemporalDiagnostics log(dir,1024);
    for(int i=0;i<300;++i){engine.observe({123+i*1'000'000LL,i*1'000'000LL,i*1'000'000LL});log.record(engine.snapshot(),"event");}
    check(!log.failed()&&log.retained()==128);
    check(std::filesystem::file_size(dir/"time-events.jsonl")<=1024);
    check(std::filesystem::file_size(dir/"time-events.1.jsonl")<=1024);
    std::ifstream f(dir/"time-status.json");std::string text((std::istreambuf_iterator<char>(f)),{});
    check(text.find("\"internal_utc_us\":299000123")!=std::string::npos && text.find("\"offset_us\":0")!=std::string::npos);
    for(int i=0;i<100;++i) log.record(engine.snapshot(),"event");check(log.dropped()>0);
    const auto blocked=dir/"not-a-directory";{std::ofstream out(blocked);out<<"fixture";}
    TemporalDiagnostics broken(blocked);broken.record(engine.snapshot(),"event");check(broken.failed());
    engine.observe({300000123,300000000,300000000});check(engine.snapshot().elapsed==300000000);
    std::cout<<"Bounded logging, direct UTC snapshot, disabled/failure isolation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
