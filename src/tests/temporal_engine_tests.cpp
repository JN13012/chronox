#include "temporal_engine.h"
#include <iostream>
#include <stdexcept>
using namespace chronox;
void check(bool ok) { if (!ok) throw std::runtime_error("temporal invariant failed"); }
int main() {
    TemporalEngine e(false, {1'000'000'000,0,0});
    e.observe({1'010'000'000,10'000'000,10'000'000});
    e.observe({890'000'000,10'000'000,10'000'000});
    check(e.snapshot().utc == 890'000'000 && e.snapshot().elapsed == 10'000'000);
    e.observe({900'000'000,20'000'000,20'000'000});
    check(e.snapshot().elapsed == 20'000'000 && e.pending().size() == 1);
    e.observe({1'500'000'000,620'000'000,20'000'000});
    check(e.snapshot().uncertain && !e.snapshot().counting && e.snapshot().elapsed == 20'000'000);
    e.reconcile(false, {1'500'000'000,620'000'000,20'000'000});
    e.observe({1'510'000'000,630'000'000,30'000'000});
    check(e.snapshot().elapsed == 30'000'000 && e.snapshot().continuous == 10'000'000);
    bool rejected = false;
    try { e.observe({0,0,0}); } catch(const std::invalid_argument&) { rejected = true; }
    check(rejected && e.snapshot().elapsed == 30'000'000);
    std::cout << "Temporal engine invariants passed\n";
}
