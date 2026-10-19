#pragma once
#include <atomic>
#include <cstdint>
namespace aer_probe {
// A commanded phase is not proof that the rendered image belongs to that eye.
#ifdef FH4VR_AER_DEFERRED
constexpr unsigned PhaseCount=9;
inline float Eye(unsigned phase) {
    constexpr float sequence[]={0,0,-.032f,-.032f,.032f,-.032f,.032f,.032f,-.032f,.032f};
    return phase<=PhaseCount?sequence[phase]:0;
}
#else
constexpr unsigned PhaseCount=7; // center, L, R, L, R, L, R
inline float Eye(unsigned phase) { return phase<2 || phase>PhaseCount ? 0.0f : (phase%2==0 ? -.032f:.032f); }
#endif
struct State {
    std::atomic<unsigned> phase{0};
    std::atomic<uint64_t> deadline{0};
    std::atomic<uint64_t> calls[PhaseCount+1]{},applied[PhaseCount+1]{},rejected[PhaseCount+1]{};
    std::atomic<bool> used{false};
    bool Start(uint64_t now) {
        bool expected=false;if(!used.compare_exchange_strong(expected,true)) return false;
        deadline.store(now+10000);phase.store(1);return true;
    }
    unsigned Read(bool foreground,uint64_t now) const {
        auto p=phase.load();return foreground && now<deadline.load() && p<=PhaseCount?p:0;
    }
    void Stop() {phase.store(0);}
    bool Advance(unsigned expected,uint64_t now) {
        if(now>=deadline.load()) {Stop();return false;}
        if(expected>=PhaseCount) {Stop();return false;}
        return phase.compare_exchange_strong(expected,expected+1);
    }
};
inline State state;
}
