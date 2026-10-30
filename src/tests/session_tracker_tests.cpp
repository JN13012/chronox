#include "session_tracker.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using chronox::SessionEvent;
using chronox::SessionTracker;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void startsImmediatelyWhenInitiallyUnlocked() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(false, start);
    require(tracker.isCounting(), "unlocked startup counts immediately");
    require(tracker.elapsed(start + 5s) == 5s, "startup duration is counted");
}

void lockedStartupWaitsForUnlock() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(true, start);
    require(!tracker.isCounting(), "locked startup does not count");
    require(tracker.elapsed(start + 5s) == 0s, "locked startup remains at zero");
    require(tracker.apply(SessionEvent::Unlocked, start + 5s), "unlock changes state");
    require(tracker.elapsed(start + 8s) == 3s, "counting starts on unlock");
}

void lockAndUnlockAccumulateOnlyUnlockedTime() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(false, start);
    require(tracker.apply(SessionEvent::Locked, start + 4s), "lock changes state");
    require(tracker.apply(SessionEvent::Unlocked, start + 10s), "unlock changes state");
    require(tracker.elapsed(start + 13s) == 7s, "locked duration is excluded");
}

void sleepAndWakeAccumulateOnlyAwakeTime() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(false, start);
    require(tracker.apply(SessionEvent::Suspended, start + 3s), "suspend changes state");
    require(tracker.apply(SessionEvent::Resumed, start + 9s), "resume changes state");
    require(tracker.elapsed(start + 11s) == 5s, "sleep duration is excluded");
}

void duplicateEventsAreIdempotent() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(false, start);
    require(!tracker.apply(SessionEvent::Unlocked, start + 2s), "duplicate unlock is ignored");
    require(tracker.apply(SessionEvent::Locked, start + 3s), "first lock changes state");
    require(!tracker.apply(SessionEvent::Locked, start + 5s), "duplicate lock is ignored");
    require(tracker.elapsed(start + 7s) == 3s, "duplicate events do not alter elapsed time");
}

void wakingWhileLockedDoesNotResumeCounting() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(false, start);
    tracker.apply(SessionEvent::Locked, start + 2s);
    tracker.apply(SessionEvent::Suspended, start + 3s);
    tracker.apply(SessionEvent::Resumed, start + 8s);
    require(!tracker.isCounting(), "waking while locked remains inactive");
    tracker.apply(SessionEvent::Unlocked, start + 10s);
    require(tracker.elapsed(start + 12s) == 4s, "counting resumes after unlock");
}

void sleepAndWakeWhileLockedDoNotAddTime() {
    const auto start = SessionTracker::TimePoint{};
    SessionTracker tracker(true, start);
    tracker.apply(SessionEvent::Suspended, start + 2s);
    tracker.apply(SessionEvent::Resumed, start + 8s);
    require(tracker.elapsed(start + 10s) == 0s, "locked sleep and wake do not add time");
    tracker.apply(SessionEvent::Unlocked, start + 10s);
    require(tracker.elapsed(start + 12s) == 2s, "unlock starts a fresh interval");
}

} // namespace

int main() {
    startsImmediatelyWhenInitiallyUnlocked();
    lockedStartupWaitsForUnlock();
    lockAndUnlockAccumulateOnlyUnlockedTime();
    sleepAndWakeAccumulateOnlyAwakeTime();
    duplicateEventsAreIdempotent();
    wakingWhileLockedDoesNotResumeCounting();
    sleepAndWakeWhileLockedDoNotAddTime();
    std::cout << "All session tracker tests passed.\n";
}
