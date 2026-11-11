#include "activity_tracker.h"
#include <iostream>
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
    Fixture sleep; sleep.step(10*S); auto b=sleep.e.snapshot();
    sleep.e.observe({1000*S,1000*S,10*S});
    sleep.a.advance(b,sleep.e.snapshot(),10*S,{1000000,0,true});
    check(sleep.total()==10*S && sleep.a.state()==ActivityState::Uncertain);
    ActivityTracker wrap; TemporalEngine engine(false,{0,0,0}); auto snap=engine.snapshot();
    const std::uint64_t W=1ULL<<32;
    wrap.advance(snap,snap,0,{W-500,static_cast<std::uint32_t>(W-1000),true});
    wrap.advance(snap,snap,0,{W+500,250,true}); check(wrap.uncertainties()==0);
    wrap.advance(snap,snap,0,{W+600,200,true}); check(wrap.uncertainties()==1);
    bool rejected=false; try { ActivityTracker bad({true,3}); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected);
    std::cout << "Activity boundaries, invalid inputs, recovery and wrap: PASS\n";
}
