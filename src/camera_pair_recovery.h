#pragma once
#include <cstdint>
// Invalid camera output poisons the whole eye pair, not the VR toggle.
struct CameraPairRecovery {
    uint64_t camera=0,head=0,dropThrough=0,since=0;
    bool pending=false;
    bool Observe(uint64_t frame,uint64_t cameraRejected,uint64_t headRejected,uint64_t now,uint64_t pairFrames=4){
        if(cameraRejected==camera && headRejected==head)return false;
        camera=cameraRejected;head=headRejected;
        dropThrough=frame?((frame-1)/pairFrames+1)*pairFrames:pairFrames;
        if(!pending){pending=true;since=now;}
        return true;
    }
    bool Drop(uint64_t frame)const{return frame && frame<=dropThrough;}
    bool Expired(uint64_t now)const{return pending && now>=since && now-since>=2000;}
    void Submitted(){pending=false;}
};
