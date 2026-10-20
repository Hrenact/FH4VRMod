#pragma once
#include <cstdint>
namespace right_eye_probe {
// Diagnostic only: show the same right-eye source bounds to both eyes.
// A held key cannot extend the five-second window; release before rearming.
struct Gate {
    bool held=true;
    uint64_t started=0;
    bool armed=false;
    bool Active(bool down,uint64_t now) {
        if(!down){held=false;armed=false;return false;}
        if(!held){held=true;started=now;armed=true;}
        return armed && now>=started && now-started<5000;
    }
};
template<class Bounds> void Apply(bool active,Bounds (&bounds)[2]) {
    if(active)bounds[0]=bounds[1];
}
}
