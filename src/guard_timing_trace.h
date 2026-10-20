#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
// Sole Present-thread writer. Bounded memory; all disk IO deferred until VR stops.
namespace guard_timing {
template<unsigned Capacity=131072> class Recorder {
    struct Event {int64_t qpc;uint64_t tick,frame,generation,cycle,epoch,invalidThrough;unsigned kind,flags;};
    Event events[Capacity]{};
    unsigned count=0,dropped=0;
public:
    void Record(unsigned kind,unsigned flags,uint64_t frame,uint64_t generation,uint64_t cycle=0,uint64_t epoch=0,uint64_t invalidThrough=0){
        const DWORD saved=GetLastError();
        if(count<Capacity){LARGE_INTEGER q{};QueryPerformanceCounter(&q);events[count++]={q.QuadPart,GetTickCount64(),frame,generation,cycle,epoch,invalidThrough,kind,flags};}
        else ++dropped;
        SetLastError(saved);
    }
    unsigned Count()const{return count;} unsigned Dropped()const{return dropped;}
    HRESULT Save(const wchar_t* path){
        FILE* f=nullptr;if(_wfopen_s(&f,path,L"wx")||!f)return E_FAIL;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        fprintf(f,"# pid=%lu qpc_frequency=%lld dropped=%u\n",GetCurrentProcessId(),frequency.QuadPart,dropped);
        fputs("qpc,tick,kind,flags,frame,generation,cycle,epoch,invalid_through\n",f);
        for(unsigned i=0;i<count;++i){const auto&e=events[i];fprintf(f,"%lld,%llu,%u,%u,%llu,%llu,%llu,%llu,%llu\n",e.qpc,e.tick,e.kind,e.flags,e.frame,e.generation,e.cycle,e.epoch,e.invalidThrough);}
        bool failed=ferror(f)!=0;int closed=fclose(f);return failed||closed?E_FAIL:S_OK;
    }
};
// kind1=guard decision; flags bit0 accepted, bit1 mismatch, bit2 new sequence
// epoch, bit3 blocked generation, bit4 recovery range. kind2=copy failure/skip
// (flags HRESULT bits), kind3=Submit returned (flags HRESULT bits).
}
