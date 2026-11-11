#include "activity_tracker.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
using namespace chronox;
constexpr Timestamp S=1'000'000;
void check(bool x) { if(!x) throw std::runtime_error("Activity invariant failed"); }
struct Fixture {
    TemporalEngine e{false,{0,0,0}};
    ActivityTracker a;
    Timestamp time{};
    Fixture() { a.advance(e.snapshot(),e.snapshot(),0,{0,0,true}); }
    void step(Timestamp t, std::uint32_t input=0, bool valid=true, Timestamp utc=-1) {
        auto b=e.snapshot(); auto civil=e.current()?e.current()->utcEnd:b.utc;
        time=t; e.observe({utc<0?t:utc,t,t});
        a.advance(b,e.snapshot(),civil,{static_cast<std::uint64_t>(t/1000),input,valid});
    }
    Timestamp total() { Timestamp n{}; for(auto& s:a.pending()) n+=s.duration; if(a.current()) n+=a.current()->duration; return n; }
};
int main() {
    Fixture f; f.step(299*S); check(f.total()==299*S && f.a.state()==ActivityState::Active);
    f.step(300*S); check(f.total()==300*S && f.a.state()==ActivityState::Inactive);
    f.step(1800*S); check(f.total()==300*S); // media/PDF cannot extend it
    f.step(1801*S,1'801'000); check(f.total()==300*S);
    f.step(1802*S,1'801'000); check(f.total()==301*S);
    Fixture delayed; delayed.step(1800*S); check(delayed.total()==300*S);
    Fixture invalid; invalid.step(299*S); invalid.step(301*S,0,false);
    check(invalid.total()==300*S && invalid.a.uncertainties()==1);
    invalid.step(302*S,302'000); invalid.step(303*S,302'000); check(invalid.total()==301*S);
    Fixture continuous; continuous.step(299*S); continuous.step(301*S,299'500);
    check(continuous.total()==301*S);
    Fixture correction; correction.step(60*S,0,true,10*S); correction.step(120*S,0,true,500*S);
    check(correction.total()==120*S && correction.a.pending().size()==2);
    Fixture disabled; disabled.step(301*S); disabled.a.configure({false,5});
    disabled.a.advance(disabled.e.snapshot(),disabled.e.snapshot(),301*S,{301000,0,true});
    disabled.step(1800*S); check(disabled.total()==1799*S);
    Fixture change; change.step(60*S); change.a.configure({true,1});
    change.a.advance(change.e.snapshot(),change.e.snapshot(),60*S,{60000,0,true});
    change.step(70*S); check(change.total()==60*S);
    Fixture settingFailure; settingFailure.step(30*S);
    settingFailure.a.configure({true,10});
    settingFailure.a.advance(settingFailure.e.snapshot(),settingFailure.e.snapshot(),30*S,{30000,0,false});
    settingFailure.step(601*S,0,false); check(settingFailure.total()==600*S);
    Fixture sleep; sleep.step(10*S); auto b=sleep.e.snapshot();
    sleep.e.observe({1000*S,1000*S,10*S});
    sleep.a.advance(b,sleep.e.snapshot(),10*S,{1000000,0,true});
    check(sleep.total()==10*S && sleep.a.state()==ActivityState::Uncertain);
    const std::uint64_t W=1ULL<<32;
    const Timestamp uptime=static_cast<Timestamp>(W-1000)*1000;
    TemporalEngine wrappedEngine(false,{0,uptime,uptime}); ActivityTracker wrapped;
    wrapped.advance(wrappedEngine.snapshot(),wrappedEngine.snapshot(),0,{W-1000,static_cast<std::uint32_t>(W-1000),true});
    auto wb=wrappedEngine.snapshot(); wrappedEngine.observe({2*S,uptime+2*S,uptime+2*S});
    wrapped.advance(wb,wrappedEngine.snapshot(),0,{W+1000,static_cast<std::uint32_t>(W-1000),true});
    check(wrapped.current()->duration==2*S);
    wb=wrappedEngine.snapshot(); wrappedEngine.observe({301*S,uptime+301*S,uptime+301*S});
    wrapped.advance(wb,wrappedEngine.snapshot(),2*S,{W+300000,static_cast<std::uint32_t>(W-1000),true});
    check(!wrapped.current() && wrapped.pending().front().duration==300*S);
    Fixture paused; paused.step(10*S); auto pausedBefore=paused.e.snapshot();
    paused.e.observe({1000*S,11*S,11*S});
    paused.a.advance(pausedBefore,paused.e.snapshot(),10*S,{1000000,0,true});
    check(paused.total()==10*S && paused.a.state()==ActivityState::Uncertain);
    paused.a.configure({false,5});
    pausedBefore=paused.e.snapshot(); paused.e.observe({1001*S,12*S,12*S});
    paused.a.advance(pausedBefore,paused.e.snapshot(),1000*S,{1001000,0,true});
    check(paused.total()==10*S && paused.a.state()==ActivityState::Uncertain);
    paused.a.reconcilePower();
    pausedBefore=paused.e.snapshot(); paused.e.observe({1002*S,13*S,13*S});
    paused.a.advance(pausedBefore,paused.e.snapshot(),1001*S,{1002000,0,true});
    check(paused.total()==11*S && paused.a.state()==ActivityState::Active);
    // Lock/resume and duplicate notifications never manufacture activity.
    Fixture locked; locked.step(10*S); auto lb=locked.e.snapshot();
    locked.e.event(SessionEvent::Locked,{11*S,11*S,11*S});
    locked.a.advance(lb,locked.e.snapshot(),10*S,{11000,0,true}); locked.a.setState(ActivityState::Locked);
    check(locked.total()==11*S);
    lb=locked.e.snapshot(); locked.e.event(SessionEvent::Unlocked,{20*S,20*S,20*S});
    locked.a.advance(lb,locked.e.snapshot(),11*S,{20000,0,true});
    locked.step(21*S); check(locked.total()==11*S && locked.a.state()==ActivityState::Inactive);
    locked.step(22*S,22000); locked.step(23*S,22000); check(locked.total()==12*S);
    // Missing history after a long poll preserves the known prefix and flags doubt.
    Fixture ambiguous; ambiguous.step(1800*S,1799000);
    check(ambiguous.total()==300*S && ambiguous.a.uncertainties()==1);
    // A newly received but regressive DWORD cannot reset the inactivity deadline.
    Fixture regression; regression.step(10*S,10000); regression.step(20*S,9000);
    check(regression.a.uncertainties()==1); regression.step(400*S,9000);
    check(regression.total()==310*S);
    Fixture future; future.step(10*S,11000); future.step(12*S,11000); future.step(310*S,11000);
    check(future.total()==300*S && future.a.state()==ActivityState::Uncertain);
    future.step(311*S,311000); future.step(312*S,311000); check(future.total()==301*S);
    Fixture power; power.step(10*S); auto pb=power.e.snapshot();
    power.e.event(SessionEvent::Suspended,{11*S,11*S,11*S});
    power.a.advance(pb,power.e.snapshot(),10*S,{11000,0,true}); power.a.setState(ActivityState::Sleeping);
    pb=power.e.snapshot(); power.e.event(SessionEvent::Resumed,{1000*S,1000*S,11*S});
    power.e.reconcile(false,{1000*S,1000*S,11*S});
    power.a.advance(pb,power.e.snapshot(),11*S,{1000000,0,true});
    check(power.total()==11*S && power.a.state()==ActivityState::Inactive);
    pb=power.e.snapshot(); power.e.event(SessionEvent::Resumed,{1000*S,1000*S,11*S});
    power.a.advance(pb,power.e.snapshot(),1000*S,{1000000,0,true});
    check(power.total()==11*S);
    // Deterministic long sequence, including civil jumps and delayed polling.
    Fixture random; unsigned rng=314159; Timestamp t{}, expected{}, expiry=300*S;
    std::uint32_t last{};
    for(int i=0;i<5000;++i) {
        rng=rng*1664525u+1013904223u;
        const Timestamp d=(1+rng%600)*S;
        expected+=std::clamp(expiry-t,Timestamp{0},d); t+=d;
        if((rng>>16)%3==0) { last=static_cast<std::uint32_t>(t/1000); expiry=t+300*S; }
        random.step(t,last,true,(i%19==0)?t+3600*S:t);
        check(random.total()==expected && random.total()<=random.e.snapshot().elapsed);
    }
    Timestamp end{};
    for(const auto& span:random.a.pending()) {
        check(span.duration>=0 && span.duration==span.monoEnd-span.monoStart && span.monoStart>=end);
        end=span.monoEnd;
    }
    bool rejected=false; try { ActivityTracker bad({true,3}); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected);
    std::cout << "Activity boundaries, invalid inputs, recovery and wrap: PASS\n";
}
