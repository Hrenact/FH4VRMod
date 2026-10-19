#pragma once
#include "head_pose.h"
#include "stereo_trial.h"
namespace stereo_head {
inline std::atomic<const HeadPose*> input{nullptr};
inline HANDLE mapping=nullptr;
inline SRWLOCK lock=SRWLOCK_INIT;
inline HeadPose pairPose{};
inline std::atomic<uint64_t> applied{0},rejected{0};
inline bool Open() {
    mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,PoseMappingName);
    if(!mapping) return false;
    input.store(static_cast<const HeadPose*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(HeadPose))));
    return input.load()!=nullptr; // Mapping stays alive with the pinned proxy.
}
inline void Latch() {
    HeadPose pose{};auto source=input.load();
    if(!source || !CopyHeadPose(source,pose) || !PoseUsable(pose,GetTickCount64())) pose={};
    AcquireSRWLockExclusive(&lock);pairPose=pose;ReleaseSRWLockExclusive(&lock);
}
inline bool Read(HeadPose& pose,bool foreground,ULONGLONG now) {
    if(stereo_trial::Offset(foreground,now)==0) return false;
    AcquireSRWLockShared(&lock);pose=pairPose;ReleaseSRWLockShared(&lock);
    return PoseUsable(pose,now);
}
inline bool Apply(float* view) {
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    HeadPose pose{};
    if(!Read(pose,foreground==GetCurrentProcessId(),GetTickCount64())) return true;
    bool ok=ApplyHeadRotation(view,pose.rotation);
    if(ok) ++applied;else ++rejected;
    return ok;
}
}
