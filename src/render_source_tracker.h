#pragma once
#include "render_cycle_state.h"
namespace render_source {
enum Status : unsigned { Missing=0, Matched=1, Ambiguous=2, WrongThread=3 };
struct Stamp {
    render_cycle::Snapshot source{};
    uintptr_t target=0;
    DWORD thread=0;
    Status status=Missing;
};
// Diagnostic only. Exact bytes, bounded storage, no nearest-frame fallback.
template<unsigned Capacity=64> class Tracker {
    struct Entry { float matrix[16]{};render_cycle::Snapshot source{};uintptr_t target=0;bool used=false; };
    SRWLOCK lock=SRWLOCK_INIT;
    Entry entries[Capacity]{};
    Stamp latest{};
    uint64_t epoch=0,overflows=0;
public:
    uint64_t Reset(){
        AcquireSRWLockExclusive(&lock);
        ++epoch;for(auto& entry:entries)entry.used=false;latest={};overflows=0;
        const auto result=epoch;ReleaseSRWLockExclusive(&lock);return result;
    }
    uint64_t Epoch(){
        AcquireSRWLockShared(&lock);const auto result=epoch;ReleaseSRWLockShared(&lock);return result;
    }
    bool Queue(uint64_t token,uintptr_t target,const float* matrix,const render_cycle::Snapshot& source){
        AcquireSRWLockExclusive(&lock);
        if(token!=epoch || overflows || !target || !source.command || !source.serial){
            ReleaseSRWLockExclusive(&lock);return false;
        }
        Entry* free=nullptr;
        for(auto& entry:entries)if(!entry.used){free=&entry;break;}
        if(!free){
            ++overflows;for(auto& entry:entries)entry.used=false;
            latest={};ReleaseSRWLockExclusive(&lock);return false;
        }
        memcpy(free->matrix,matrix,64);free->source=source;free->target=target;free->used=true;
        ReleaseSRWLockExclusive(&lock);return true;
    }
    Stamp Consume(uint64_t token,uintptr_t target,const float* matrix,DWORD thread){
        AcquireSRWLockExclusive(&lock);
        if(token!=epoch){ReleaseSRWLockExclusive(&lock);return {};}
        Stamp stamp{};stamp.target=target;stamp.thread=thread;
        unsigned matches=0;
        for(auto& entry:entries)if(!overflows && matrix && entry.used && entry.target==target && !memcmp(entry.matrix,matrix,64)){
            ++matches;stamp.source=entry.source;entry.used=false;
        }
        stamp.status=matches==1?Matched:matches?Ambiguous:Missing;
        if(stamp.status!=Matched)stamp.source={};
        latest=stamp;ReleaseSRWLockExclusive(&lock);return stamp;
    }
    Stamp Read(DWORD thread){
        AcquireSRWLockShared(&lock);auto result=latest;ReleaseSRWLockShared(&lock);
        if(result.thread!=thread){result.source={};result.status=WrongThread;}
        return result;
    }
    uint64_t Overflows(){
        AcquireSRWLockShared(&lock);auto result=overflows;ReleaseSRWLockShared(&lock);return result;
    }
};
inline Tracker<> tracker;
}
