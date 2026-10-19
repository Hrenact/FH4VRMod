#pragma once
#include "ui_root_probe.h"
#ifdef FH4VR_UI_MATERIAL_CLIP
#include "ui_material_clip.h"
#include "stereo_trial.h"
#endif

// Slot0 snapshots are read-only and captured before optional guarded correction.
// These presets alone do not classify screen UI: instruments/world labels may
// use the same presets. ui_material_clip supplies separate strict eligibility.
// Raw bytes + 1a7bd70 callsite establish void(binding, context, packet), x64 ABI.
namespace ui_material_probe {
using Draw = void(*)(void*,void*,void*);
inline constexpr uintptr_t RendererPointerRva=0x80cf5c8;
inline constexpr uintptr_t RendererVtableRva=0x5c7ed70;
inline constexpr unsigned Count=4;
struct Spec { const char* name;uintptr_t table,function;unsigned length;unsigned char prefix[24]; };
inline constexpr Spec Specs[Count]={
    {"Default",0x4fe14e0,0x1a7c100,20,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20}},
    {"AcrylicPanel",0x4fe1608,0x1a7c3a0,21,{0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xd8,0xf9,0xff,0xff}},
    {"Glass",0x4fe1570,0x1a7d9a0,19,{0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xa0}},
    {"Roundtangle",0x4fe1528,0x1a7f020,19,{0x48,0x8b,0xc4,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xc8,0x48,0x81,0xec,0x20,0x01,0x00,0x00}}
};
using ui_root_probe::LastErrorGuard;
using ui_root_probe::ExclusiveLock;
using ui_root_probe::Read;
using ui_root_probe::Member;
inline SRWLOCK installationLock=SRWLOCK_INIT,logLock=SRWLOCK_INIT;
inline std::atomic<HMODULE> owner{nullptr};
inline std::atomic<uintptr_t> imageBase{0};
inline std::atomic<uint64_t> activeCalls{0};
struct State {
    std::atomic<Draw> original{nullptr};
    void* volatile* slot=nullptr; // Only Start/Stop, under installationLock.
    std::atomic<uint64_t> calls{0},returned{0},unwinds{0},inside{0},outside{0},outsideActive{0};
    std::atomic<uint64_t> samples{0},sampleMatches{0},sampleDifferent{0},sampleUnknown{0};
    // Separate raw-plane classes inside/outside the root. A common disabled
    // draw must not consume the only sample while a rare nontrivial draw runs.
    std::atomic<uint64_t> classCalls[6]{},sampledTicks[6]{};
};
inline State states[Count];
struct RootToken {
    uint64_t id=0,parent=0;unsigned depth=0;uintptr_t expectedRenderer=0;bool rootActive=false;
};
inline RootToken CurrentRoot() {
    RootToken t;t.rootActive=ui_root_probe::activeRoots.load(std::memory_order_relaxed)!=0;
    if(auto f=ui_root_probe::current){t.id=f->id;t.parent=f->parent?f->parent->id:0;t.depth=f->depth;
        t.expectedRenderer=reinterpret_cast<uintptr_t>(f->expectedRenderer);}
    return t;
}
struct Snapshot {
    // Addresses are correlation identifiers only. Record() never dereferences them.
    uintptr_t binding=0,context=0,packet=0,caller=0,bindingVtable=0,renderer=0,rendererVtable=0;
    uintptr_t expectedVtable=0,matrixPointers[3]{};
    float packetMatrices[3][16]{};
    unsigned char packetRaw50[64]{};
    unsigned valid=0; // 1 binding vt,2 global renderer,4 actual vt,8 packet ptrs,
                      // 16/32/64 matrices,128 raw50,256 expected vt.
    uint64_t captureTick=0;
    unsigned raw50Class=0; // 0 unreadable,1 repeated (0,0,0,-1),2 other raw values.
    RootToken root{};
    ui_root_probe::RenderSample rendererState{};
};
struct RawPlanes {
    unsigned char data[64]{};bool valid=false;unsigned category=0;uint64_t tick=0;
};
inline RawPlanes CaptureRawPlanes(void* packet) {
    LastErrorGuard error;RawPlanes result;result.tick=GetTickCount64();
    result.valid=Member(reinterpret_cast<uintptr_t>(packet),0x50,result.data);
    if(result.valid){float values[16]{};memcpy(values,result.data,sizeof(values));result.category=1;
        for(unsigned i=0;i<16;++i)if(values[i]!=(i%4==3?-1.0f:0.0f)){result.category=2;break;}}
    return result;
}
inline Snapshot Capture(void* binding,void* context,void* packet,void* caller,const RootToken& root,const RawPlanes* raw=nullptr) {
    LastErrorGuard error;Snapshot s;s.captureTick=GetTickCount64();s.binding=reinterpret_cast<uintptr_t>(binding);
    s.context=reinterpret_cast<uintptr_t>(context);s.packet=reinterpret_cast<uintptr_t>(packet);
    s.caller=reinterpret_cast<uintptr_t>(caller);s.root=root;
    if(Read(s.binding,s.bindingVtable))s.valid|=1;
    auto base=imageBase.load(std::memory_order_acquire);
    if(base && base<=UINTPTR_MAX-RendererPointerRva && Read(base+RendererPointerRva,s.renderer))s.valid|=2;
    if(Read(s.renderer,s.rendererVtable))s.valid|=4;
    if(Read(root.expectedRenderer,s.expectedVtable))s.valid|=256;
    // Copy the three contiguous pointers once, then exactly 64 bytes from each.
    // No packet or matrix pointer escapes for a later read.
    if(Member(s.packet,0x30,s.matrixPointers)){
        s.valid|=8;
        for(unsigned i=0;i<3;++i)if(Read(s.matrixPointers[i],s.packetMatrices[i]))s.valid|=16u<<i;
    }
    auto direct=raw?RawPlanes{}:CaptureRawPlanes(packet);const auto& planes=raw?*raw:direct;
    s.raw50Class=planes.category;if(raw)s.captureTick=raw->tick;
    if(planes.valid){memcpy(s.packetRaw50,planes.data,sizeof(s.packetRaw50));s.valid|=128;}
    if(base && base<=UINTPTR_MAX-RendererVtableRva && s.rendererVtable==base+RendererVtableRva)
        s.rendererState=ui_root_probe::CaptureRenderer(reinterpret_cast<void*>(s.renderer),caller,false);
    return s;
}
inline bool ShouldSample(unsigned index,bool inside,unsigned category,uint64_t classCount) {
    if(!owner.load(std::memory_order_acquire))return false;
    auto now=GetTickCount64();auto& tick=states[index].sampledTicks[(inside?3:0)+category];
    if(classCount<=4){tick.store(now,std::memory_order_relaxed);return true;}
    auto previous=tick.load(std::memory_order_relaxed);
    return now-previous>=1000 && tick.compare_exchange_strong(previous,now,std::memory_order_relaxed);
}
inline void PrintMatrix(FILE* f,const float* value) {
    for(unsigned i=0;i<16;++i)fprintf(f,"%s%.9g",i?",":"",value[i]);
}
inline void Record(unsigned index,uint64_t sequence,const Snapshot& s,bool returned,const RootToken& after) {
    LastErrorGuard error;auto module=owner.load(std::memory_order_acquire);if(!module)return;
    wchar_t path[MAX_PATH]{};auto length=GetModuleFileNameW(module,path,MAX_PATH);
    auto tail=length && length<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
    if(!tail || wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"ui-material-probe.log"))return;
    ExclusiveLock lock(logLock);FILE* file=nullptr;if(_wfopen_s(&file,path,L"a") || !file)return;
    const auto& state=states[index];const auto& r=s.rendererState;
    fprintf(file,"pid=%lu tid=%lu capture_tick=%llu return_tick=%llu preset=%s phase=%s sequence=%llu calls=%llu returns=%llu unwinds=%llu inside=%llu outside=%llu outside_active=%llu samples=%llu sample_match=%llu sample_different=%llu sample_unknown=%llu active=%llu root=%llu parent=%llu depth=%u root_active=%u exit_root=%llu expected_renderer=%llx expected_vt=%llx binding=%llx binding_vt=%llx context=%llx packet=%llx caller=%llx valid=%u renderer=%llx renderer_vt=%llx renderer_match=%u renderer_valid=%u target=%llx layer=%llx command_list=%llx size=%u,%u clip=%llx,%u,%u clip_valid=%u clip_top=",
        GetCurrentProcessId(),GetCurrentThreadId(),s.captureTick,GetTickCount64(),Specs[index].name,returned?"return":"unwind",sequence,
        state.calls.load(),state.returned.load(),state.unwinds.load(),state.inside.load(),state.outside.load(),state.outsideActive.load(),
        state.samples.load(),state.sampleMatches.load(),state.sampleDifferent.load(),state.sampleUnknown.load(),activeCalls.load(),
        s.root.id,s.root.parent,s.root.depth,s.root.rootActive?1u:0u,after.id,s.root.expectedRenderer,s.expectedVtable,
        s.binding,s.bindingVtable,s.context,s.packet,s.caller,s.valid,s.renderer,s.rendererVtable,
        s.renderer && s.renderer==s.root.expectedRenderer?1u:0u,r.valid?1u:0u,r.target,r.layer,r.commandList,
        r.dimensions[0],r.dimensions[1],r.clipData,r.clipCapacity,r.clipCount,r.clipValid?1u:0u);
    for(auto byte:r.clipTop)fprintf(file,"%02x",static_cast<unsigned>(byte));
    for(unsigned i=0;i<3;++i){fprintf(file," packet_ptr%u=%llx packet_m%u=",i,s.matrixPointers[i],i);PrintMatrix(file,s.packetMatrices[i]);}
    fprintf(file," raw50_class=%u raw50_counts=",s.raw50Class);for(unsigned i=0;i<6;++i)fprintf(file,"%s%llu",i?",":"",state.classCalls[i].load());
    fprintf(file," packet_raw50=");for(auto byte:s.packetRaw50)fprintf(file,"%02x",static_cast<unsigned>(byte));
    float raw[16]{};memcpy(raw,s.packetRaw50,sizeof(raw));fprintf(file," packet_raw50_floats=");PrintMatrix(file,raw);
    for(unsigned i=0;i<ui_root_probe::MatrixCount;++i){fprintf(file," %s=",ui_root_probe::MatrixNames[i]);PrintMatrix(file,r.matrices[i]);}
