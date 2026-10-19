#pragma once
#include "ui_root_probe.h"
#include "ui_affine_clip.h"

// Pure observation of MapCustomRenderElement::slot31. Prefix/COL/slot were
// verified in image SHA256 c0dcf8439887317a44174427f0c93f826d50b50a1a1094b8c8c63595bb17990a.
// No shader, projection, rectangle, renderer target, or game state is changed.
namespace ui_map_probe {
using Draw=void(*)(void*,void*);
using ActivityReader=bool(*)();
inline constexpr uintptr_t TableRva=0x5cd8188,CellRva=0x5cd8280,FunctionRva=0x4150550,
    ColRva=0x65a9270,RendererPointerRva=0x80cf5c8,RendererVtableRva=0x5c7ed70,
    MapRendererVtableRva=0x5cd5b98;
inline constexpr unsigned char Prefix[]={0x48,0x8b,0xc4,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8d,0xa8,0x28,0xff,0xff,0xff,0x48,0x81,0xec,0xc0,0x01,0,0};
inline constexpr uint32_t ColValues[]={1,0,0,0x7c6ed60,0x63c47d8,0x65a9270};
using ui_root_probe::Read;
using ui_root_probe::Member;
using ui_root_probe::LastErrorGuard;
using ui_root_probe::ExclusiveLock;
inline std::atomic<Draw> original{nullptr};
inline std::atomic<ActivityReader> activityReader{nullptr};
inline std::atomic<HMODULE> owner{nullptr};
inline std::atomic<uintptr_t> imageBase{0};
inline SRWLOCK installationLock=SRWLOCK_INIT,logLock=SRWLOCK_INIT;
inline void* volatile* slot=nullptr;
inline std::atomic<uint64_t> calls{0},returns{0},unwinds{0},activeCalls{0},inside{0},outside{0},
    outsideActive{0},samples{0},sampleMatches{0},sampleProjectionMatches{0},sampleVRActive{0};
// Independent budgets ensure an unrelated/native call cannot consume the first
// observations of the root-scoped, active-VR path. Bit 0 = root, bit 1 = VR.
inline std::atomic<uint64_t> eligibleCalls[4]{},lastLogTick[4]{};
// This stack describes only the synchronous native slot31 invocation. It does
// not cache MapRenderer identities: those can change while slot31 prepares a view.
struct MapFrame {
    MapFrame* parent=nullptr;ui_root_probe::Frame* root=nullptr;
    uint64_t id=0,rootId=0;unsigned depth=0;
    void* node=nullptr;void* context=nullptr;void* expectedRenderer=nullptr;
};
inline thread_local MapFrame* currentMap=nullptr;
inline std::atomic<uint64_t> mapScopes{0},mapTlsMismatches{0};
struct MapScope {
    MapFrame frame{};
    MapScope(void* node,void* context) {
        frame.parent=currentMap;frame.root=ui_root_probe::current;
        frame.id=mapScopes.fetch_add(1,std::memory_order_relaxed)+1;
        frame.depth=frame.parent?frame.parent->depth+1:1;frame.node=node;frame.context=context;
        if(frame.root){frame.rootId=frame.root->id;frame.expectedRenderer=frame.root->expectedRenderer;}
        currentMap=&frame;
    }
    MapScope(const MapScope&)=delete;MapScope& operator=(const MapScope&)=delete;
    ~MapScope() noexcept {
        LastErrorGuard error;if(currentMap!=&frame)++mapTlsMismatches;
        currentMap=frame.parent;
    }
};
struct Activity {bool known=false,active=false;};
inline Activity CurrentActivity() {
    LastErrorGuard error;Activity a;
    if(auto active=activityReader.load(std::memory_order_acquire)){
        try{a.active=active();a.known=true;}catch(...){a.known=false;}
    }
    return a;
}
struct RootToken {
    uint64_t id=0,parent=0;unsigned depth=0;uintptr_t ui=0,expectedRenderer=0;
    bool active=false;
};
inline RootToken CurrentRoot() {
    RootToken t;t.active=ui_root_probe::activeRoots.load(std::memory_order_relaxed)!=0;
    if(auto f=ui_root_probe::current){t.id=f->id;t.parent=f->parent?f->parent->id:0;t.depth=f->depth;
        t.ui=reinterpret_cast<uintptr_t>(f->ui);t.expectedRenderer=reinterpret_cast<uintptr_t>(f->expectedRenderer);}
    return t;
}
struct Snapshot {
    uint64_t tick=0;RootToken root{};
    uintptr_t node=0,nodeVtable=0,drawContext=0,caller=0,renderer=0,rendererVtable=0,
        rendererContext=0,mapRenderer=0,mapRendererVtable=0,mapView=0,mapViewVtable=0,viewRenderer=0;
    uint32_t flags=0,valid=0;
    // drawContext is the AVUI render context (+24 opacity). renderer+40 is a
    // backend graphics context. Equality is diagnostic, not an eligibility gate.
    bool rootMatches=false,contextMatches=false,projectionMatches=false,vrKnown=false,vrActive=false;
    bool mapConstantsValid=false,mapStateValid=false;uint32_t mapState=0;float mapConstants[52]{};
    ui_root_probe::RenderSample rendererState{};
};
inline Snapshot Capture(void* node,void* context,void* caller,const Activity* activity=nullptr) {
    LastErrorGuard error;Snapshot s;s.tick=GetTickCount64();s.root=CurrentRoot();
    s.node=reinterpret_cast<uintptr_t>(node);s.drawContext=reinterpret_cast<uintptr_t>(context);
    s.caller=reinterpret_cast<uintptr_t>(caller);
    auto a=activity?*activity:CurrentActivity();s.vrActive=a.active;s.vrKnown=a.known;
    auto base=imageBase.load(std::memory_order_acquire);
    if(Read(s.node,s.nodeVtable))s.valid|=1;
    if(base && base<=UINTPTR_MAX-(RendererPointerRva+8)){
        if(s.nodeVtable==base+TableRva){
            if(Member(s.node,0xc0,s.flags))s.valid|=2;
            if(Member(s.node,0x140,s.mapRenderer))s.valid|=4;
            if(Member(s.node,0x158,s.mapView))s.valid|=8;
            if(Read(s.mapRenderer,s.mapRendererVtable))s.valid|=16;
            if(Read(s.mapView,s.mapViewVtable))s.valid|=32;
            if(Member(s.mapView,8,s.viewRenderer))s.valid|=64;
            if(s.mapRenderer && s.mapRenderer==s.viewRenderer && s.mapRendererVtable==base+MapRendererVtableRva){
                s.mapConstantsValid=Member(s.mapRenderer,0xc0,s.mapConstants);
                s.mapStateValid=Member(s.mapRenderer,0x78,s.mapState);
                // Re-read identities after the bounded copy, including for the
                // after snapshot. This detects reassociation during the read;
                // the values themselves remain a non-atomic CPU snapshot.
                uintptr_t freshMap=0,freshView=0,freshViewRenderer=0,freshVtable=0;
                if(!Member(s.node,0x140,freshMap) || freshMap!=s.mapRenderer ||
                   !Member(s.node,0x158,freshView) || freshView!=s.mapView ||
                   !Member(freshView,8,freshViewRenderer) || freshViewRenderer!=s.mapRenderer ||
                   !Read(s.mapRenderer,freshVtable) || freshVtable!=base+MapRendererVtableRva){
                    s.mapConstantsValid=false;s.mapStateValid=false;
                }
            }
        }
        if(Read(base+RendererPointerRva,s.renderer))s.valid|=128;
        if(Read(s.renderer,s.rendererVtable))s.valid|=256;
        if(s.rendererVtable==base+RendererVtableRva){
            if(Member(s.renderer,0x40,s.rendererContext))s.valid|=512;
            s.rendererState=ui_root_probe::CaptureRenderer(reinterpret_cast<void*>(s.renderer),caller,false);
            if(s.rendererState.valid){s.valid|=1024;
                s.projectionMatches=ui_affine_clip::Matches(reinterpret_cast<void*>(s.renderer),s.rendererState.matrices[0]);}
        }
    }
    s.rootMatches=s.root.id && s.renderer && s.root.expectedRenderer==s.renderer;
    s.contextMatches=(s.valid&512) && s.drawContext==s.rendererContext;
    return s;
}
inline bool ShouldSample(unsigned bucket) {
    if(!owner.load(std::memory_order_acquire))return false;
    if(bucket>=4)return false;
    auto sequence=eligibleCalls[bucket].fetch_add(1,std::memory_order_relaxed)+1;
    auto now=GetTickCount64();
    if(sequence<=4){lastLogTick[bucket].store(now,std::memory_order_relaxed);return true;}
    auto previous=lastLogTick[bucket].load(std::memory_order_relaxed);
    return now-previous>=1000 && lastLogTick[bucket].compare_exchange_strong(previous,now,std::memory_order_relaxed);
}
inline void Statistics(FILE* file) {
    fprintf(file," map_calls=%llu map_returns=%llu map_unwinds=%llu map_active=%llu map_inside=%llu map_outside=%llu map_outside_active=%llu map_samples=%llu map_sample_root_match=%llu map_sample_projection_match=%llu map_sample_vr_active=%llu",
        calls.load(),returns.load(),unwinds.load(),activeCalls.load(),inside.load(),outside.load(),outsideActive.load(),
        samples.load(),sampleMatches.load(),sampleProjectionMatches.load(),sampleVRActive.load());
    fprintf(file," map_eligible_buckets=%llu,%llu,%llu,%llu",eligibleCalls[0].load(),eligibleCalls[1].load(),eligibleCalls[2].load(),eligibleCalls[3].load());
}
inline void Record(const char* phase,uint64_t sequence,const Snapshot& s,const Snapshot* entry=nullptr,unsigned bucket=0) {
    LastErrorGuard error;auto module=owner.load(std::memory_order_acquire);if(!module)return;
    wchar_t path[MAX_PATH]{};auto length=GetModuleFileNameW(module,path,MAX_PATH);
    auto slash=length && length<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
    if(!slash || wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ui-map-probe.log"))return;
    ExclusiveLock lock(logLock);FILE* file=nullptr;if(_wfopen_s(&file,path,L"a") || !file)return;
    const auto& r=s.rendererState;
    fprintf(file,"pid=%lu tid=%lu tick=%llu phase=%s sequence=%llu root=%llu parent=%llu depth=%u root_active=%u ui=%llx expected_renderer=%llx node=%llx node_vt=%llx context=%llx caller=%llx valid=%u flags_c0=%u clip_requested=%u map_renderer=%llx map_renderer_vt=%llx map_view=%llx map_view_vt=%llx view_renderer=%llx renderer=%llx renderer_vt=%llx renderer_context=%llx root_match=%u context_match=%u projection_match=%u vr_known=%u vr_active=%u renderer_valid=%u target=%llx resource=%llx layer=%llx command_list=%llx size=%u,%u clip=%llx,%u,%u clip_valid=%u clip_top=",
        GetCurrentProcessId(),GetCurrentThreadId(),s.tick,phase,sequence,s.root.id,s.root.parent,s.root.depth,s.root.active?1u:0u,
        s.root.ui,s.root.expectedRenderer,s.node,s.nodeVtable,s.drawContext,s.caller,s.valid,s.flags,(s.flags>>1)&1u,
        s.mapRenderer,s.mapRendererVtable,s.mapView,s.mapViewVtable,s.viewRenderer,s.renderer,s.rendererVtable,s.rendererContext,
        s.rootMatches?1u:0u,s.contextMatches?1u:0u,s.projectionMatches?1u:0u,s.vrKnown?1u:0u,s.vrActive?1u:0u,
        r.valid?1u:0u,r.target,r.resource,r.layer,r.commandList,r.dimensions[0],r.dimensions[1],r.clipData,r.clipCapacity,r.clipCount,r.clipValid?1u:0u);
    for(auto byte:r.clipTop)fprintf(file,"%02x",static_cast<unsigned>(byte));
    for(unsigned m=0;m<ui_root_probe::MatrixCount;++m){fprintf(file," %s=",ui_root_probe::MatrixNames[m]);
        for(unsigned i=0;i<16;++i)fprintf(file,"%s%.9g",i?",":"",r.matrices[m][i]);}
    // Non-atomic CPU snapshots, including after-state, are not proof of the
    // exact bytes uploaded to the GPU. No deferred pointer dereferences here.
    fprintf(file," map_cb_valid=%u map_state_valid=%u map_state78=%u map_cb_c0=",s.mapConstantsValid?1u:0u,s.mapStateValid?1u:0u,s.mapState);
    for(unsigned i=0;i<52;++i)fprintf(file,"%s%.9g",i?",":"",s.mapConstants[i]);
    if(entry)fprintf(file," entry_tick=%llu root_changed=%u renderer_changed=%u context_changed=%u target_changed=%u layer_changed=%u map_view_changed=%u view_renderer_changed=%u projection_changed=%u",
        entry->tick,s.root.id!=entry->root.id,s.renderer!=entry->renderer,s.rendererContext!=entry->rendererContext,
        r.target!=entry->rendererState.target,r.layer!=entry->rendererState.layer,s.mapView!=entry->mapView,
        s.viewRenderer!=entry->viewRenderer,memcmp(r.matrices[0],entry->rendererState.matrices[0],64)!=0);
    fprintf(file," sample_bucket=%u",bucket);Statistics(file);fputc('\n',file);fclose(file);
}
class Invocation {
    void* node;void* context;void* caller;uint64_t sequence;unsigned bucket=0;bool sampled=false;Snapshot entry{};
public:
    bool returned=false;
    Invocation(void* object,void* drawContext,void* source):node(object),context(drawContext),caller(source) {
        LastErrorGuard error;++activeCalls;sequence=calls.fetch_add(1)+1;auto root=CurrentRoot();
        if(root.id)++inside;else{++outside;if(root.active)++outsideActive;}
        auto activity=owner.load(std::memory_order_acquire)?CurrentActivity():Activity{};
        bucket=(root.id?1u:0u)|(activity.known && activity.active?2u:0u);
        sampled=ShouldSample(bucket);if(sampled){entry=Capture(node,context,caller,&activity);++samples;
            if(entry.rootMatches)++sampleMatches;if(entry.projectionMatches)++sampleProjectionMatches;
            if(entry.vrKnown && entry.vrActive)++sampleVRActive;Record("entry",sequence,entry,nullptr,bucket);}
    }
    Invocation(const Invocation&)=delete;Invocation& operator=(const Invocation&)=delete;
    ~Invocation() noexcept {
        LastErrorGuard error;if(returned)++returns;else ++unwinds;
        if(sampled){auto after=Capture(node,context,caller);Record(returned?"return":"unwind",sequence,after,&entry,bucket);}
        --activeCalls;
    }
};
__declspec(noinline) inline void Observe(void* node,void* drawContext) {
    DWORD incoming=GetLastError();Invocation invocation(node,drawContext,_ReturnAddress());
    MapScope mapScope(node,drawContext);
    auto function=original.load(std::memory_order_acquire);SetLastError(incoming);
    function(node,drawContext); // Exact ABI, exactly one call, propagate native exception.
    DWORD outgoing=GetLastError();invocation.returned=true;SetLastError(outgoing);
}
inline void* Hook(){return reinterpret_cast<void*>(&Observe);}
inline bool Pending(){return slot!=nullptr;}
template<class Replace> DWORD Remove(Replace replace) {
    if(!slot)return ERROR_SUCCESS;
    void* observed=nullptr;auto native=reinterpret_cast<void*>(original.load(std::memory_order_acquire));
    if(!Read(reinterpret_cast<uintptr_t>(slot),observed))return ERROR_READ_FAULT;
    if(observed==native){slot=nullptr;return ERROR_SUCCESS;}
    if(observed!=Hook()){slot=nullptr;return ERROR_INVALID_DATA;}
    DWORD status=replace(slot,Hook(),native);observed=nullptr;
    if(!Read(reinterpret_cast<uintptr_t>(slot),observed))return status?status:ERROR_READ_FAULT;
    if(observed==native){slot=nullptr;return status;}
    if(observed!=Hook()){slot=nullptr;return status?status:ERROR_INVALID_DATA;}
    return status?status:ERROR_WRITE_FAULT;
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace,ActivityReader active=nullptr) {
    LastErrorGuard error;ExclusiveLock lock(installationLock);
    if(Pending())return ERROR_ALREADY_EXISTS;
    if(activeCalls.load() || ui_root_probe::activeRoots.load())return ERROR_BUSY;
    if(!base || base>UINTPTR_MAX-(RendererPointerRva+8))return ERROR_REVISION_MISMATCH;
    unsigned char prefix[sizeof(Prefix)]{};uint32_t col[6]{};uintptr_t locator=0,native=0;
    if(!Read(base+FunctionRva,prefix) || memcmp(prefix,Prefix,sizeof(Prefix)) ||
       !Read(base+TableRva-8,locator) || locator!=base+ColRva ||
       !Read(base+ColRva,col) || memcmp(col,ColValues,sizeof(ColValues)) ||
       !Read(base+CellRva,native) || native!=base+FunctionRva)return ERROR_REVISION_MISMATCH;
    original.store(reinterpret_cast<Draw>(base+FunctionRva),std::memory_order_release);
    imageBase.store(base,std::memory_order_release);owner.store(module,std::memory_order_release);
    activityReader.store(active,std::memory_order_release);
    slot=reinterpret_cast<void* volatile*>(base+CellRva);
    DWORD status=replace(slot,reinterpret_cast<void*>(native),Hook());void* observed=nullptr;
    if(!status && (!Read(reinterpret_cast<uintptr_t>(slot),observed) || observed!=Hook()))status=ERROR_WRITE_FAULT;
    if(status){DWORD rollback=Remove(replace);if(rollback)return rollback;}
    // Retain originals/base/log owner for callbacks already in flight.
    return status;
}
template<class Replace> DWORD Stop(Replace replace) {
    LastErrorGuard error;ExclusiveLock lock(installationLock);return Remove(replace);
}
} // namespace ui_map_probe
