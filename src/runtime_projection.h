#pragma once
#include "projection_crop.h"
#include <atomic>
#include <cstdint>
namespace runtime_projection {
inline std::atomic<float> verticalRadians{0};
struct Frustum {float left,right,top,bottom;};
struct Plan {float vertical=0;ProjectionCrop crops[2]{};uint32_t width=0,height=0;};
// Choose a centered source covering both asymmetric eye frusta, then preserve
// enough source pixels that each cropped eye meets the runtime recommendation.
inline bool Compute(const Frustum (&eyes)[2],uint32_t recommendedWidth,uint32_t recommendedHeight,
                    float aspect,float scale,Plan& output) {
    if(!recommendedWidth || !recommendedHeight || !std::isfinite(aspect) || aspect<=0 ||
       !std::isfinite(scale) || scale<.25f || scale>2.f)return false;
    float tangent=0;
    for(auto eye:eyes){
        const float values[]={eye.left,eye.right,eye.top,eye.bottom};
        for(float v:values)if(!std::isfinite(v))return false;
        if(eye.left>=0 || eye.right<=0 || eye.top>=0 || eye.bottom<=0)return false;
        tangent=(std::max)(tangent,(std::max)({-eye.top,eye.bottom,-eye.left/aspect,eye.right/aspect}));
    }
    Plan p{};p.vertical=2.f*std::atan(tangent);
    double h=0;
    for(int i=0;i<2;++i){
        auto eye=eyes[i];
        if(!ComputeProjectionCrop(aspect,p.vertical,eye.left,eye.right,eye.top,eye.bottom,p.crops[i]))return false;
        auto crop=p.crops[i];
        h=(std::max)(h,(std::max)(double(recommendedHeight)*scale/(crop.v1-crop.v0),
                            double(recommendedWidth)*scale/(crop.u1-crop.u0)/aspect));
    }
    // Limits here protect integer conversion, not a particular headset resolution.
    if(!std::isfinite(h) || h<1 || h>65532 || h*aspect>65532)return false;
    p.height=static_cast<uint32_t>(std::ceil(h/4))*4;
    p.width=static_cast<uint32_t>(std::ceil(double(p.height)*aspect/4))*4;
    output=p;return true;
}
}