#ifdef FH4VR_UI_MATERIAL_CLIP
    ui_material_clip::Statistics(file);
#endif
    fputc('\n',file);fclose(file);
}
class Invocation {
    unsigned index;uint64_t sequence;bool sampled=false;Snapshot snapshot{};
public:
    bool returned=false;
    Invocation(unsigned which,void* binding,void* context,void* packet,void* caller):index(which) {
        LastErrorGuard error;++activeCalls;auto& state=states[index];sequence=state.calls.fetch_add(1)+1;
        auto root=CurrentRoot();bool inside=root.id!=0;
        if(inside)++state.inside;else ++state.outside;
        if(!inside && root.rootActive)++state.outsideActive;
        RawPlanes raw;
        if(owner.load(std::memory_order_acquire)){raw=CaptureRawPlanes(packet);
            auto count=state.classCalls[(inside?3:0)+raw.category].fetch_add(1)+1;
            sampled=ShouldSample(index,inside,raw.category,count);}
        if(sampled){snapshot=Capture(binding,context,packet,caller,root,&raw);++state.samples;
            if(!root.expectedRenderer || !snapshot.rendererState.valid)++state.sampleUnknown;
            else if(snapshot.renderer==root.expectedRenderer)++state.sampleMatches;
            else ++state.sampleDifferent;}
    }
    Invocation(const Invocation&)=delete;Invocation& operator=(const Invocation&)=delete;
    ~Invocation() noexcept {
        LastErrorGuard error;auto& state=states[index];if(returned)++state.returned;else ++state.unwinds;
        if(sampled)Record(index,sequence,snapshot,returned,CurrentRoot());--activeCalls;
    }
};
template<unsigned Index> __declspec(noinline) void Observe(void* binding,void* context,void* packet) {
    static_assert(Index<Count,"material preset index");
    DWORD incoming=GetLastError();Invocation invocation(Index,binding,context,packet,_ReturnAddress());
    auto function=states[Index].original.load(std::memory_order_acquire);
#ifdef FH4VR_UI_MATERIAL_CLIP
    ui_material_clip::Scope correction(Index,imageBase.load(std::memory_order_acquire),binding,context,packet,
        _ReturnAddress(),owner.load(std::memory_order_acquire) && stereo_trial::Offset(true,GetTickCount64())!=0);
#endif
    SetLastError(incoming);function(binding,context,packet); // Exactly once; no retry or exception suppression.
    DWORD outgoing=GetLastError();
#ifdef FH4VR_UI_MATERIAL_CLIP
    correction.Returned();
#endif
    invocation.returned=true;SetLastError(outgoing);
}
inline void* Hook(unsigned index) {
    static void* const hooks[]={reinterpret_cast<void*>(&Observe<0>),reinterpret_cast<void*>(&Observe<1>),
        reinterpret_cast<void*>(&Observe<2>),reinterpret_cast<void*>(&Observe<3>)};
    return hooks[index];
}
inline bool Pending() {for(auto& state:states)if(state.slot)return true;return false;}
// Also handles a Replace helper that reports an error after successfully doing
// its CAS (e.g. restoring page protection failed). Never overwrite a third party.
template<class Replace> DWORD Remove(unsigned index,Replace replace) {
    auto& state=states[index];if(!state.slot)return ERROR_SUCCESS;
    void* observed=nullptr;auto native=reinterpret_cast<void*>(state.original.load(std::memory_order_acquire));
    if(!Read(reinterpret_cast<uintptr_t>(state.slot),observed))return ERROR_READ_FAULT;
    if(observed==native){state.slot=nullptr;return ERROR_SUCCESS;}
    if(observed!=Hook(index)){state.slot=nullptr;return ERROR_INVALID_DATA;}
    DWORD status=replace(state.slot,Hook(index),native);observed=nullptr;
    if(!Read(reinterpret_cast<uintptr_t>(state.slot),observed))return status?status:ERROR_READ_FAULT;
    if(observed==native){state.slot=nullptr;return status;}
    if(observed!=Hook(index)){state.slot=nullptr;return status?status:ERROR_INVALID_DATA;}
    return status?status:ERROR_WRITE_FAULT; // Own hook still live: retain retryable state.
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace) {
    LastErrorGuard error;ExclusiveLock lock(installationLock);
    if(Pending())return ERROR_ALREADY_EXISTS;
    if(activeCalls.load() || ui_root_probe::activeRoots.load())return ERROR_BUSY;
    if(!base || base>UINTPTR_MAX-(RendererPointerRva+sizeof(void*)))return ERROR_REVISION_MISMATCH;
    // Validate all four entries before the first mutation. Prefixes end at whole instructions.
    for(unsigned i=0;i<Count;++i){const auto& spec=Specs[i];unsigned char prefix[24]{};SIZE_T got=0;void* observed=nullptr;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+spec.function),prefix,spec.length,&got) ||
           got!=spec.length || memcmp(prefix,spec.prefix,spec.length) || !Read(base+spec.table,observed) ||
           observed!=reinterpret_cast<void*>(base+spec.function))return ERROR_REVISION_MISMATCH;}
    for(unsigned i=0;i<Count;++i)states[i].original.store(reinterpret_cast<Draw>(base+Specs[i].function),std::memory_order_release);
    imageBase.store(base,std::memory_order_release);owner.store(module,std::memory_order_release);
    DWORD status=ERROR_SUCCESS;
    for(unsigned i=0;i<Count;++i){auto& state=states[i];state.slot=reinterpret_cast<void* volatile*>(base+Specs[i].table);
        status=replace(state.slot,reinterpret_cast<void*>(base+Specs[i].function),Hook(i));
        void* observed=nullptr;
        if(!status && (!Read(reinterpret_cast<uintptr_t>(state.slot),observed) || observed!=Hook(i)))status=ERROR_WRITE_FAULT;
        if(status)break;}
    if(status){DWORD rollback=ERROR_SUCCESS;for(unsigned i=Count;i--;) {auto r=Remove(i,replace);if(r && !rollback)rollback=r;}
        if(rollback)return rollback;}
    // Keep originals, imageBase and owner valid for a callback already in flight,
    // even when all table cells were successfully restored during rollback.
    return status;
}
template<class Replace> DWORD Stop(Replace replace) {
    LastErrorGuard error;ExclusiveLock lock(installationLock);DWORD result=ERROR_SUCCESS;
    for(unsigned i=Count;i--;){auto status=Remove(i,replace);if(status && !result)result=status;}
    return result;
}
}
