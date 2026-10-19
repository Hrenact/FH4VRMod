#include "scene_pose.h"
#include <cstdio>
#include <limits>
static vr::TrackedDevicePose_t Pose(float yaw) {
    vr::TrackedDevicePose_t p{};p.bDeviceIsConnected=p.bPoseIsValid=true;p.eTrackingResult=vr::TrackingResult_Running_OK;
    auto& m=p.mDeviceToAbsoluteTracking.m;
    m[0][0]=m[2][2]=std::cos(yaw);m[0][2]=std::sin(yaw);m[2][0]=-std::sin(yaw);m[1][1]=1;
    m[0][3]=.2f;m[1][3]=1.6f;m[2][3]=-.3f;return p;
}
int main(){
    ScenePoseState s{};auto first=Pose(.3f);
    if(s.Usable(100) || !s.Begin(first,false,100) || !s.Usable(100))return 1;
    const float identity[9]={1,0,0,0,1,0,0,0,1};
    for(int i=0;i<9;++i)if(std::abs(s.game.rotation[i]-identity[i])>.00001f)return 2;
    auto next=Pose(.5f);if(!s.Begin(next,false,120))return 3;
    if(std::abs(s.game.rotation[2]+std::sin(.2f))>.00001f || std::abs(s.game.rotation[6]-std::sin(.2f))>.00001f)return 4;
    if(memcmp(&s.absolute,&next.mDeviceToAbsoluteTracking,sizeof(s.absolute)))return 5;
    // Later samples must not mutate an already-latched pair.
    next=Pose(-.8f);if(std::abs(s.absolute.m[0][2]-std::sin(.5f))>.00001f)return 6;
    if(s.Usable(119) || !s.Usable(370) || s.Usable(371))return 7;
    if(s.DropIfStale(370)||!s.DropIfStale(371)||s.valid)return 22;
    if(!s.DropIfStale(372)||!s.Begin(Pose(.6f),false,380)||s.DropIfStale(380))return 23;
    if(!s.Begin(next,true,400))return 8;
    for(int i=0;i<9;++i)if(std::abs(s.game.rotation[i]-identity[i])>.00001f)return 9;
    next.bPoseIsValid=false;if(s.Begin(next,false,410) || s.Usable(410))return 10;
    next=Pose(0);next.mDeviceToAbsoluteTracking.m[0][3]=std::numeric_limits<float>::quiet_NaN();
    if(s.Begin(next,false,420))return 11;
    next=Pose(0);next.mDeviceToAbsoluteTracking.m[0][0]=2;if(s.Begin(next,false,430))return 12;
    next=Pose(0);next.eTrackingResult=vr::TrackingResult_Running_OutOfRange;if(s.Begin(next,false,440))return 13;
    // R_y(yaw) R_x(pitch) R_z(roll): center ignores both pitch and roll.
    auto tilted=Pose(.4f);const float pitch=.2f,roll=.3f;
    float base[9],cur[9];
    const float cy=std::cos(.4f),sy=std::sin(.4f),cp=std::cos(pitch),sp=std::sin(pitch);
    const float level[9]={cy,sy*sp,sy*cp, 0,cp,-sp, -sy,cy*sp,cy*cp};
    memcpy(base,level,sizeof(base));
    for(int row=0;row<3;++row){
        cur[row*3]=base[row*3]*std::cos(roll)+base[row*3+1]*std::sin(roll);
        cur[row*3+1]=-base[row*3]*std::sin(roll)+base[row*3+1]*std::cos(roll);
        cur[row*3+2]=base[row*3+2];
        for(int col=0;col<3;++col)tilted.mDeviceToAbsoluteTracking.m[row][col]=cur[row*3+col];
    }
    ScenePoseState upright{};
    if(!upright.Begin(tilted,false,500))return 14;
    const float heading[9]={cy,0,sy, 0,1,0, -sy,0,cy};
    for(int i=0;i<9;++i)if(std::abs(upright.center[i]-heading[i])>.00001f)return 15;
    if(std::abs(upright.game.rotation[1]+std::sin(roll))>.00001f)return 16;
    if(std::abs(upright.game.rotation[5]-sp)>.00001f)return 21; // Pitch still tracked.
    if(!upright.Begin(tilted,true,510))return 17;
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)tilted.mDeviceToAbsoluteTracking.m[row][col]=heading[row*3+col];
    if(!upright.Begin(tilted,false,520))return 18;
    for(int i=0;i<9;++i)if(std::abs(upright.game.rotation[i]-identity[i])>.00001f)return 19;
    if(memcmp(&upright.absolute,&tilted.mDeviceToAbsoluteTracking,sizeof(upright.absolute)))return 20;
    puts("PASS scene paired pose, yaw-only recenter, pitch/roll tracking retained, upright restoration, age and invalid rejection");
}
