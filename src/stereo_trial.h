#pragma once
#include <atomic>
#include <cstdint>
#include "frame_order_trace.h"
namespace stereo_trial {
inline std::atomic<uint64_t> command{0},deadline{0},applied{0},rejected{0};
#ifndef FH4VR_STEREO_HOLD_FRAMES
#define FH4VR_STEREO_HOLD_FRAMES 2
#endif
inline constexpr uint64_t holdFrames=FH4VR_STEREO_HOLD_FRAMES;
static_assert(holdFrames==1 || holdFrames==2,"Only one-frame trial and two-frame baseline are supported");
#ifndef FH4VR_STEREO_CAPTURE_LAG
#define FH4VR_STEREO_CAPTURE_LAG 0
#endif
inline constexpr uint64_t captureLag=FH4VR_STEREO_CAPTURE_LAG;
static_assert(captureLag<=1,"Only measured zero/one Present delay is supported");
inline uint64_t ImageCommand(uint64_t present){return present>captureLag?present-captureLag:0;}
inline uint64_t CaptureGeneration(uint64_t present){auto n=ImageCommand(present);return n?(n-1)/(holdFrames*2)+1:0;}
// Present has returned: publish the next view before a potentially blocking
// compositor handoff. Game worker threads can query the view during that call.
template<class Finish> inline auto PublishThenFinish(uint64_t next,Finish finish) {
    command.store(next);
#ifdef FH4VR_FRAME_ORDER_TRACE
    frame_order_trace::Record(frame_order_trace::Publish,next);
#endif
    return finish();
}
struct ToggleKey {
    bool held=true;
    bool Press(bool foreground,bool down) {
        if(!foreground){held=true;return false;}
        const bool edge=down&&!held;held=down;return edge;
    }
};
inline constexpr bool RenderAllowed(bool foreground) {
#ifdef FH4VR_KEEP_BACKGROUND_VR
    (void)foreground;return true;
#else
    return foreground;
#endif
}
inline float Offset(bool foreground,uint64_t now) {
    auto n=command.load();auto end=deadline.load();
    return RenderAllowed(foreground) && n && (!end || now<end) ? (((n-1)/holdFrames)%2 ? .032f:-.032f):0;
}
// Select the last image of each held command after the measured Present delay.
// This is a diagnostic fixed-delay assumption, not a render-frame provenance tag.
inline int CapturedEye(uint64_t present) { const auto n=ImageCommand(present);return n<holdFrames || n%holdFrames ? -1:static_cast<int>((n/holdFrames-1)%2); }
}
