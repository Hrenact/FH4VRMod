#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace frame_order_trace {
enum Kind : unsigned { MatrixEnter=1,MatrixExit,PositionEnter,PositionExit,UiEnter,UiExit,
    PresentEnter,PresentExit,Publish,PoseWaitEnter,PoseWaitExit,HandoffEnter,HandoffExit,
    CycleLatch,CameraSnapshot,UiSnapshot,UiPlane,UiPassthrough,CaptureSource,
    RenderMatrixQueued,RenderMatrixConsumed,RenderSourceAtCapture,RenderSourcePose };
struct Event { int64_t qpc;uint64_t command;uintptr_t caller,object;DWORD thread;unsigned kind;uint64_t data0,data1; };
class Recorder {
    static constexpr unsigned Capacity=131072;
    SRWLOCK lock=SRWLOCK_INIT;
    std::atomic<bool> active{false};
    std::atomic<unsigned> count{0},dropped{0};
    Event events[Capacity]{};
    uint64_t until=0;
    bool used=false,saved=true;
public:
    bool Start(uint64_t now) {
        AcquireSRWLockExclusive(&lock);
        const bool allowed=!active.load() && saved;
        if(allowed){count=0;dropped=0;until=now+2000;used=true;saved=false;active=true;}
        ReleaseSRWLockExclusive(&lock);return allowed;
    }
    void Stop(){AcquireSRWLockExclusive(&lock);active=false;ReleaseSRWLockExclusive(&lock);}
    void Poll(uint64_t now){if(active.load() && now>=until)Stop();}
    void Record(Kind kind,uint64_t command,const void* caller=nullptr,const void* object=nullptr,uint64_t data0=0,uint64_t data1=0) {
        if(!active.load())return;
        const DWORD error=GetLastError();
        AcquireSRWLockShared(&lock);
        if(active.load()){
            const unsigned n=count.fetch_add(1);
            if(n<Capacity){LARGE_INTEGER q{};QueryPerformanceCounter(&q);events[n]={q.QuadPart,command,
                reinterpret_cast<uintptr_t>(caller),reinterpret_cast<uintptr_t>(object),GetCurrentThreadId(),kind,data0,data1};}
            else ++dropped;
        }
        ReleaseSRWLockShared(&lock);SetLastError(error);
    }
    // Called on the sole Present thread after VR stops, never during capture.
    HRESULT Save(const wchar_t* path) {
        Stop();if(!used || saved)return S_FALSE;
        FILE* file=nullptr;if(_wfopen_s(&file,path,L"wx") || !file)return E_FAIL;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        fprintf(file,"# pid=%lu base=%p qpc_frequency=%lld dropped=%u\n",GetCurrentProcessId(),GetModuleHandleW(nullptr),frequency.QuadPart,dropped.load());
        fputs("index,qpc,command,thread,kind,caller,object,data0,data1\n",file);
        const unsigned size=count.load()<Capacity?count.load():Capacity;
        for(unsigned i=0;i<size;++i){const auto& e=events[i];fprintf(file,"%u,%lld,%llu,%lu,%u,%llx,%llx,%llu,%llu\n",i,e.qpc,e.command,e.thread,e.kind,
            static_cast<unsigned long long>(e.caller),static_cast<unsigned long long>(e.object),e.data0,e.data1);}
        const bool failed=ferror(file)!=0;const int closed=fclose(file);
        if(failed || closed)return E_FAIL;saved=true;return S_OK;
    }
    bool Active()const{return active.load();}
    unsigned Count()const{return count.load();}
    unsigned Dropped()const{return dropped.load();}
};
// Raw-bit fingerprint for comparing projection inputs/outputs, not a frame ID.
inline uint64_t MatrixFingerprint(const float* matrix){
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<16;++i){uint32_t bits;memcpy(&bits,matrix+i,4);hash^=bits;hash*=1099511628211ull;}
    return hash;
}
#ifdef FH4VR_FRAME_ORDER_TRACE
inline Recorder recorder;
inline void Record(Kind kind,uint64_t command,const void* caller=nullptr,const void* object=nullptr,uint64_t data0=0,uint64_t data1=0){recorder.Record(kind,command,caller,object,data0,data1);}
#else
inline void Record(Kind,uint64_t,const void* =nullptr,const void* =nullptr,uint64_t=0,uint64_t=0){}
#endif
}
