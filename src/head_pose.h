#pragma once
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstddef>
constexpr wchar_t PoseMappingName[]=L"Local\\FH4VR_HeadPose_v1";
constexpr DWORD PoseMagic=0x31524846;
struct HeadPose {
    DWORD magic,version;
    volatile LONG sequence;
    DWORD producerPid;
    ULONGLONG tick;
    DWORD enabled,valid;
    float rotation[9]; // row-vector camera-space view rotation, no translation
    float yaw,pitch,roll;
};
static_assert(sizeof(HeadPose)==80);
static_assert(offsetof(HeadPose,rotation)==32);
inline bool RotationValid(const float* r) {
    for(unsigned i=0;i<9;++i) if(!std::isfinite(r[i])) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        float dot=0;
        for(unsigned k=0;k<3;++k) dot+=r[k*3+i]*r[k*3+j];
        if(std::abs(dot-(i==j?1.f:0.f))>0.01f) return false;
    }
    float det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    return std::abs(det-1)<0.01f;
}
inline void RelativeViewRotation(const float* center,const float* current,float* output) {
    // OpenVR column-vector RH pose: relative = transpose(center) * current.
    // Row-vector view inverse cancels the transpose. Reflect Z into game basis.
    constexpr float sign[3]={1,1,-1};
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        float value=0;
        for(unsigned k=0;k<3;++k) value+=center[k*3+i]*current[k*3+j];
        output[i*3+j]=sign[i]*sign[j]*value;
    }
}
// Fixed recenter policy shared by scene and UI: heading only, tracking-space Y up.
inline bool HeadingOnlyCenter(const float* current,float* center) {
    if(!RotationValid(current))return false;
    const float x=current[2],z=current[8],horizontal=std::sqrt(x*x+z*z);
    if(horizontal<.001f)return false;
    const float rx=z/horizontal,rz=-x/horizontal;
    const float leveled[9]={rx,0,-rz,0,1,0,rz,0,rx};
    memcpy(center,leveled,sizeof(leveled));return true;
}
inline bool PoseUsable(const HeadPose& pose,ULONGLONG now) {
    return pose.magic==PoseMagic && pose.version==1 && pose.enabled==1 && pose.valid==1 &&
        pose.tick<=now && now-pose.tick<=250 && RotationValid(pose.rotation);
}
inline bool CopyHeadPose(const HeadPose* source,HeadPose& copy) {
    for(unsigned attempt=0;attempt<3;++attempt) {
        LONG before=source->sequence;
        MemoryBarrier();
        if(before&1) continue;
        memcpy(&copy,source,sizeof(copy));
        MemoryBarrier();
        if(before==source->sequence && copy.sequence==before) return true;
    }
    return false;
}
inline bool ApplyHeadRotation(float* matrix,const float* rotation) {
    if(!RotationValid(rotation)) return false;
    float m[16],r[9]; memcpy(m,matrix,sizeof(m));
    for(float value:m) if(!std::isfinite(value)) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) r[i*3+j]=m[i*4+j];
    if(!RotationValid(r) || std::abs(m[3])>0.001f || std::abs(m[7])>0.001f ||
       std::abs(m[11])>0.001f || std::abs(m[15]-1)>0.001f) return false;
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<3;++j) {
        float value=0;
        for(unsigned k=0;k<3;++k) value+=m[i*4+k]*rotation[k*3+j];
        matrix[i*4+j]=value;
    }
    return true;
}
