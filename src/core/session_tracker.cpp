#include "session_tracker.h"

namespace chronox {

SessionTracker::SessionTracker(bool initiallyLocked, TimePoint startedAt)
    : locked_(initiallyLocked) {
    if (!locked_) {
        countingSince_ = startedAt;
    }
}

bool SessionTracker::apply(SessionEvent event, TimePoint at) {
    const bool wasLocked = locked_;
    const bool wasAwake = awake_;
    const bool wasCounting = isCounting();

    switch (event) {
    case SessionEvent::Locked:
        locked_ = true;
        break;
    case SessionEvent::Unlocked:
        locked_ = false;
        break;
    case SessionEvent::Suspended:
        awake_ = false;
        break;
    case SessionEvent::Resumed:
        awake_ = true;
        break;
    }

    if (wasLocked == locked_ && wasAwake == awake_) {
        return false;
    }

    updateCounting(wasCounting, at);
    return true;
}

SessionTracker::Duration SessionTracker::elapsed(TimePoint at) const {
    if (isCounting()) {
        return accumulated_ + (at - countingSince_);
    }
    return accumulated_;
}

bool SessionTracker::isLocked() const noexcept {
    return locked_;
}

bool SessionTracker::isAwake() const noexcept {
    return awake_;
}

bool SessionTracker::isCounting() const noexcept {
    return !locked_ && awake_;
}

void SessionTracker::updateCounting(bool wasCounting, TimePoint at) {
    const bool counting = isCounting();
    if (!wasCounting && counting) {
        countingSince_ = at;
    } else if (wasCounting && !counting) {
        accumulated_ += at - countingSince_;
    }
}

} // namespace chronox
