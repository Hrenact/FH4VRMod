#pragma once
#include <openvr.h>
#include "head_pose.h"
// One sample owns BOTH the relative game rotation and absolute compositor tag.
// Only the Present thread mutates this; camera callbacks receive a locked copy.
struct ScenePoseState {
    float center[9]{};
    bool centered=false,valid=false;
    vr::HmdMatrix34_t absolute{};
    HeadPose game{};
    uint64_t generation=0;
    bool Begin(const vr::TrackedDevicePose_t& pose,bool recenter,ULONGLONG now) {
        valid=false;game={};
        if(!pose.bDeviceIsConnected || !pose.bPoseIsValid || pose.eTrackingResult!=vr::TrackingResult_Running_OK)return false;
        float current[9];
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j){
            float value=pose.mDeviceToAbsoluteTracking.m[i][j];
            if(!std::isfinite(value))return false;
            if(j<3)current[i*3+j]=value;
        }
        if(!RotationValid(current))return false;
        if(!centered || recenter){
            // Calibrate heading only. Tracking-space up remains vertical, so
            // pitch and roll are tracked but never baked into the center.
            if(!HeadingOnlyCenter(current,center))return false;
            centered=true;
        }
        HeadPose next{};next.magic=PoseMagic;next.version=1;next.enabled=next.valid=1;next.tick=now;
        RelativeViewRotation(center,current,next.rotation);
        if(!RotationValid(next.rotation))return false;
        absolute=pose.mDeviceToAbsoluteTracking;game=next;valid=true;++generation;return true;
    }
    bool Usable(ULONGLONG now) const {return valid && PoseUsable(game,now);}
    bool DropIfStale(ULONGLONG now) {
        if(Usable(now))return false;
        valid=false;return true;
    }
};
