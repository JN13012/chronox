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
    Duration currentSessionElapsed(TimePoint at) const;
    bool isLocked() const noexcept;
    bool isAwake() const noexcept;
    bool isCounting() const noexcept;

private:
    bool locked_;
    bool awake_{true};
    Duration accumulated_{};
    TimePoint countingSince_{};
    TimePoint lastEvent_{};
    void validate(TimePoint at) const;

    void updateCounting(bool wasCounting, TimePoint at);
};

} // namespace chronox
