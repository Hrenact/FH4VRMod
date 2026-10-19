#include "projection_crop.h"
#include <cstdio>
#include <limits>
int main(){
    ProjectionCrop crop{};
    float angle=2.f*std::atan(1.279527f);
    if(!ComputeProjectionCrop(2560.f/1440.f,angle,-1.279527f,1.279527f,-1.279527f,1.279527f,crop))return 1;
    if(std::abs(crop.u0-.21875f)>1e-6 || std::abs(crop.u1-.78125f)>1e-6 || crop.v0>1e-6 || crop.v1<.999999f)return 2;
    if(!ComputeProjectionCrop(2.f,1.5707963268f,-.5f,1.f,-.75f,.5f,crop))return 3;
    if(std::abs(crop.u0-.375f)>1e-6 || std::abs(crop.u1-.75f)>1e-6 || std::abs(crop.v0-.125f)>1e-6 || std::abs(crop.v1-.75f)>1e-6)return 4;
    if(ComputeProjectionCrop(1.f,angle,-2.f,2.f,-1.f,1.f,crop))return 5;
    if(ComputeProjectionCrop(1.f,std::numeric_limits<float>::quiet_NaN(),-1.f,1.f,-1.f,1.f,crop))return 6;
    puts("PASS symmetric/asymmetric projection bounds and insufficient/invalid source rejection");
}
