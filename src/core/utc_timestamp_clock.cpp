#include "utc_timestamp_clock.h"

#include <algorithm>

namespace chronox {

UtcTimestampClock::UtcTimestampClock()
    : UtcTimestampClock(SteadyClock::now(), wallClockNow()) {}

UtcTimestampClock::UtcTimestampClock(TimePoint steadyAnchor, Timestamp utcAnchor)
    : steadyAnchor_(steadyAnchor), utcAnchor_(utcAnchor), lastTimestamp_(utcAnchor),
      hasLastTimestamp_(true) {}

Timestamp UtcTimestampClock::at(TimePoint steadyNow) {
    const auto offset = std::chrono::duration_cast<std::chrono::microseconds>(
                            steadyNow - steadyAnchor_)
                            .count();
    const Timestamp candidate = utcAnchor_ + offset;
    if (!hasLastTimestamp_ || candidate > lastTimestamp_) {
        lastTimestamp_ = candidate;
        hasLastTimestamp_ = true;
    }
    return lastTimestamp_;
}

void UtcTimestampClock::reanchorAfterResume(TimePoint steadyNow) {
    reanchor(steadyNow, wallClockNow());
}

void UtcTimestampClock::alignWithPersistedTime(Timestamp timestamp) {
    reanchor(SteadyClock::now(), std::max(wallClockNow(), timestamp));
}

Timestamp UtcTimestampClock::wallClockNow() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void UtcTimestampClock::reanchor(TimePoint steadyNow, Timestamp utcNow) {
    if (hasLastTimestamp_ && utcNow < lastTimestamp_) {
        utcNow = lastTimestamp_;
    }
    steadyAnchor_ = steadyNow;
    utcAnchor_ = utcNow;
    lastTimestamp_ = utcNow;
    hasLastTimestamp_ = true;
}

} // namespace chronox
