#include "calendar_period.h"

#include <chrono>
#include <ctime>
#include <stdexcept>

namespace chronox {
namespace {

constexpr Timestamp MicrosecondsPerSecond = 1'000'000;

std::tm localTime(std::time_t timestamp) {
    std::tm result{};
#if defined(_WIN32)
    if (localtime_s(&result, &timestamp) != 0) {
        throw std::runtime_error("localtime_s failed");
    }
#else
    if (localtime_r(&timestamp, &result) == nullptr) {
        throw std::runtime_error("localtime_r failed");
    }
#endif
    return result;
}

std::time_t localTimestamp(std::tm value) {
    value.tm_isdst = -1;
    const std::time_t result = std::mktime(&value);
    if (result == static_cast<std::time_t>(-1)) {
        throw std::runtime_error("mktime failed");
    }
    return result;
}

Timestamp toMicroseconds(std::time_t timestamp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::seconds(timestamp))
        .count();
}

} // namespace

TimeRange calendarPeriodContaining(Timestamp timestamp, CalendarPeriod period) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto time = static_cast<std::time_t>(seconds.count());
    std::tm start = localTime(time);
    start.tm_hour = 0;
    start.tm_min = 0;
    start.tm_sec = 0;

    switch (period) {
    case CalendarPeriod::Day:
        ++start.tm_mday;
        break;
    case CalendarPeriod::Week:
        start.tm_mday -= (start.tm_wday + 6) % 7;
        start.tm_mday += 7;
        break;
    case CalendarPeriod::Month:
        start.tm_mday = 1;
        ++start.tm_mon;
        break;
    case CalendarPeriod::Year:
        start.tm_mday = 1;
        start.tm_mon = 0;
        ++start.tm_year;
        break;
    }

    const Timestamp end = toMicroseconds(localTimestamp(start));
    start.tm_isdst = -1;
    switch (period) {
    case CalendarPeriod::Day:
        --start.tm_mday;
        break;
    case CalendarPeriod::Week:
        start.tm_mday -= 7;
        break;
    case CalendarPeriod::Month:
        --start.tm_mon;
        start.tm_mday = 1;
        break;
    case CalendarPeriod::Year:
        --start.tm_year;
        start.tm_mon = 0;
        start.tm_mday = 1;
        break;
    }
    const Timestamp begin = toMicroseconds(localTimestamp(start));
    if (end <= begin || end - begin > MicrosecondsPerSecond * 366LL * 24LL * 60LL * 60LL) {
        throw std::runtime_error("Invalid local calendar period");
    }
    return {begin, end};
}

} // namespace chronox
