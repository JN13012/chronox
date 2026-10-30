#pragma once

#include <cstdint>

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

TimeRange calendarPeriodContaining(Timestamp timestamp, CalendarPeriod period);

} // namespace chronox
