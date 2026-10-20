#pragma once
#include "head_pose.h"
#include "ui_smoothing.h"
namespace render_cycle {
struct Snapshot {
    uint64_t command=0,serial=0;
    HeadPose pose{};
    ui_smoothing::Offset ui{};
    uint64_t discontinuities=0,invalidThroughCommand=0;
    float absolute[12]{};
    uint64_t poseGeneration=0;
};
// Candidate cycle boundary is an observed camera-setup callsite, not a proven
// engine frame ID. Pending state never mutates the already-latched cycle.
class State {
    SRWLOCK lock=SRWLOCK_INIT;
    Snapshot pending{},current{};
    uint64_t serial=0,discontinuities=0,invalidThroughCommand=0;
public:
    void Publish(uint64_t command,const HeadPose& pose,const ui_smoothing::Offset& ui,
                 const float* absolute=nullptr,uint64_t poseGeneration=0) {
        AcquireSRWLockExclusive(&lock);
        pending={command,0,pose,ui};
        if(absolute){memcpy(pending.absolute,absolute,sizeof(pending.absolute));pending.poseGeneration=poseGeneration;}
        ReleaseSRWLockExclusive(&lock);
    }
    Snapshot Latch(){
        AcquireSRWLockExclusive(&lock);
#ifdef FH4VR_SOURCE_DRIVEN_STEREO
        // A native setup advances exactly one eye, regardless of Present cadence.
        // A right eye retains the entire left-eye pose/UI/absolute pose sample.
        ++serial;
        if(serial%2)current=pending;
        current.command=current.serial=serial;
#else
        if(current.command && pending.command!=current.command+1){
            ++discontinuities;
            // The skipped/repeated setup belongs to these commands, not to the
            // later Present which happens to observe the discontinuity counter.
            const uint64_t affected=current.command>pending.command?current.command:pending.command;
            if(affected>invalidThroughCommand)invalidThroughCommand=affected;
        }
        current=pending;current.serial=++serial;current.discontinuities=discontinuities;current.invalidThroughCommand=invalidThroughCommand;
#endif
        const auto result=current;
        ReleaseSRWLockExclusive(&lock);return result;
    }
    Snapshot Read(){AcquireSRWLockShared(&lock);const auto result=current;ReleaseSRWLockShared(&lock);return result;}
    void Stop(){AcquireSRWLockExclusive(&lock);pending={};current={};serial=0;discontinuities=0;invalidThroughCommand=0;ReleaseSRWLockExclusive(&lock);}
};
inline float Eye(const Snapshot& s,uint64_t hold){return s.command && hold?(((s.command-1)/hold)%2?.032f:-.032f):0.f;}
#ifdef FH4VR_RENDER_CYCLE_LATCH
inline State state;
#endif
}
