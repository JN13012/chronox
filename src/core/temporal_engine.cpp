#include "temporal_engine.h"
#include <cstdlib>
#include <algorithm>
#include <stdexcept>

namespace chronox {
namespace {
Timestamp us(SessionTracker::Duration d) {
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}
}
TemporalEngine::TemporalEngine(bool locked, TimeSample initial)
    : last_(initial), tracker_(locked, logical_) {
    if(initial.uncertainty<0) throw std::invalid_argument("Invalid sample uncertainty");
    if(initial.uncertainty>100'000) {
        tracker_.apply(SessionEvent::Suspended,logical_);
        uncertain_=true; ++uncertainties_;
    }
    if (tracker_.isCounting()) { ++session_; open(); }
}
void TemporalEngine::open() {
    current_ = MeasuredSpan{++next_, epoch_, session_, last_.utc, last_.utc,
                           last_.awake, last_.awake, 0, true};
}
void TemporalEngine::close() {
    if (current_) {
        current_->active = false;
        pending_.push_back(*current_);
        current_.reset();
    }
}
void TemporalEngine::observe(TimeSample s) {
    if (stopped_) throw std::logic_error("Temporal engine is stopped");
    const Timestamp ds = s.steady - last_.steady, da = s.awake - last_.awake;
    if (ds < 0 || da < 0 || s.uncertainty < 0) throw std::invalid_argument("Invalid monotonic sample");
    // A missing power transition, or impossible counter pair, cannot silently
    // turn the unobserved interval into active time. Exclude it and latch doubt.
    const Timestamp tolerance=std::max<Timestamp>(2'000,last_.uncertainty+s.uncertainty);
    const bool gap = tracker_.isAwake() &&
        (ds-da>tolerance || da-ds>tolerance || s.uncertainty>100'000);
    if (gap) {
        close();
        tracker_.apply(SessionEvent::Suspended, logical_);
        uncertain_ = true;
        ++uncertainties_;
    } else {
        logical_ += std::chrono::microseconds(da);
        if (current_) {
            current_->duration += da;
            current_->monoEnd = s.awake;
            current_->utcEnd = current_->utcStart + current_->duration;
        }
    }
    // Compare with the epoch's prediction, so small slews accumulate rather
    // than disappearing under a per-tick threshold. Civil now is always s.utc.
    const Timestamp predicted = current_ ? current_->utcEnd : last_.utc + ds;
    const bool correction = std::abs(s.utc - predicted) > 50'000;
    last_ = s;
    if (correction) {
        close(); ++epoch_; ++corrections_;
        if (tracker_.isCounting() && !uncertain_) open();
    }
}
void TemporalEngine::event(SessionEvent e, TimeSample s) {
    observe(s);
    const bool was = tracker_.isCounting();
    if (e == SessionEvent::Resumed) uncertain_ = false;
    if (!tracker_.apply(e, logical_)) return;
    const bool counting = tracker_.isCounting();
    if (was && !counting) close();
    if (!was && counting && !uncertain_) { ++session_; open(); }
}
void TemporalEngine::reconcile(bool locked, TimeSample s) {
    observe(s);
    // Caller has established current awake + WTS state, not the missing past.
    if (uncertain_) {
        tracker_.apply(SessionEvent::Locked, logical_);
        tracker_.apply(SessionEvent::Resumed, logical_);
        uncertain_ = false;
    }
    event(locked ? SessionEvent::Locked : SessionEvent::Unlocked, s);
}
void TemporalEngine::stop(TimeSample s) {
    if(stopped_) return; // A failed durable transaction may be retried safely.
    observe(s); close(); tracker_.apply(SessionEvent::Locked, logical_); stopped_ = true;
}
TemporalSnapshot TemporalEngine::snapshot() const {
    return {last_, last_.utc, us(tracker_.elapsed(logical_)),
            us(tracker_.currentSessionElapsed(logical_)), epoch_, corrections_,
            uncertainties_, tracker_.isCounting() && !stopped_ && !uncertain_, uncertain_};
}
} // namespace chronox
