#include "camera_yaw_test.h"
#include <cstdio>
#include <limits>
#include <initializer_list>
int main() {
    HoldYawGate gate;
    if (gate.angle(true,true,10)!=0) return 1; // held at startup is not armed
    gate.angle(true,false,20);
    const float angle=gate.angle(true,true,30);
    if (angle<0.08f || gate.angle(true,true,5029)==0 || gate.angle(true,true,5030)!=0 || gate.angle(true,true,9000)!=0) return 2;
    gate.angle(true,false,9001);
    if (gate.angle(true,true,9002)==0 || gate.angle(false,true,9003)!=0 || gate.angle(true,true,9004)!=0) return 3;
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,-10,-20,-30,1};
    float matrix[16]; memcpy(matrix,identity,sizeof(matrix));
    if (!ApplyViewYaw(matrix,angle)) return 4;
    // Known +X axis response and camera position mapped to origin.
    if (std::abs(matrix[0]-std::cos(angle))>1e-6f || std::abs(matrix[2]-std::sin(angle))>1e-6f) return 5;
    for(unsigned j=0;j<3;++j) if(std::abs(10*matrix[j]+20*matrix[4+j]+30*matrix[8+j]+matrix[12+j])>1e-5f) return 6;
    if (!ApplyViewYaw(matrix,-angle)) return 7;
    for(unsigned i=0;i<16;++i) if(std::abs(matrix[i]-identity[i])>1e-5f) return 8;
    for(float bad: {2.0f,std::numeric_limits<float>::infinity()}) {
        memcpy(matrix,identity,sizeof(matrix)); matrix[0]=bad;
        float before[16]; memcpy(before,matrix,sizeof(before));
        if(ApplyViewYaw(matrix,angle) || memcmp(before,matrix,sizeof(before))) return 9;
    }
    memcpy(matrix,identity,sizeof(matrix)); matrix[3]=1;
    if(ApplyViewYaw(matrix,angle)) return 10;
    puts("PASS: hold timeout/rearm/focus loss; known yaw, inverse, camera origin, invalid matrix rejection without writes.");
    return 0;
}
