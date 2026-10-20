#pragma once
#include <cstdint>
// Experimental rejection of an observed ordering anomaly, NOT a pixel/frame ID.
// Only used with hold1/lag1. Reject both members of the scheduled pair if either
// capture sees a cycle other than the current Present command. No waits/retries.
struct CaptureCycleGuard {
    uint64_t blockedGeneration=0,anomalies=0,dropped=0,lastDiscontinuities=0,recoveryThrough=0,sequenceChanges=0;
    bool Accept(uint64_t present,uint64_t cycle,uint64_t generation,uint64_t discontinuities=0,uint64_t invalidThroughCommand=0){
        if(!present || !generation)return false;
        if(discontinuities!=lastDiscontinuities){
            lastDiscontinuities=discontinuities;++sequenceChanges;
            // Whole-pair boundary containing the last affected command (hold1).
            // No extra pair merely because the render thread noticed it late.
            const uint64_t affectedPair=invalidThroughCommand/2+(invalidThroughCommand%2);
            if(affectedPair>recoveryThrough)recoveryThrough=affectedPair;
        }
        if(cycle!=present){blockedGeneration=generation;++anomalies;}
        if(blockedGeneration==generation || generation<=recoveryThrough){++dropped;return false;}
        return true;
    }
};
