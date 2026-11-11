#include "activity_tracker.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace chronox {
bool validActivitySettings(ActivitySettings s) {
    return s.minutes==1 || s.minutes==5 || s.minutes==10 || s.minutes==15 || s.minutes==30;
}
ActivityTracker::ActivityTracker(ActivitySettings s) : settings_(s) {
    if (!validActivitySettings(s)) throw std::invalid_argument("Invalid inactivity threshold");
}
void ActivityTracker::close() {
    if(current_) { current_->active=false; pending_.push_back(*current_); current_.reset(); }
}
void ActivityTracker::configure(ActivitySettings s) {
    if (!validActivitySettings(s)) throw std::invalid_argument("Invalid inactivity threshold");
    if(s==settings_) return;
    close(); settings_=s;
    // Re-evaluate prospectively at the next observation, never rewrite a span.
    deadline_.reset(); first_=true;
}
void ActivityTracker::setState(ActivityState s) {
    state_=s;
    if(s!=ActivityState::Active && s!=ActivityState::Inactive) { close(); deadline_.reset(); }
}
void ActivityTracker::stop() { close(); deadline_.reset(); }
void ActivityTracker::append(Timestamp from, Timestamp to, Timestamp utc, const TemporalSnapshot& b) {
    if(to<=from) return;
    if(current_ && (current_->monoEnd!=from || current_->utcEnd!=utc || current_->epoch!=b.epoch)) close();
    if(!current_) current_=MeasuredSpan{++sequence_,b.epoch,0,utc,utc,from,from,0,true};
    current_->duration+=to-from; current_->monoEnd=to; current_->utcEnd+=to-from;
}
void ActivityTracker::advance(const TemporalSnapshot& b, const TemporalSnapshot& a,
                              Timestamp civil, InputSample in) {
    const Timestamp delta=a.elapsed-b.elapsed;
    if(delta<0 || delta>a.sample.awake-b.sample.awake) throw std::invalid_argument("Invalid session delta");
    const Timestamp from=b.sample.awake, end=from+delta;
    bool good=in.valid && (!observedTick_ || in.ticks>=*observedTick_);
    std::uint64_t candidate{};
    if(good) {
        // The most recent congruent timestamp at or before uptime. Zero is valid.
        const auto age=static_cast<std::uint32_t>(static_cast<std::uint32_t>(in.ticks)-in.lastInput);
        good=age<=in.ticks;
        if(good) { candidate=in.ticks-age; good=!inputTick_ || candidate>=*inputTick_; }
        // An unchanged DWORD cannot become a fresh event at the next wrap.
        if(good && inputTick_ && static_cast<std::uint32_t>(*inputTick_)==in.lastInput) candidate=*inputTick_;
    }
    if(in.valid) observedTick_=in.ticks;
    const bool fresh=good && (!inputTick_ || candidate>*inputTick_);
    const bool safeMapping=!a.uncertain && !b.uncertain &&
        std::abs((a.sample.steady-b.sample.steady)-(a.sample.awake-b.sample.awake))<=
            std::max<Timestamp>(2'000,a.sample.uncertainty+b.sample.uncertainty);
    const Timestamp threshold=static_cast<Timestamp>(settings_.minutes)*60'000'000;
    std::optional<Timestamp> eventAwake;
    if(good && in.ticks-candidate<=static_cast<std::uint64_t>(threshold/1000)) {
        const auto age=static_cast<Timestamp>(in.ticks-candidate)*1000;
        if(age<=a.sample.awake) eventAwake=a.sample.awake-age;
    }
    Timestamp limit=deadline_.value_or(from);
    // A newly observed input before the known expiry proves continuous coverage.
    if(!first_ && fresh && safeMapping && eventAwake && deadline_ && *eventAwake<=*deadline_ && *eventAwake>=from)
        limit=*eventAwake+threshold;
    if(!settings_.enabled) limit=end;
    if(delta>0) append(from,std::clamp(limit,from,end),civil,b);
    if(limit<end || a.epoch!=b.epoch) close();
    if(!good) { if(!inputUncertain_) ++uncertainties_; inputUncertain_=true; }
    else if(fresh || first_) inputUncertain_=false;
    if(good && fresh) inputTick_=candidate;
    if(a.uncertain || !a.counting) {
        close(); deadline_.reset();
        if(a.uncertain) state_=ActivityState::Uncertain;
        else if(state_!=ActivityState::Sleeping) state_=ActivityState::Locked;
    } else {
        if(!settings_.enabled) { deadline_.reset(); state_=ActivityState::Active; }
        else {
            if(good && eventAwake && (first_ || fresh) && (safeMapping || first_)) deadline_=*eventAwake+threshold;
            state_=inputUncertain_ ? ActivityState::Uncertain :
                deadline_ && a.sample.awake<*deadline_ ? ActivityState::Active : ActivityState::Inactive;
            if(deadline_ && a.sample.awake>=*deadline_) close();
        }
    }
    first_=false;
}
}
