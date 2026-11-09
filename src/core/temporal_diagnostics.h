#pragma once
#include "temporal_engine.h"
#include <array>
#include <filesystem>
#include <string_view>
namespace chronox {
// Empty directory disables all I/O. Bounded RAM and disk; never throws to caller.
class TemporalDiagnostics {
public:
    explicit TemporalDiagnostics(std::filesystem::path directory = {}, std::uintmax_t limit = 262144);
    void record(const TemporalSnapshot&, std::string_view reason) noexcept;
    bool failed() const {return failed_;}
    std::size_t retained() const {return retained_;}
    std::uint64_t dropped() const {return dropped_;}
private:
    std::filesystem::path directory_;
    std::uintmax_t limit_;
    std::array<TemporalSnapshot,128> ring_{};
    std::size_t position_{}, retained_{};
    std::uint64_t dropped_{};
    Timestamp lastStatus_{}, rateStart_{}, corrections_{-1}, uncertainties_{-1};
    unsigned rate_{};
    bool failed_{}, initialized_{};
};
} // namespace chronox
