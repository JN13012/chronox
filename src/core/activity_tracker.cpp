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
    close();
    // A known input remains evidence even if the next API read fails. Move its
    // expiry prospectively; already closed contributions are never revisited.
    if(settings_.enabled && s.enabled && deadline_) {
        *deadline_+=static_cast<Timestamp>(s.minutes-settings_.minutes)*60'000'000;
    } else { deadline_.reset(); first_=true; }
    settings_=s;
}
void ActivityTracker::setState(ActivityState s) {
    state_=s;
    if(s!=ActivityState::Active && s!=ActivityState::Inactive) { close(); deadline_.reset(); }
}
void ActivityTracker::stop() { close(); deadline_.reset(); }
void ActivityTracker::append(Timestamp from, Timestamp to, Timestamp utc, const TemporalSnapshot& b) {
    if(to<=from) return;
    if(current_ && (current_->monoEnd!=from || current_->utcEnd!=utc || current_->epoch!=b.epoch)) close();
    if(!current_) current_=ActivitySpan{MeasuredSpan{++sequence_,b.epoch,0,utc,utc,from,from,0,true},settings_.enabled,settings_.minutes};
    current_->duration+=to-from; current_->monoEnd=to; current_->utcEnd+=to-from;
}
void ActivityTracker::advance(const TemporalSnapshot& b, const TemporalSnapshot& a,
                              Timestamp civil, InputSample in) {
    const Timestamp delta=a.elapsed-b.elapsed;
    if(delta<0 || delta>a.sample.awake-b.sample.awake) throw std::invalid_argument("Invalid session delta");
    const Timestamp from=b.sample.awake, end=from+delta;
    // TickCount includes sleep. A discrepancy also catches a missed transition
    // if a platform's steady counter paused. Allow native tick quantization and
    // the input-source read bracket; never infer an awake interval from uptime.
    const bool tickGap=observedTick_ && in.ticks>=*observedTick_ &&
        std::abs(static_cast<Timestamp>(in.ticks-*observedTick_)*1000-
                 (a.sample.steady-b.sample.steady))>150'000;
    bool good=in.valid && (!observedTick_ || in.ticks>=*observedTick_) &&
        (!rejectedInput_ || in.lastInput!=*rejectedInput_);
    std::uint64_t candidate{};
    bool alias{};
    if(good) {
        // The most recent congruent timestamp at or before uptime. Zero is valid.
        const auto age=static_cast<std::uint32_t>(static_cast<std::uint32_t>(in.ticks)-in.lastInput);
        good=age<=in.ticks;
        if(good) { candidate=in.ticks-age; good=!inputTick_ || candidate>=*inputTick_; }
        // An unchanged DWORD cannot become a fresh event at the next wrap.
        if(good && inputTick_ && static_cast<std::uint32_t>(*inputTick_)==in.lastInput) {
            alias=candidate>*inputTick_; candidate=*inputTick_;
        }
    }
    observedTick_=std::max(in.ticks,observedTick_.value_or(0));
    const bool fresh=good && (!inputTick_ || candidate>*inputTick_);
    const bool safeMapping=!tickGap && !powerUncertain_ && !a.uncertain && !b.uncertain &&
        std::abs((a.sample.steady-b.sample.steady)-(a.sample.awake-b.sample.awake))<=
            std::max<Timestamp>(2'000,a.sample.uncertainty+b.sample.uncertainty);
    // Availability is independent of counted activity: a locked interval can
    // be measured zero. Never bridge a civil jump or an unknown power gap.
    if(coverage_ && (coverage_->epoch!=a.epoch || !safeMapping)) {
        if(safeMapping) coverage_->utcEnd=std::max(coverage_->utcEnd,civil+a.sample.awake-b.sample.awake);
        pendingCoverage_.push_back(*coverage_); coverage_.reset();
    }
    if(safeMapping) {
        if(!coverage_) coverage_=ActivityCoverage{++coverageSequence_,a.epoch,a.utc,a.utc};
        else coverage_->utcEnd=std::max(coverage_->utcEnd,a.utc);
    }
    const Timestamp threshold=static_cast<Timestamp>(settings_.minutes)*60'000'000;
    std::optional<Timestamp> eventAwake;
    if(good && in.ticks-candidate<=static_cast<std::uint64_t>(threshold/1000)) {
        const auto age=static_cast<Timestamp>(in.ticks-candidate)*1000;
        if(age<=a.sample.awake) eventAwake=a.sample.awake-age;
    }
    Timestamp limit=deadline_.value_or(from);
    // A newly observed input before the known expiry proves continuous coverage.
    if(!first_ && fresh && safeMapping && eventAwake && deadline_ && *eventAwake<=*deadline_)
        limit=*eventAwake+threshold;
    if(!settings_.enabled) limit=end;
    if(delta>0 && !tickGap && !powerUncertain_) append(from,std::clamp(limit,from,end),civil,b);
    if(limit<end || a.epoch!=b.epoch) close();
    const bool ambiguous=fresh && eventAwake && deadline_ && *eventAwake>*deadline_ &&
        end>*deadline_ && a.sample.steady-b.sample.steady>2'000'000;
    if(tickGap || (settings_.enabled && (!good || alias || ambiguous || (fresh && !safeMapping && !first_)))) {
        if(!inputUncertain_) ++uncertainties_; inputUncertain_=true;
    }
    else if(fresh || first_ || !settings_.enabled) inputUncertain_=false;
    // An invalid future value must not become a new input merely because time
    // catches up. Require a different, valid newer observation to clear it.
    if(in.valid && !good) rejectedInput_=in.lastInput;
    if(good && fresh) { inputTick_=candidate; rejectedInput_.reset(); }
    if(tickGap) powerUncertain_=true;
    if(powerUncertain_) { close(); deadline_.reset(); state_=ActivityState::Uncertain; first_=false; return; }
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
