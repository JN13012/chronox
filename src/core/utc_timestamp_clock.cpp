#include "utc_timestamp_clock.h"

#include <algorithm>

namespace chronox {

UtcTimestampClock::UtcTimestampClock()
    : UtcTimestampClock(SteadyClock::now(), wallClockNow()) {}

UtcTimestampClock::UtcTimestampClock(TimePoint steadyAnchor, Timestamp utcAnchor)
    : steadyAnchor_(steadyAnchor), utcAnchor_(utcAnchor) {}

Timestamp UtcTimestampClock::at(TimePoint steadyNow) {
    const auto offset = std::chrono::duration_cast<std::chrono::microseconds>(
                            steadyNow - steadyAnchor_)
                            .count();
    return utcAnchor_ + offset;
}

void UtcTimestampClock::reanchorAfterResume(TimePoint steadyNow) {
    reanchor(steadyNow, wallClockNow());
}

void UtcTimestampClock::alignWithPersistedTime(Timestamp timestamp) {
    (void)timestamp; // Historical civil dates never constrain current UTC.
    reanchor(SteadyClock::now(), wallClockNow());
}

Timestamp UtcTimestampClock::wallClockNow() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void UtcTimestampClock::reanchor(TimePoint steadyNow, Timestamp utcNow) {
    steadyAnchor_ = steadyNow;
    utcAnchor_ = utcNow;
}

} // namespace chronox
