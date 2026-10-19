#define NOMINMAX
#include "head_pose.h"
#include "camera_yaw_test.h"
#include <cstdio>
#include <thread>
#include <atomic>
int main() {
    const float center[9]={1,0,0,0,1,0,0,0,1};
    const float a=.0872664626f,c=std::cos(a),s=std::sin(a);
    // Physical right turn in OpenVR must match the visually confirmed right-yaw test.
    const float right[9]={c,0,-s,0,1,0,s,0,c}; float r[9]; RelativeViewRotation(center,right,r);
    float view[16]={1,0,0,0,0,1,0,0,0,0,1,0,-10,-20,-30,1},expected[16];
    memcpy(expected,view,sizeof(view)); ApplyViewYaw(expected,a);
    if(!ApplyHeadRotation(view,r)) return 1;
    for(unsigned i=0;i<16;++i) if(std::abs(view[i]-expected[i])>1e-5f) return 2;
    RelativeViewRotation(right,right,r);
    for(unsigned i=0;i<9;++i) if(std::abs(r[i]-center[i])>1e-5f) return 3;
    const float up[9]={1,0,0,0,c,-s,0,s,c}; RelativeViewRotation(center,up,r);
    if(std::abs(r[5]-s)>1e-5f || std::abs(r[7]+s)>1e-5f) return 4;
    const float roll[9]={c,-s,0,s,c,0,0,0,1}; RelativeViewRotation(center,roll,r);
    if(std::abs(r[1]+s)>1e-5f || std::abs(r[3]-s)>1e-5f) return 5;
    HeadPose p{}; p.magic=PoseMagic;p.version=1;p.enabled=1;p.valid=1;p.tick=1000; memcpy(p.rotation,center,sizeof(center));
    if(!PoseUsable(p,1250) || PoseUsable(p,1251) || PoseUsable(p,999)) return 6;
    p.enabled=0; if(PoseUsable(p,1000)) return 7; p.enabled=1;
    p.rotation[0]=-1; if(PoseUsable(p,1000)) return 8;
    // Process-shared Windows seqlock: readers must never accept a mixed packet.
    HeadPose shared{};std::atomic<bool> done{false}; unsigned coherent=0;
    std::thread writer([&] { for(unsigned i=1;i<=100000;++i) {
        InterlockedIncrement(&shared.sequence);shared.tick=i;shared.producerPid=i;
        InterlockedIncrement(&shared.sequence);
    } done=true; });
    do { HeadPose copy{}; if(CopyHeadPose(&shared,copy)) { if(copy.tick!=copy.producerPid) { writer.join();return 9; } ++coherent; } } while(!done);
    writer.join(); if(!coherent) return 10;
    puts("PASS: right/up/roll basis, recenter, stale/future/disabled/reflection rejection, coherent snapshots."); return 0;
}
