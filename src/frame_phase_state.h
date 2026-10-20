#pragma once
#include <atomic>
#include <cstdint>
namespace frame_phase {
inline constexpr unsigned Count=24;
inline constexpr int Sequence[Count]={0,0,0,-1,-1,-1,1,1,1,0,0,-1,1,-1,1,1,-1,-1,1,-1,1,0,0,0};
inline float Eye(unsigned phase){return phase && phase<=Count?Sequence[phase-1]*.032f:0.f;}
struct State {
    std::atomic<unsigned> phase{0};
    std::atomic<uint64_t> deadline{0};
    std::atomic<bool> used{false};
    std::atomic<bool> begun{false};
    std::atomic<uint64_t> calls[Count+1][2]{},applied[Count+1][2]{},rejected[Count+1][2]{};
    bool Reserve(){bool expected=false;return used.compare_exchange_strong(expected,true);}
    bool Begin(uint64_t now){bool expected=false;if(!used || !begun.compare_exchange_strong(expected,true))return false;deadline=now+10000;phase=1;return true;}
    bool Start(uint64_t now){return Reserve() && Begin(now);}
    unsigned Read(bool focused,uint64_t now)const{const auto p=phase.load();return focused && now<deadline.load() && p<=Count?p:0;}
    bool Advance(unsigned expected,uint64_t now){
        if(now>=deadline.load() || expected>=Count){Stop();return false;}
        return phase.compare_exchange_strong(expected,expected+1);
    }
    void Stop(){phase=0;}
};
inline State state;
}
