#pragma once
#include <cmath>
#include <algorithm>
struct ProjectionCrop {float u0,v0,u1,v1;};
// Source is a centered, square-pixel rectilinear projection.
inline bool ComputeProjectionCrop(float aspect,float verticalRadians,
    float left,float right,float top,float bottom,ProjectionCrop& output) {
    const float values[]={aspect,verticalRadians,left,right,top,bottom};
    for(float value:values)if(!std::isfinite(value))return false;
    if(aspect<=0 || verticalRadians<=0 || verticalRadians>=3.0f || left>=right || top>=bottom)return false;
    const float ty=std::tan(verticalRadians*.5f),tx=ty*aspect;
    ProjectionCrop crop{.5f+left/(2*tx),.5f+top/(2*ty),.5f+right/(2*tx),.5f+bottom/(2*ty)};
    if(crop.u0<-.00001f || crop.v0<-.00001f || crop.u1>1.00001f || crop.v1>1.00001f)return false;
    crop.u0=std::clamp(crop.u0,0.f,1.f);crop.v0=std::clamp(crop.v0,0.f,1.f);
    crop.u1=std::clamp(crop.u1,0.f,1.f);crop.v1=std::clamp(crop.v1,0.f,1.f);
    output=crop;return true;
}
