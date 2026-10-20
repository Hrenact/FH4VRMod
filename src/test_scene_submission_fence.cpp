#include "scene_submission_fence.h"
#include <cassert>
#include <string>
#include <cstdio>
int main(){
    SceneSubmissionFence fence;std::string order;
    auto handoff=[&]{order+='H';};
    auto seal=[&]{order+='S';return S_OK;};
    assert(fence.Finish(false,handoff,seal)==S_OK && order.empty());
    fence.Submitted();order="Submit Present WaitPoses ";
    assert(fence.Finish(true,handoff,seal)==S_OK && order=="Submit Present WaitPoses S");
    assert(fence.Finish(false,handoff,seal)==S_OK && order=="Submit Present WaitPoses S");
    fence.Submitted();order="Submit PresentFailed ";
    assert(fence.Finish(false,handoff,seal)==S_OK && order=="Submit PresentFailed HS");
    fence.Submitted();order="Submit WaitFailed ";
    assert(fence.Finish(false,handoff,seal)==S_OK && order=="Submit WaitFailed HS");
    fence.Submitted();order="Stop ";
    assert(fence.Finish(false,handoff,[&]{order+='S';return E_FAIL;})==E_FAIL && order=="Stop HS");
    order.clear();assert(fence.Finish(false,handoff,seal)==S_OK && order.empty());
    fence.Submitted();assert(fence.Finish(true,handoff,seal)==S_OK && order=="S");
    puts("PASS deferred submission fence: pose wait before seal; stop/failure handoff before seal; exactly once; signal failure propagation; reuse");
}
