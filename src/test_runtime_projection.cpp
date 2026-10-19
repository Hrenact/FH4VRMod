#include "runtime_projection.h"
#include <cstdio>
#include <limits>
int main(){
    using namespace runtime_projection;
    Frustum eyes[2]={{-1,1,-1,1},{-1,1,-1,1}};Plan p{};
    if(!Compute(eyes,2000,2000,1,1,p) || p.width!=2000 || p.height!=2000)return 1;
    if(!Compute(eyes,2000,2000,16.f/9,1,p) || p.height!=2000 || p.width<3555)return 2;
    if(!Compute(eyes,2000,2000,1,.75f,p) || p.width!=1500)return 3;
    eyes[0]={-1.3f,.9f,-1.1f,1.2f};eyes[1]={-.8f,1.4f,-1.f,1.3f};
    for(float aspect:{1.f,16.f/9, .8f}){
        if(!Compute(eyes,2300,2500,aspect,1,p))return 4;
        for(auto c:p.crops)if(c.u0<0 || c.v0<0 || c.u1>1 || c.v1>1 ||
            (c.u1-c.u0)*p.width<2299.9f || (c.v1-c.v0)*p.height<2499.9f)return 5;
    }
    if(Compute(eyes,0,2500,1,1,p) || Compute(eyes,2000,2000,0,1,p) || Compute(eyes,2000,2000,1,3,p))return 6;
    eyes[0].left=std::numeric_limits<float>::quiet_NaN();if(Compute(eyes,2000,2000,1,1,p))return 7;
    puts("PASS runtime projection square/rectangular/asymmetric coverage, scale, output density and invalid input");
}
