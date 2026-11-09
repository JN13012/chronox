#pragma once

#include "calendar_period.h"

#include <chrono>

namespace chronox {

// Civil projection inside an explicit epoch; UTC may decrease on reanchor.
// Duration/non-overlap guarantees belong to TemporalEngine and storage.
class UtcTimestampClock {
public:
    using SteadyClock = std::chrono::steady_clock;
    using TimePoint = SteadyClock::time_point;

    UtcTimestampClock();
    UtcTimestampClock(TimePoint steadyAnchor, Timestamp utcAnchor);

    Timestamp at(TimePoint steadyNow);
    void reanchorAfterResume(TimePoint steadyNow);
    void alignWithPersistedTime(Timestamp timestamp);
    void reanchor(TimePoint steadyNow, Timestamp utcNow);

private:
    static Timestamp wallClockNow();

    TimePoint steadyAnchor_;
    Timestamp utcAnchor_{};

};

} // namespace chronox
