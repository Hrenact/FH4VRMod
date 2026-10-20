#pragma once
#include <windows.h>
#include <utility>

// A submitted texture cannot be recycled until runtime queue work is handed off
// and then covered by our fence. Normal completion uses the next WaitGetPoses;
// stopping or a failed pose wait uses explicit handoff before sealing instead.
class SceneSubmissionFence {
    bool pending=false;
public:
    void Submitted(){pending=true;}
    template<class Handoff,class Seal>
    HRESULT Finish(bool poseWaitSucceeded,Handoff&& handoff,Seal&& seal){
        if(!pending)return S_OK;
        if(!poseWaitSucceeded)std::forward<Handoff>(handoff)();
        const HRESULT hr=std::forward<Seal>(seal)();
        pending=false; // The ring retains/poisons failed signals itself.
        return hr;
    }
};
