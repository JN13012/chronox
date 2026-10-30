#pragma once

#include "calendar_period.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace chronox {

class SessionDatabase {
public:
    explicit SessionDatabase(const std::filesystem::path& path);
    ~SessionDatabase();

    SessionDatabase(const SessionDatabase&) = delete;
    SessionDatabase& operator=(const SessionDatabase&) = delete;
    SessionDatabase(SessionDatabase&&) noexcept;
    SessionDatabase& operator=(SessionDatabase&&) noexcept;

    void setCounting(bool counting, Timestamp timestamp);
    void checkpoint(Timestamp timestamp);
    Timestamp latestRecordedTimestamp() const;
    std::chrono::microseconds duration(CalendarPeriod period, Timestamp now) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
