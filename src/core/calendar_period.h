#pragma once

#include <cstdint>
#include <ctime>

namespace chronox {

using Timestamp = std::int64_t;

enum class CalendarPeriod {
    Day,
    Week,
    Month,
    Year,
};

struct TimeRange {
    Timestamp start;
    Timestamp end;
};

struct CalendarTimeConverter {
    std::tm (*toLocal)(std::time_t);
    std::time_t (*toTimestamp)(std::tm);
};

TimeRange calendarPeriodContaining(Timestamp timestamp, CalendarPeriod period);
TimeRange calendarPeriodContaining(Timestamp timestamp,
                                   CalendarPeriod period,
                                   const CalendarTimeConverter& converter);

} // namespace chronox
