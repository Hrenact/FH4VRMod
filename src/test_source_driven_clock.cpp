#define FH4VR_SOURCE_DRIVEN_STEREO
#include "render_cycle_state.h"
#include <cassert>
#include <cstdio>
int main(){
    render_cycle::State clock;HeadPose pose{};ui_smoothing::Offset ui{};
    float absolute[12]{};absolute[0]=absolute[5]=absolute[10]=1;
    pose.tick=100;absolute[3]=10;
    clock.Publish(1,pose,ui,absolute,1);
    auto left=clock.Latch();
    assert(left.command==1 && left.serial==1 && left.poseGeneration==1);
    // Present jumps and changes its pose. Right eye must retain the left sample.
    pose.tick=200;absolute[3]=20;clock.Publish(4,pose,ui,absolute,2);
    auto right=clock.Latch();
    assert(right.command==2 && right.serial==2 && right.poseGeneration==1);
    assert(right.pose.tick==100 && right.absolute[3]==10);
    // Even without a new Present, a new native setup advances to the next eye.
    left=clock.Latch();right=clock.Latch();
    assert(left.command==3 && right.command==4 && left.poseGeneration==2 && right.poseGeneration==2);
    assert(left.pose.tick==200 && right.pose.tick==200 && right.absolute[3]==20);
    assert(render_cycle::Eye(left,1)<0 && render_cycle::Eye(right,1)>0);
    clock.Stop();pose.tick=300;clock.Publish(99,pose,ui,absolute,50);
    left=clock.Latch();assert(left.command==1 && left.poseGeneration==50 && left.pose.tick==300);
    puts("PASS native setup cadence despite skipped/repeated Present; shared relative/absolute pose; reset");
}
