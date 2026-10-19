#pragma once
#include <atomic>
#include <cstdint>
namespace stereo_trial {
inline std::atomic<uint64_t> command{0},deadline{0},applied{0},rejected{0};
inline constexpr uint64_t holdFrames=2;
struct ToggleKey {
    bool held=true;
    bool Press(bool foreground,bool down) {
        if(!foreground){held=true;return false;}
        const bool edge=down&&!held;held=down;return edge;
    }
};
inline float Offset(bool foreground,uint64_t now) {
    auto n=command.load();auto end=deadline.load();
    return foreground && n && (!end || now<end) ? (((n-1)/holdFrames)%2 ? .032f:-.032f):0;
}
// Diagnostic settling interval: take only the last Present in each held view.
// This tolerates the measured one-Present lag, but is not a true render-frame tag.
inline int CapturedEye(uint64_t n) { return n<holdFrames || n%holdFrames ? -1:static_cast<int>((n/holdFrames-1)%2); }
}
