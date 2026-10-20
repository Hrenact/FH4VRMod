#pragma once
#include <cstdint>
// One compositor wait per successful submission, plus the initial wait.
// Dropped pairs sample a fresh system pose without reopening a compositor frame.
struct PoseWaitPolicy {
    bool initialized=false;
    uint64_t submittedAtWait=0;
    bool NeedWait(uint64_t submitted)const{return !initialized || submitted!=submittedAtWait;}
    void WaitSucceeded(uint64_t submitted){initialized=true;submittedAtWait=submitted;}
};
