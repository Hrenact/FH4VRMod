#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include "head_pose.h"
namespace ui_smoothing {
struct Offset {
    float x=0,y=0;
    float basis[9]={1,0,0,0,1,0,0,0,1}; // row-vector panel to camera, +Z forward
};
struct Filter {
    bool ready=false;float forward[3]{},upReference[3]{};
    Offset Update(const float* rotation,uint64_t /*now*/,bool reset=false) {
        for(unsigned i=0;i<9;++i)if(!std::isfinite(rotation[i]))return {};
        // Anchor changes only at activation or explicit yaw-only recenter.
        // Turning away, waiting, or a frame gap must never drag it toward the head.
        const bool snap=reset||!ready;
        if(snap){
            float heading[9];
            if(!HeadingOnlyCenter(rotation,heading)){ready=false;return {};}
            for(unsigned i=0;i<3;++i){forward[i]=-heading[i*3+2];upReference[i]=i==1?1.f:0.f;}
        }
        float norm=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
        if(norm<.001f){ready=false;return {};}
        for(float& v:forward)v/=norm;
        ready=true;
        float local[3]{};
        for(unsigned j=0;j<3;++j)for(unsigned i=0;i<3;++i)local[j]+=rotation[i*3+j]*forward[i];
        float right[3]={forward[1]*upReference[2]-forward[2]*upReference[1],
            forward[2]*upReference[0]-forward[0]*upReference[2],forward[0]*upReference[1]-forward[1]*upReference[0]};
        float rn=std::sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
        if(rn<.1f){ready=false;return {};}
        for(float& v:right)v/=rn;
        float up[3]={right[1]*forward[2]-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]};
        Offset o{};
        if(std::abs(local[2])>.001f){o.x=local[0]/-local[2];o.y=local[1]/-local[2];}
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){
            const float* axis=i==0?right:i==1?up:forward;float value=0;
            for(unsigned k=0;k<3;++k)value+=axis[k]*rotation[k*3+j];
            o.basis[i*3+j]=j==2?-value:value;
        }
        return o;
    }
};
inline SRWLOCK lock=SRWLOCK_INIT;
inline Offset paired{};
inline void Publish(Offset value){AcquireSRWLockExclusive(&lock);paired=value;ReleaseSRWLockExclusive(&lock);}
inline Offset Read(){AcquireSRWLockShared(&lock);auto value=paired;ReleaseSRWLockShared(&lock);return value;}
}
