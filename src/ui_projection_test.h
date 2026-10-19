#pragma once
#include <initializer_list>
#include <cstring>
#include "camera_yaw_test.h"
#include "ui_smoothing.h"
namespace ui_projection_test {
inline std::atomic<uint64_t> dimensions{0};
inline HoldYawGate gate;
inline bool Shrink(const float* input,float* output,uint32_t width,uint32_t height) {
    if(!width||!height)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(input[i]))return false;
    const float x=2.f/width,y=-2.f/height;
    if(std::abs(input[0]-x)>x*.0001f || std::abs(input[5]-y)>-y*.0001f ||
       std::abs(input[12]+1)>1e-5f || std::abs(input[13]-1)>1e-5f ||
       std::abs(input[14])>1e-5f || std::abs(input[15]-1)>1e-5f)return false;
    for(unsigned i:{1u,2u,3u,4u,6u,7u,8u,9u,11u})if(std::abs(input[i])>1e-6f)return false;
    memcpy(output,input,64);
    // Post-project scale around NDC center. Preserve depth and homogeneous w.
    for(unsigned row=0;row<4;++row){output[row*4]*=.75f;output[row*4+1]*=.75f;}
    return true;
}
// Centered UI plane, 50-degree vertical angular extent at two metres.
// Eye displacement must match the scene's active eye, not a headset model constant.
inline bool Plane(const float* input,float* output,uint32_t width,uint32_t height,
                  float vertical,float eye,ui_smoothing::Offset offset={},bool depthTrial=false) {
    if(!std::isfinite(vertical)||vertical<=0||vertical>=3.f||
       !std::isfinite(eye)||std::abs(eye)>.1f)return false;
    float probe[16];if(!Shrink(input,probe,width,height))return false;
    const float tangent=std::tan(vertical*.5f),aspect=float(width)/height;
    const float scale=std::tan(50.f*3.14159265359f/360.f)/tangent;
    if(!std::isfinite(scale)||scale<=0||scale>1)return false;
    for(float value:offset.basis)if(!std::isfinite(value))return false;
    const float halfHeight=2.f*std::tan(50.f*3.14159265359f/360.f),halfWidth=halfHeight*aspect;
    memcpy(output,input,64);
    for(unsigned row=0;row<4;++row){
        float pos[3]{};
        for(unsigned j=0;j<3;++j)pos[j]=input[row*4]*halfWidth*offset.basis[j]+
            input[row*4+1]*halfHeight*offset.basis[3+j]+input[row*4+3]*2.f*offset.basis[6+j];
        output[row*4]=(pos[0]-eye*input[row*4+3])/(2.f*tangent*aspect);
        output[row*4+1]=pos[1]/(2.f*tangent);
        output[row*4+3]=pos[2]/2.f;
    }
    // Hide a panel crossing/behind the eye plane instead of falling back to the
    // original head-locked UI. Keep the anchor so looking back restores it in place.
    for(unsigned x=0;x<2;++x)for(unsigned y=0;y<2;++y)
        if(x*width*output[3]+y*height*output[7]+output[15]<.1f){
            memset(output,0,64);output[12]=2;output[15]=1;return true;
        }
    // Diagnostic only: preserve small local depth differences inside clip volume.
    if(depthTrial)for(unsigned row=0;row<4;++row)
        output[row*4+2]=.5f*output[row*4+3]+.001f*input[row*4+2];
    return true;
}
}
