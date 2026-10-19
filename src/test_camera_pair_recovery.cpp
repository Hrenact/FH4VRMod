#include "camera_pair_recovery.h"
#include <cassert>
#include <cstdio>
int main(){
    for(uint64_t frame=1;frame<=12;++frame){
        CameraPairRecovery r;assert(r.Observe(frame,1,0,100));
        uint64_t boundary=((frame-1)/4+1)*4;
        for(auto n=frame;n<=boundary;++n)assert(r.Drop(n));
        assert(!r.Drop(boundary+1));assert(!r.Observe(boundary+1,1,0,110));
        assert(!r.Expired(2099));assert(r.Expired(2100));
        r.Submitted();assert(!r.Expired(9999));
        assert(r.Observe(boundary+2,1,1,10000));assert(!r.Expired(10001));
    }
    CameraPairRecovery r;assert(r.Observe(7,13,0,10,6)&&r.dropThrough==12);
    assert(r.Observe(8,14,0,20,6)&&r.since==10); // Repeated rejects cannot postpone failure indefinitely.
    puts("PASS invalid-pair discard at all eye phases, recovery, sustained failure timeout");
}
