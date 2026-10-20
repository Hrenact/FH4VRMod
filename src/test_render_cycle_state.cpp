#include "render_cycle_state.h"
#include <thread>
#include <atomic>
#include <cstdio>
int main(){
    render_cycle::State state;HeadPose pose{};ui_smoothing::Offset ui{};
    pose.tick=1;ui.x=1;state.Publish(1,pose,ui);
    if(state.Read().command)return 1;
    auto first=state.Latch();if(first.command!=1 || first.serial!=1 || render_cycle::Eye(first,2)!=-.032f)return 2;
    pose.tick=3;ui.x=3;state.Publish(3,pose,ui);
    if(state.Read().command!=1 || state.Read().pose.tick!=1 || state.Read().ui.x!=1)return 3;
    auto next=state.Latch();if(next.command!=3 || next.serial!=2 || render_cycle::Eye(next,2)!=.032f)return 4;
    std::atomic<bool> done{false},failed{false};
    std::thread publisher([&]{for(unsigned i=4;i<40004;++i){HeadPose p{};p.tick=i;ui_smoothing::Offset u{};u.x=static_cast<float>(i);state.Publish(i,p,u);}done=true;});
    std::thread reader([&]{while(!done){auto s=state.Read();if(s.command!=s.pose.tick || s.ui.x!=s.command)failed=true;}});
    for(unsigned i=0;i<40000;++i){auto s=state.Latch();if(s.command!=s.pose.tick || s.ui.x!=s.command)failed=true;}
    publisher.join();reader.join();if(failed)return 5;
    state.Stop();if(state.Read().command || state.Read().pose.valid || state.Latch().command)return 6;
    state.Publish(1,pose,ui);if(state.Read().command)return 7;
    state.Stop();state.Publish(1,pose,ui);if(state.Latch().discontinuities)return 8;
    state.Publish(3,pose,ui);auto skipped=state.Latch();if(skipped.discontinuities!=1||skipped.invalidThroughCommand!=3)return 9;
    auto repeated=state.Latch();if(repeated.discontinuities!=2||repeated.invalidThroughCommand!=3)return 10;
    state.Publish(4,pose,ui);if(state.Latch().discontinuities!=2)return 11;
    state.Stop();if(state.Read().discontinuities||state.Read().invalidThroughCommand)return 12;
    puts("PASS cycle snapshot immutability; coherent command/pose/UI under concurrent publication; stop/start isolation; eye selection");
}
