#include "capture_cycle_guard.h"
#include <cstdio>
int main(){
    CaptureCycleGuard g;
    if(!g.Accept(2,2,1)||!g.Accept(3,3,1))return 1;
    // Late cycle at left capture rejects the otherwise valid right too.
    if(g.Accept(4,3,2)||g.Accept(5,5,2))return 2;
    if(!g.Accept(6,6,3)||!g.Accept(7,7,3))return 3;
    // A right-side anomaly invalidates the left already queued by the caller.
    if(!g.Accept(8,8,4)||g.Accept(9,8,4))return 4;
    if(!g.Accept(10,10,5)||!g.Accept(11,11,5))return 5;
    if(g.anomalies!=2||g.dropped!=3)return 6;
    g={};if(!g.Accept(2,2,1)||g.anomalies||g.dropped)return 7;
    // Command 4 is the right eye of pair2: do not also discard clean pair3.
    if(g.Accept(4,4,2,1,4)||g.Accept(5,5,2,1,4))return 8;
    if(!g.Accept(6,6,3,1,4)||!g.Accept(7,7,3,1,4))return 9;
    // Command9 belongs to pair5: reject both current pair4 and delayed pair5.
    if(g.Accept(8,8,4,2,9)||g.Accept(9,9,4,2,9)||g.Accept(10,10,5,2,9)||g.Accept(11,11,5,2,9))return 10;
    if(!g.Accept(12,12,6,2,9))return 11;
    // Late observation of an old discontinuity cannot reject a newer clean pair.
    if(!g.Accept(14,14,7,3,10))return 12;
    // A fresh mismatch is still rejected even outside the recovery watermark.
    if(g.Accept(15,14,7,3,10))return 13;
    g={};if(!g.Accept(2,2,1,0,0)||g.recoveryThrough||g.sequenceChanges)return 14;
    puts("PASS capture-cycle guard: normal pairs, left/right anomaly, whole-pair rejection, next-pair recovery, session reset");
}
