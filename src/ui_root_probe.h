#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <new>

// Read-only UserInterface::slot28 scope. A scope is evidence of synchronous
// callbacks, not permission to classify every UIRenderer draw as screen UI.
namespace ui_root_probe {
using Root = void(*)(void*,void*);
inline constexpr uintptr_t TableRva=0x5b9b248, FunctionRva=0x3ab9c90;
inline constexpr unsigned SlotIndex=28;
inline constexpr unsigned char Prefix[]={0x48,0x8b,0xc4,0x55,0x56,0x57,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xd0,0,0,0};
inline std::atomic<Root> original{nullptr};
inline std::atomic<HMODULE> owner{nullptr};
inline void* volatile* slot=nullptr; // Start/Stop are serialized by installationLock.
inline SRWLOCK installationLock=SRWLOCK_INIT, logLock=SRWLOCK_INIT;
inline std::atomic<uint64_t> roots{0},returns{0},unwinds{0},activeRoots{0},lastLogTick{0};
inline std::atomic<unsigned> maximumDepth{0};
inline constexpr const char* EventNames[]={"setter","prepare","text52","text54","begin","end","layer_end","draw","other"};
inline constexpr unsigned EventCount=sizeof(EventNames)/sizeof(EventNames[0]);
struct EventTotals {
    std::atomic<uint64_t> total{0},inScope{0},matched{0},different{0},outside{0},changed{0},outsideWhileRootActive{0};
};
inline EventTotals totals[EventCount];
struct Counts {uint64_t count=0,matched=0,different=0,changed=0;void* firstCaller=nullptr;void* lastCaller=nullptr;};

struct LastErrorGuard {
    DWORD value=GetLastError();
    ~LastErrorGuard(){SetLastError(value);}
};
struct ExclusiveLock {
    SRWLOCK* value;
    explicit ExclusiveLock(SRWLOCK& lock):value(&lock){AcquireSRWLockExclusive(value);}
    ~ExclusiveLock(){ReleaseSRWLockExclusive(value);}
};
template<class T> bool Read(uintptr_t address,T& result) {
    SIZE_T got=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&result,sizeof(result),&got)&&got==sizeof(result);
}
template<class T> bool Member(uintptr_t object,size_t offset,T& result) {
    return object && object<=UINTPTR_MAX-offset && Read(object+offset,result);
}
struct Snapshot {
    uintptr_t uiVtable=0,renderer=0,rendererVtable=0,application=0,applicationVtable=0;
    uintptr_t manager=0,managerVtable=0,manager14=0,manager15=0;
    uintptr_t rendererContext=0,commandList=0,backend=0,defaultTarget=0,target=0,layer=0;
    uintptr_t resource=0,clipData=0;unsigned dimensions[2]{},clipCapacity=0,clipCount=0;
    float projection[16]{},viewport[4]{};unsigned valid=0;
};
inline Snapshot Capture(void* object) {
    LastErrorGuard error;Snapshot s;auto ui=reinterpret_cast<uintptr_t>(object);
    if(Read(ui,s.uiVtable))s.valid|=1;
    if(Member(ui,0xd0,s.renderer))s.valid|=2;
    Member(ui,0xd8,s.application);Read(s.renderer,s.rendererVtable);Read(s.application,s.applicationVtable);
    Member(s.application,0x48,s.manager);Read(s.manager,s.managerVtable);
    Member(s.managerVtable,14*8,s.manager14);Member(s.managerVtable,15*8,s.manager15);
    if(Member(s.renderer,0x40,s.rendererContext))s.valid|=4;
    Member(s.renderer,0x48,s.commandList);Member(s.renderer,0x68,s.backend);
    Member(s.backend,0x10,s.defaultTarget);Member(s.renderer,0x70,s.target);Member(s.renderer,0x650,s.layer);
    Read(s.target,s.resource);Member(s.target,0x80,s.dimensions);
    Member(s.renderer,0x270,s.clipData);Member(s.renderer,0x278,s.clipCapacity);Member(s.renderer,0x27c,s.clipCount);
    if(Member(s.renderer,0x360,s.projection))s.valid|=8;
    if(Member(s.renderer,0x78,s.viewport))s.valid|=16;
    return s;
}
inline bool Sample(uint64_t id) {
    if(!owner.load(std::memory_order_relaxed))return false;
    if(id<=8){lastLogTick.store(GetTickCount64(),std::memory_order_relaxed);return true;}
    auto now=GetTickCount64(),previous=lastLogTick.load(std::memory_order_relaxed);
    return now-previous>=1000 && lastLogTick.compare_exchange_strong(previous,now,std::memory_order_relaxed);
}
struct Frame;
inline thread_local Frame* current=nullptr;
inline constexpr unsigned SamplesPerEvent=4;
inline constexpr unsigned MatrixOffsets[]={0x360,0x2a0,0x2e0,0x320,0x3a0,0x3e0,0x430,0x470,0x4e0,0x520,0x5b0};
inline constexpr const char* MatrixNames[]={"P360","A2a0","B2e0","L320","D3a0","combined3e0","D0430","clip470","M04e0","M1520","draw5b0"};
inline constexpr unsigned MatrixCount=sizeof(MatrixOffsets)/sizeof(MatrixOffsets[0]);
static_assert(MatrixCount==sizeof(MatrixNames)/sizeof(MatrixNames[0]),"Every sampled matrix needs an exact offset label");
struct RenderSample {
    void* renderer=nullptr;void* caller=nullptr;uintptr_t target=0,layer=0,context=0,commandList=0,resource=0;
    unsigned dimensions[2]{},clipCapacity=0,clipCount=0;uintptr_t clipData=0;
    unsigned char clipTop[20]{};float matrices[MatrixCount][16]{};bool valid=false,clipValid=false,changed=false;
};
struct CallbackSamples {RenderSample values[EventCount][SamplesPerEvent]{};unsigned counts[EventCount]{};};
inline RenderSample CaptureRenderer(void* object,void* caller,bool changed) {
    LastErrorGuard error;RenderSample s;s.renderer=object;s.caller=caller;s.changed=changed;
    unsigned char data[0x620]{};auto r=reinterpret_cast<uintptr_t>(object);
    if(!Read(r,data))return s;s.valid=true;
    memcpy(&s.context,data+0x40,8);memcpy(&s.commandList,data+0x48,8);memcpy(&s.target,data+0x70,8);
    memcpy(&s.clipData,data+0x270,8);memcpy(&s.clipCapacity,data+0x278,4);memcpy(&s.clipCount,data+0x27c,4);
    for(unsigned i=0;i<MatrixCount;++i)memcpy(s.matrices[i],data+MatrixOffsets[i],64);
    Member(r,0x650,s.layer);Read(s.target,s.resource);Member(s.target,0x80,s.dimensions);
    if(s.clipData && s.clipCount && s.clipCount<=s.clipCapacity && s.clipCount<=4096 &&
       s.clipData<=UINTPTR_MAX-(s.clipCount-1)*20)
        s.clipValid=Read(s.clipData+(s.clipCount-1)*20,s.clipTop);
    return s;
}
struct Frame {
    Frame* parent=nullptr;uint64_t id=0;unsigned depth=0;void* ui=nullptr;void* context=nullptr;
    void* expectedRenderer=nullptr;void* caller=nullptr;bool sampled=false,returned=false;
    Counts direct[EventCount]{},inclusive[EventCount]{};Snapshot entry{};CallbackSamples* samples=nullptr;
};
inline void PrintSnapshot(FILE* file,const Snapshot& s) {
    fprintf(file," valid=%u ui_vt=%llx renderer=%llx renderer_vt=%llx app=%llx app_vt=%llx manager=%llx manager_vt=%llx manager14=%llx manager15=%llx rcontext=%llx command_list=%llx backend=%llx default_target=%llx target=%llx resource=%llx layer=%llx size=%u,%u clip=%llx,%u,%u",
        s.valid,s.uiVtable,s.renderer,s.rendererVtable,s.application,s.applicationVtable,s.manager,s.managerVtable,s.manager14,s.manager15,
        s.rendererContext,s.commandList,s.backend,s.defaultTarget,s.target,s.resource,s.layer,s.dimensions[0],s.dimensions[1],s.clipData,s.clipCapacity,s.clipCount);
    for(unsigned i=0;i<16;++i)fprintf(file," p%u=%.9g",i,s.projection[i]);
    for(unsigned i=0;i<4;++i)fprintf(file," vp%u=%.9g",i,s.viewport[i]);
}
inline void Record(const char* phase,const Frame& frame,const Snapshot& snapshot) {
    LastErrorGuard error;auto module=owner.load(std::memory_order_relaxed);if(!module)return;
    wchar_t path[MAX_PATH]{};auto length=GetModuleFileNameW(module,path,MAX_PATH);
    auto tail=length && length<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
    if(!tail || wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"ui-root-probe.log"))return;
    ExclusiveLock lock(logLock);FILE* file=nullptr;if(_wfopen_s(&file,path,L"a") || !file)return;
    fprintf(file,"pid=%lu tid=%lu tick=%llu phase=%s root=%llu parent=%llu depth=%u ui=%p context=%p expected_renderer=%p caller=%p returned=%u active=%llu roots=%llu returns=%llu unwinds=%llu",
        GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),phase,frame.id,frame.parent?frame.parent->id:0,frame.depth,
        frame.ui,frame.context,frame.expectedRenderer,frame.caller,frame.returned?1:0,activeRoots.load(),roots.load(),returns.load(),unwinds.load());
    PrintSnapshot(file,snapshot);
    if(strcmp(phase,"entry")){
        fprintf(file," renderer_changed=%u context_changed=%u target_changed=%u default_changed=%u layer_changed=%u projection_changed=%u",
            frame.entry.renderer!=snapshot.renderer,frame.entry.rendererContext!=snapshot.rendererContext,
            frame.entry.target!=snapshot.target,frame.entry.defaultTarget!=snapshot.defaultTarget,
            frame.entry.layer!=snapshot.layer,memcmp(frame.entry.projection,snapshot.projection,sizeof(snapshot.projection))!=0);
        for(unsigned i=0;i<EventCount;++i){const auto& a=frame.direct[i];const auto& b=frame.inclusive[i];const auto& t=totals[i];
            fprintf(file," %s={direct:%llu,inclusive:%llu,match:%llu,different:%llu,changed:%llu,first:%p,last:%p,total:%llu,inside:%llu,outside:%llu,outside_active:%llu}",
                EventNames[i],a.count,b.count,b.matched,b.different,b.changed,a.firstCaller,a.lastCaller,
                t.total.load(),t.inScope.load(),t.outside.load(),t.outsideWhileRootActive.load());
        }
    }
    fputc('\n',file);
    if(frame.samples && strcmp(phase,"entry"))for(unsigned event=0;event<EventCount;++event)
        for(unsigned index=0;index<frame.samples->counts[event];++index){const auto& s=frame.samples->values[event][index];
            fprintf(file,"pid=%lu tid=%lu tick=%llu phase=callback_sample root=%llu event=%s index=%u renderer=%p caller=%p valid=%u changed=%u target=%llx resource=%llx layer=%llx context=%llx command_list=%llx size=%u,%u clip=%llx,%u,%u clip_valid=%u clip_top=",
                GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),frame.id,EventNames[event],index,s.renderer,s.caller,s.valid?1:0,s.changed?1:0,
                s.target,s.resource,s.layer,s.context,s.commandList,s.dimensions[0],s.dimensions[1],s.clipData,s.clipCapacity,s.clipCount,s.clipValid?1:0);
            for(auto byte:s.clipTop)fprintf(file,"%02x",static_cast<unsigned>(byte));
            for(unsigned matrix=0;matrix<MatrixCount;++matrix){fprintf(file," %s=",MatrixNames[matrix]);
                for(unsigned n=0;n<16;++n)fprintf(file,"%s%.9g",n?",":"",s.matrices[matrix][n]);}
            fputc('\n',file);
        }
    fclose(file);
}
class Scope {
public:
    Frame frame;
    Scope(void* ui,void* context,void* caller) {
        LastErrorGuard error;frame.parent=current;frame.id=roots.fetch_add(1)+1;
        frame.depth=frame.parent?frame.parent->depth+1:1;frame.ui=ui;frame.context=context;frame.caller=caller;
        uintptr_t renderer=0;Member(reinterpret_cast<uintptr_t>(ui),0xd0,renderer);
        frame.expectedRenderer=reinterpret_cast<void*>(renderer);frame.sampled=Sample(frame.id);
        if(frame.sampled){frame.entry=Capture(ui);frame.samples=new(std::nothrow) CallbackSamples;}
        current=&frame;++activeRoots;
        unsigned previous=maximumDepth.load(std::memory_order_relaxed);
        while(previous<frame.depth && !maximumDepth.compare_exchange_weak(previous,frame.depth,std::memory_order_relaxed)){}
        if(frame.sampled)Record("entry",frame,frame.entry);
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    ~Scope() noexcept {
        LastErrorGuard error;
        if(frame.returned)++returns;else ++unwinds;
        if(frame.sampled)Record(frame.returned?"exit":"unwind",frame,Capture(frame.ui));
        delete frame.samples;
        current=frame.parent;--activeRoots;
    }
};
inline unsigned EventIndex(const char* event) {
    if(event)for(unsigned i=0;i<EventCount-1;++i)if(!strcmp(event,EventNames[i]))return i;
    return EventCount-1;
}
inline void Add(Counts& counts,bool matched,void* caller,bool changed) {
    if(!counts.count)counts.firstCaller=caller;++counts.count;counts.lastCaller=caller;
    if(matched)++counts.matched;else ++counts.different;if(changed)++counts.changed;
}
inline void ObserveEvent(const char* event,void* renderer,void* caller,bool projectionChanged=false) {
    LastErrorGuard error;auto index=EventIndex(event);auto& total=totals[index];++total.total;
    if(projectionChanged)++total.changed;
    auto frame=current;
    if(!frame){++total.outside;if(activeRoots.load(std::memory_order_relaxed))++total.outsideWhileRootActive;return;}
    ++total.inScope;bool matched=renderer && renderer==frame->expectedRenderer;
    if(matched)++total.matched;else ++total.different;
    Add(frame->direct[index],matched,caller,projectionChanged);
    if(frame->samples && frame->samples->counts[index]<SamplesPerEvent){
        uintptr_t target=0;Member(reinterpret_cast<uintptr_t>(renderer),0x70,target);
        bool seen=false;for(unsigned i=0;i<frame->samples->counts[index];++i){const auto& s=frame->samples->values[index][i];
            if(s.renderer==renderer && s.target==target && s.caller==caller && s.changed==projectionChanged){seen=true;break;}}
        if(!seen)frame->samples->values[index][frame->samples->counts[index]++]=CaptureRenderer(renderer,caller,projectionChanged);
    }
    // Ancestors are inclusive, while direct counts belong only to the innermost scope.
    for(;frame;frame=frame->parent)Add(frame->inclusive[index],renderer && renderer==frame->expectedRenderer,caller,projectionChanged);
}
inline void Observe(void* object,void* context) {
    DWORD incoming=GetLastError();Scope scope(object,context,_ReturnAddress());
    SetLastError(incoming);original.load(std::memory_order_acquire)(object,context);
    DWORD outgoing=GetLastError();scope.frame.returned=true;SetLastError(outgoing);
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace) {
    LastErrorGuard error;ExclusiveLock lock(installationLock);
    if(slot)return ERROR_ALREADY_EXISTS;
    if(activeRoots.load())return ERROR_BUSY;
    if(!base || base>UINTPTR_MAX-(TableRva+(SlotIndex+1)*8))return ERROR_REVISION_MISMATCH;
    auto candidate=reinterpret_cast<Root>(base+FunctionRva);
    auto address=reinterpret_cast<void* volatile*>(base+TableRva+SlotIndex*8);
    unsigned char code[sizeof(Prefix)]{};void* observed=nullptr;
    if(!Read(base+FunctionRva,code) || memcmp(code,Prefix,sizeof(Prefix)) ||
       !Read(reinterpret_cast<uintptr_t>(address),observed) || observed!=reinterpret_cast<void*>(candidate))return ERROR_REVISION_MISMATCH;
    original.store(candidate,std::memory_order_release);owner.store(module,std::memory_order_relaxed);
    slot=address;
    auto status=replace(address,reinterpret_cast<void*>(candidate),reinterpret_cast<void*>(&Observe));
    if(status!=ERROR_SUCCESS){
        // A replacement helper can report a page-protection error after the CAS.
        // Roll back only our own hook; retain state if rollback cannot complete.
        observed=nullptr;
        if(Read(reinterpret_cast<uintptr_t>(address),observed) && observed==reinterpret_cast<void*>(&Observe)){
            auto rollback=replace(address,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(candidate));
            if(rollback!=ERROR_SUCCESS){
                // CAS may have succeeded before restoring page protection failed.
                // Preserve that error, but do not retain a hook already removed.
                observed=nullptr;
                if(Read(reinterpret_cast<uintptr_t>(address),observed) && observed==reinterpret_cast<void*>(candidate))slot=nullptr;
                return rollback;
            }
        }
        observed=nullptr;
        if(!Read(reinterpret_cast<uintptr_t>(address),observed) || observed==reinterpret_cast<void*>(&Observe))return ERROR_READ_FAULT;
        slot=nullptr;
    }
    return status;
}
template<class Replace> DWORD Stop(Replace replace) {
    LastErrorGuard error;ExclusiveLock lock(installationLock);if(!slot)return ERROR_SUCCESS;
    auto status=replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original.load()));
    if(status==ERROR_SUCCESS)slot=nullptr;
    else {
        void* observed=nullptr;
        if(Read(reinterpret_cast<uintptr_t>(slot),observed) && observed==reinterpret_cast<void*>(original.load()))slot=nullptr;
    }
    // Keep original and owner valid for any callback already in flight.
    return status;
}
}
