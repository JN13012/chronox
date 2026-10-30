#pragma once

#include <chrono>

namespace chronox {

enum class SessionEvent {
    Locked,
    Unlocked,
    Suspended,
    Resumed,
};

class SessionTracker {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    SessionTracker(bool initiallyLocked, TimePoint startedAt);

    bool apply(SessionEvent event, TimePoint at);
    Duration elapsed(TimePoint at) const;
    bool isLocked() const noexcept;
    bool isAwake() const noexcept;
    bool isCounting() const noexcept;

private:
    bool locked_;
    bool awake_{true};
    Duration accumulated_{};
    TimePoint countingSince_{};

    void updateCounting(bool wasCounting, TimePoint at);
};

} // namespace chronox
