#pragma once
#include "camera_yaw_test.h"
// Offsets are in game-world units; physical scale has not yet been calibrated.
inline bool ApplyEyeOffset(float* matrix,float eyeX) {
    if(!std::isfinite(eyeX) || std::abs(eyeX)>0.04f) return false;
    float copy[16];memcpy(copy,matrix,sizeof(copy));
    if(!ApplyViewYaw(copy,0)) return false;
    // V_eye = V_center * T(-eyeX,0,0), for an affine row-vector view matrix.
    copy[12]-=eyeX;
    memcpy(matrix,copy,sizeof(copy));return true;
}
struct EyeOffsetGate {
    HoldYawGate gate;
    float offset(bool foreground,bool left,bool right,uint64_t tick) {
        if(left && right) {gate.angle(false,false,tick);return 0;}
        return gate.angle(foreground,left || right,tick)!=0 ? (left?-0.032f:0.032f):0;
    }
};

inline bool ApplyEyePosition(double* position,const float* view,float eyeX) {
    if(!std::isfinite(eyeX) || std::abs(eyeX)>0.04f) return false;
    float checked[16];memcpy(checked,view,sizeof(checked));
    if(!ApplyViewYaw(checked,0)) return false;
    double copy[4];memcpy(copy,position,sizeof(copy));
    for(double v:copy) if(!std::isfinite(v)) return false;
    // The first view column is the camera's world-space right axis.
    for(unsigned i=0;i<3;++i) copy[i]+=static_cast<double>(eyeX)*checked[i*4];
    memcpy(position,copy,sizeof(copy));return true;
}
