#include "session_tracker.h"
#include <stdexcept>

namespace chronox {

SessionTracker::SessionTracker(bool initiallyLocked, TimePoint startedAt)
    : locked_(initiallyLocked), lastEvent_(startedAt) {
    if (!locked_) {
        countingSince_ = startedAt;
    }
}

bool SessionTracker::apply(SessionEvent event, TimePoint at) {
    validate(at);
    lastEvent_=at;
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
    validate(at);
    if (isCounting()) {
        return accumulated_ + (at - countingSince_);
    }
    return accumulated_;
}

SessionTracker::Duration SessionTracker::currentSessionElapsed(TimePoint at) const {
    validate(at);
    return isCounting() ? at - countingSince_ : Duration{};
}

bool SessionTracker::isLocked() const noexcept {
    return locked_;
}

void SessionTracker::validate(TimePoint at) const {
    if(at<lastEvent_) throw std::invalid_argument("Session event time regressed");
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
