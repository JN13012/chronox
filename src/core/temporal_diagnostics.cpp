#include "temporal_diagnostics.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace chronox {
namespace {
void publish(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING)) throw std::runtime_error("diagnostic publication failed");
#else
    std::filesystem::rename(from,to);
#endif
}
}
TemporalDiagnostics::TemporalDiagnostics(std::filesystem::path directory, std::uintmax_t limit)
    : directory_(std::move(directory)),limit_(std::clamp<std::uintmax_t>(limit,1024,262144)) {}
void TemporalDiagnostics::record(const TemporalSnapshot& s, std::string_view reason) noexcept {
    if(directory_.empty() || failed_) return;
    try {
        const bool event=reason!="sample" || s.corrections!=corrections_ || s.uncertainties!=uncertainties_;
        const bool status=!initialized_ || s.sample.steady-lastStatus_>=1'000'000;
        if(!event && !status) return;
        std::filesystem::create_directories(directory_);
        // Reasons originate in the executable; sanitize nevertheless to keep JSON valid.
        std::string label;
        for(char c:reason.substr(0,32)) if((c>='a'&&c<='z') || c=='-') label+=c;
        std::ostringstream out;
        out<<"{\"reason\":\""<<label<<"\",\"internal_utc_us\":"<<s.utc
           <<",\"windows_utc_us\":"<<s.sample.utc<<",\"offset_us\":"<<s.utc-s.sample.utc
           <<",\"steady_us\":"<<s.sample.steady<<",\"awake_us\":"<<s.sample.awake
           <<",\"elapsed_us\":"<<s.elapsed<<",\"epoch\":"<<s.epoch
           <<",\"corrections\":"<<s.corrections<<",\"uncertainties\":"<<s.uncertainties
           <<",\"uncertain\":"<<(s.uncertain?"true":"false")
           <<",\"counting\":"<<(s.counting?"true":"false")<<",\"dropped\":"<<dropped_<<"}\n";
        const auto line=out.str();
        if(status) {
            const auto temp=directory_/"time-status.tmp";
            {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.exceptions(std::ios::badbit|std::ios::failbit);f<<line;f.close();}
            publish(temp,directory_/"time-status.json");lastStatus_=s.sample.steady;
        }
        if(event) {
            if(!initialized_ || s.sample.steady-rateStart_>=1'000'000) {rate_=0;rateStart_=s.sample.steady;}
            if(rate_++>=16) {++dropped_;return;}
            ring_[position_]=s;position_=(position_+1)%ring_.size();retained_=std::min(retained_+1,ring_.size());
            const auto log=directory_/"time-events.jsonl";
            if(std::filesystem::exists(log) && std::filesystem::file_size(log)+line.size()>limit_)
                publish(log,directory_/"time-events.1.jsonl");
            std::ofstream f(log,std::ios::binary|std::ios::app);f.exceptions(std::ios::badbit|std::ios::failbit);f<<line;f.close();
        }
        corrections_=s.corrections;uncertainties_=s.uncertainties;initialized_=true;
    } catch(...) { failed_=true; }
}
} // namespace chronox
