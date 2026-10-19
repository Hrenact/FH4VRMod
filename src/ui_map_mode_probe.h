#pragma once
#include "ui_map_probe.h"
#include "ui_map_clip_math.h"

// Map ShaderMode::slot1 is a bind-only boundary: native map CB0 has already
// been uploaded, and geometry is submitted after this virtual call returns.
// Active foreground VR uploads a local replacement; no shared renderer/cache
// bytes change. The original native map Begin restores CB0 when VR is inactive.
namespace ui_map_mode_probe {
using Mode=void(*)(void*,void*);
using Upload=void(*)(void*,const void*,uint32_t);
using ui_root_probe::Read;using ui_root_probe::Member;
using ui_root_probe::LastErrorGuard;using ui_root_probe::ExclusiveLock;
inline constexpr unsigned Count=7;
inline constexpr uintptr_t ReturnRva=0x4133967,CallsiteRva=0x413395b,UploadRva=0x3e0afa0;
// Load mode and bundle, call mode.slot1, then change map state from 1 to 2.
inline constexpr unsigned char CallsiteBytes[]={0x48,0x8b,0x0e,0x48,0x8b,0x01,0x48,0x8b,0xd3,0xff,0x50,0x08,
    0xc7,0x47,0x78,0x02,0,0,0};
struct Spec {const char* name;uintptr_t table,function,col;uint32_t type,hierarchy;};
inline constexpr Spec Specs[Count]={
    {"Normal",0x5cd5a48,0x4139d80,0x65a6d38,0x7e1cfb0,0x65a6d60},
    {"Wrap",0x5cd5a60,0x4139d80,0x65a6db8,0x7e1cff0,0x65a6de0},
    {"Dots",0x5cd5a90,0x4139e30,0x65a6e38,0x7e1d030,0x65a6e60},
    {"Line",0x5cd5ac0,0x4139d80,0x65a6fc0,0x7e1d198,0x65a6fe8},
    {"TextureAlphaOnly",0x5cd5b08,0x4139d80,0x65a7140,0x7e1d270,0x65a7168},
    {"TextureSelection",0x5cd5b20,0x4139d80,0x65a71c0,0x7e1d2c0,0x65a71e8},
    {"TextureHighlight",0x5cd5b50,0x4139e90,0x65a7240,0x7e1d310,0x65a7268}
};
// All three concrete implementations share this complete 16-byte prefix.
inline constexpr unsigned char ModePrefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x41,0x38,0x48,0x8b,0xfa};
inline constexpr unsigned char UploadPrefix[]={0x40,0x53,0x48,0x83,0xec,0x20,0x45,0x8b,0xc8,0x48,0x8b,0xd9,0x4c,0x8b,0xc2,
    0x8b,0x91,0x68,0x06,0,0,0x48,0x8b,0x89,0xa0,0,0,0,0xe8,0x8f,0x11,0,0};
inline SRWLOCK installationLock=SRWLOCK_INIT,logLock=SRWLOCK_INIT;
inline std::atomic<HMODULE> owner{nullptr};inline std::atomic<uintptr_t> imageBase{0};
inline std::atomic<Upload> upload{nullptr};
inline std::atomic<uint64_t> activeCalls{0},applied{0},rejected{0},notRequested{0},fuseEvents{0};
inline std::atomic<bool> fused{false};
struct State {std::atomic<Mode> original{nullptr};void* volatile* slot=nullptr;
    std::atomic<uint64_t> calls{0},returns{0},unwinds{0},samples[2]{},ticks[2]{};};
inline State states[Count];
enum Reason : unsigned {Eligible,Scope,Caller,ModeType,Node,MapIdentity,Bundle,Binding,Context,MapState,Projection,Clip,Cache,Math,Changed,NoUpload,Fused,UploadException,ReasonCount};
inline constexpr const char* Reasons[]={"eligible","scope","caller","mode_type","node","map_identity","bundle","binding","context","map_state","projection","clip","cache","math","changed","no_upload","fused","upload_exception"};
inline std::atomic<uint64_t> reasons[ReasonCount]{};
struct Controls {bool vr=false,foreground=false;};
inline Controls RuntimeControls(){LastErrorGuard error;Controls c;auto a=ui_map_probe::CurrentActivity();
    c.vr=a.known && a.active;DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    c.foreground=pid==GetCurrentProcessId();return c;}
using ControlsReader=Controls(*)();inline std::atomic<ControlsReader> controlsReader{&RuntimeControls};
struct Token {ui_map_probe::MapFrame* map=nullptr;ui_root_probe::Frame* root=nullptr;uint64_t mapId=0,rootId=0;};
inline Token CurrentToken(){Token t;t.map=ui_map_probe::currentMap;t.root=ui_root_probe::current;
    if(t.map)t.mapId=t.map->id;if(t.root)t.rootId=t.root->id;return t;}
struct Snapshot {
    uint64_t tick=0,mapId=0,rootId=0;uintptr_t node=0,view=0,map=0,renderer=0,bundle=0,mode=0,caller=0;
    uintptr_t context=0,backend=0,command=0;uint32_t flags=0,state=0,pipeline=0,boundPipeline=0;uint8_t aa=0,cacheValid=0;
    float cb[52]{};ui_root_probe::RenderSample render{};
};
inline Reason Capture(unsigned index,void* mode,void* bundle,void* caller,const Token& token,Snapshot& s){
    LastErrorGuard error;s.tick=GetTickCount64();s.mode=reinterpret_cast<uintptr_t>(mode);s.bundle=reinterpret_cast<uintptr_t>(bundle);
    s.caller=reinterpret_cast<uintptr_t>(caller);auto base=imageBase.load();
    auto f=ui_map_probe::currentMap;auto root=ui_root_probe::current;
    if(!base || ui_map_probe::mapTlsMismatches.load() || !f || !root || f!=token.map || root!=token.root || f->id!=token.mapId || root->id!=token.rootId ||
       f->root!=root || !root->id || f->rootId!=root->id || !f->expectedRenderer || f->expectedRenderer!=root->expectedRenderer)return Scope;
    s.mapId=f->id;s.rootId=root->id;s.node=reinterpret_cast<uintptr_t>(f->node);s.renderer=reinterpret_cast<uintptr_t>(root->expectedRenderer);
    if(index>=Count || s.caller!=base+ReturnRva)return Caller;
    uintptr_t vt=0;
    if(!Read(s.mode,vt) || vt!=base+Specs[index].table || !Member(s.mode,0x1a,s.aa) || s.aa>1)return ModeType;
    if(!Read(s.node,vt) || vt!=base+ui_map_probe::TableRva || !Member(s.node,0xc0,s.flags) || !(s.flags&2))return Node;
    uintptr_t viewMap=0;
    if(!Member(s.node,0x140,s.map) || !Member(s.node,0x158,s.view) || !s.map || !s.view ||
       !Member(s.view,8,viewMap) || viewMap!=s.map || !Read(s.map,vt) || vt!=base+ui_map_probe::MapRendererVtableRva)return MapIdentity;
    uintptr_t savedBundle=0,bundleRenderer=0,globalRenderer=0;
    if(!Member(s.map,0x88,savedBundle) || savedBundle!=s.bundle || !Read(s.bundle,bundleRenderer) || bundleRenderer!=s.renderer ||
       !Read(base+ui_map_probe::RendererPointerRva,globalRenderer) || globalRenderer!=s.renderer ||
       !Read(s.renderer,vt) || vt!=base+ui_map_probe::RendererVtableRva)return Bundle;
    // All three verified native mode functions first copy mode+38 to bundle+18.
    if(!Member(s.mode,0x38,s.pipeline) || !Member(s.bundle,0x18,s.boundPipeline) || s.pipeline!=s.boundPipeline)return Binding;
    uintptr_t mapContext=0;
    if(!Member(s.map,0x80,mapContext) || !Member(s.renderer,0x40,s.context) || !s.context || mapContext!=s.context ||
       !Member(s.renderer,0x68,s.backend) || !Member(s.backend,8,s.command) || !s.command)return Context;
    uint8_t uploadedAA=0;
    if(!Member(s.map,0x78,s.state) || s.state!=1 || !Member(s.map,0x1d0,uploadedAA) || uploadedAA!=s.aa)return MapState;
    s.render=ui_root_probe::CaptureRenderer(reinterpret_cast<void*>(s.renderer),caller,false);
    if(!s.render.valid || !ui_affine_clip::Matches(reinterpret_cast<void*>(s.renderer),s.render.matrices[0]))return Projection;
    if(!s.render.clipValid || s.render.clipTop[16]!=1)return Clip;
    float rect[4];memcpy(rect,s.render.clipTop,16);
    for(float value:rect)if(!std::isfinite(value))return Clip;
    if(rect[2]<=0 || rect[3]<=0)return Clip;
    if(!Member(s.renderer,0x420,s.cacheValid) || s.cacheValid!=1 || !Member(s.map,0xc0,s.cb) ||
       memcmp(s.cb+32,s.render.matrices[7],64))return Cache;
    return Eligible;
}
inline bool Same(const Snapshot& a,const Snapshot& b){
    return a.mapId==b.mapId && a.rootId==b.rootId && a.node==b.node && a.view==b.view && a.map==b.map &&
        a.renderer==b.renderer && a.bundle==b.bundle && a.mode==b.mode && a.context==b.context && a.backend==b.backend &&
        a.command==b.command && a.flags==b.flags && a.state==b.state && a.pipeline==b.pipeline && a.boundPipeline==b.boundPipeline &&
        a.aa==b.aa && a.cacheValid==b.cacheValid &&
        a.render.context==b.render.context && a.render.commandList==b.render.commandList && a.render.target==b.render.target &&
        a.render.layer==b.render.layer && a.render.clipData==b.render.clipData && a.render.clipCount==b.render.clipCount &&
        a.render.clipCapacity==b.render.clipCapacity && !memcmp(a.render.clipTop,b.render.clipTop,20) &&
        !memcmp(a.cb,b.cb,sizeof(a.cb)) && !memcmp(a.render.matrices,b.render.matrices,sizeof(a.render.matrices));
}
inline bool Sample(unsigned index,bool requested){
    if(!owner.load())return false;auto& state=states[index];unsigned bucket=requested?1u:0u;
    auto n=++state.samples[bucket];auto now=GetTickCount64();if(n<=4){state.ticks[bucket].store(now);return true;}
    auto previous=state.ticks[bucket].load();return now-previous>=1000 && state.ticks[bucket].compare_exchange_strong(previous,now);
}
inline void Record(unsigned index,const Snapshot& s,Reason reason,bool requested,bool didApply,const ui_map_clip_math::Result* correction=nullptr){
    LastErrorGuard error;auto module=owner.load();if(!module)return;wchar_t path[MAX_PATH]{};
    auto length=GetModuleFileNameW(module,path,MAX_PATH);auto slash=length && length<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
    if(!slash || wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ui-map-mode-probe.log"))return;
    ExclusiveLock lock(logLock);FILE* file=nullptr;if(_wfopen_s(&file,path,L"a") || !file)return;
    fprintf(file,"pid=%lu tid=%lu tick=%llu mode=%s requested=%u applied=%u reason=%s calls=%llu returns=%llu unwinds=%llu applications=%llu rejects=%llu disabled=%llu fuse=%u fuse_events=%llu root=%llu map_scope=%llu node=%llx view=%llx map_renderer=%llx renderer=%llx mode_object=%llx bundle=%llx caller=%llx context=%llx backend=%llx command=%llx map_state=%u aa=%u flags=%u clip=",
        GetCurrentProcessId(),GetCurrentThreadId(),s.tick,Specs[index].name,requested?1u:0u,didApply?1u:0u,Reasons[reason],
        states[index].calls.load(),states[index].returns.load(),states[index].unwinds.load(),applied.load(),rejected.load(),notRequested.load(),fused.load()?1u:0u,fuseEvents.load(),
        s.rootId,s.mapId,s.node,s.view,s.map,s.renderer,s.mode,s.bundle,s.caller,s.context,s.backend,s.command,s.state,s.aa,s.flags);
    for(auto v:s.render.clipTop)fprintf(file,"%02x",unsigned(v));
    fprintf(file," pipeline=%u bound_pipeline=%u",s.pipeline,s.boundPipeline);
    fprintf(file," cb=");for(unsigned i=0;i<52;++i)fprintf(file,"%s%.9g",i?",":"",s.cb[i]);
    for(unsigned m:{0u,5u,7u}){fprintf(file," %s=",ui_root_probe::MatrixNames[m]);for(unsigned i=0;i<16;++i)fprintf(file,"%s%.9g",i?",":"",s.render.matrices[m][i]);}
    if(correction){fprintf(file," corrected_mv=");for(unsigned i=0;i<16;++i)fprintf(file,"%s%.9g",i?",":"",correction->mv[i]);
        fprintf(file," corrected_planes=");for(unsigned i=0;i<16;++i)fprintf(file,"%s%.9g",i?",":"",correction->planes[i]);}
    fputc('\n',file);fclose(file);
}
inline void After(unsigned index,void* mode,void* bundle,void* caller,const Token& token){
    Controls c;{LastErrorGuard error;auto reader=controlsReader.load();if(reader)c=reader();}
    const bool requested=c.vr && c.foreground;
    const bool sampled=Sample(index,requested);if(!requested){++notRequested;if(!sampled)return;}
    Snapshot s;Reason reason=Capture(index,mode,bundle,caller,token,s);bool didApply=false;
    ui_map_clip_math::Result correction;
    if(requested && reason==Eligible){
        if(fused.load())reason=Fused;
        else{
            float rectangle[4];memcpy(rectangle,s.render.clipTop,16);
            if(!ui_map_clip_math::Compute(s.cb,s.render.matrices[5],rectangle,correction))reason=Math;
            else{
                Snapshot verify;auto again=Capture(index,mode,bundle,caller,token,verify);
                if(again!=Eligible || !Same(s,verify))reason=Changed;
                else if(auto function=upload.load()){
                    alignas(16) float local[52];memcpy(local,s.cb,sizeof(local));
                    memcpy(local,correction.mv,64);memcpy(local+32,correction.planes,64);
                    // Native uploader copies these bytes into its GPU allocation
                    // synchronously. It never retains this stack pointer. The
                    // binding persists until the next native CB upload: VR/focus
                    // loss is restored by native map Begin, not C++ scope exit.
                    try{function(reinterpret_cast<void*>(s.renderer),local,sizeof(local));++applied;didApply=true;}
                    catch(...){fused.store(true);++fuseEvents;++reasons[UploadException];++rejected;
                        Record(index,s,UploadException,true,false);throw;}
                }else reason=NoUpload;
            }
        }
    }
    if(requested && !didApply)++rejected;
    ++reasons[reason];if(sampled)Record(index,s,reason,requested,didApply,didApply?&correction:nullptr);
}
struct CallGuard {unsigned index;bool returned=false;explicit CallGuard(unsigned i):index(i){++activeCalls;++states[i].calls;}
    ~CallGuard() noexcept{LastErrorGuard error;if(returned)++states[index].returns;else ++states[index].unwinds;--activeCalls;}};
inline void Dispatch(unsigned index,void* mode,void* bundle,void* caller){
    DWORD incoming=GetLastError();CallGuard guard(index);auto token=CurrentToken();
    auto nativeCallback=states[index].original.load(std::memory_order_acquire);SetLastError(incoming);
    nativeCallback(mode,bundle); // Exactly once. Native exceptions propagate unchanged.
    DWORD outgoing=GetLastError();After(index,mode,bundle,caller,token);guard.returned=true;SetLastError(outgoing);
}
template<unsigned Index> __declspec(noinline) void Observe(void* mode,void* bundle){Dispatch(Index,mode,bundle,_ReturnAddress());}
inline void* Hook(unsigned i){static void* hooks[]={reinterpret_cast<void*>(&Observe<0>),reinterpret_cast<void*>(&Observe<1>),reinterpret_cast<void*>(&Observe<2>),
    reinterpret_cast<void*>(&Observe<3>),reinterpret_cast<void*>(&Observe<4>),reinterpret_cast<void*>(&Observe<5>),reinterpret_cast<void*>(&Observe<6>)};return hooks[i];}
inline bool Pending(){for(auto& s:states)if(s.slot)return true;return false;}
template<class Replace> DWORD Remove(Replace replace){
    DWORD result=ERROR_SUCCESS;
    for(unsigned n=Count;n>0;--n){unsigned i=n-1;auto& s=states[i];if(!s.slot)continue;
        void* value=nullptr;void* native=reinterpret_cast<void*>(s.original.load());
        if(!Read(reinterpret_cast<uintptr_t>(s.slot),value)){if(!result)result=ERROR_READ_FAULT;continue;}
        if(value==native){s.slot=nullptr;continue;}
        if(value!=Hook(i)){s.slot=nullptr;if(!result)result=ERROR_INVALID_DATA;continue;}
        DWORD status=replace(s.slot,Hook(i),native);value=nullptr;
        if(Read(reinterpret_cast<uintptr_t>(s.slot),value)){
            if(value!=Hook(i))s.slot=nullptr;
            if(!status && value!=native)status=ERROR_WRITE_FAULT;
        }else if(!status)status=ERROR_READ_FAULT;
        if(status && !result)result=status;
    }return result;
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace){
    LastErrorGuard error;ExclusiveLock lock(installationLock);
    if(Pending())return ERROR_ALREADY_EXISTS;
    if(activeCalls.load() || ui_map_probe::activeCalls.load() || ui_root_probe::activeRoots.load())return ERROR_BUSY;
    if(!base || base>UINTPTR_MAX-(ui_map_probe::RendererPointerRva+8))return ERROR_REVISION_MISMATCH;
    unsigned char uploadBytes[sizeof(UploadPrefix)]{};
    if(!Read(base+UploadRva,uploadBytes) || memcmp(uploadBytes,UploadPrefix,sizeof(UploadPrefix)))return ERROR_REVISION_MISMATCH;
    unsigned char callsite[sizeof(CallsiteBytes)]{};
    if(!Read(base+CallsiteRva,callsite) || memcmp(callsite,CallsiteBytes,sizeof(CallsiteBytes)))return ERROR_REVISION_MISMATCH;
    for(unsigned i=0;i<Count;++i){const auto& s=Specs[i];unsigned char prefix[sizeof(ModePrefix)]{};uintptr_t locator=0,fn=0;uint32_t col[6]{};
        const uint32_t expected[]={1,0,0,s.type,s.hierarchy,static_cast<uint32_t>(s.col)};
        if(!Read(base+s.function,prefix) || memcmp(prefix,ModePrefix,sizeof(prefix)) || !Read(base+s.table-8,locator) || locator!=base+s.col ||
           !Read(base+s.col,col) || memcmp(col,expected,sizeof(col)) || !Read(base+s.table+8,fn) || fn!=base+s.function)return ERROR_REVISION_MISMATCH;
    }
    imageBase.store(base);owner.store(module);upload.store(reinterpret_cast<Upload>(base+UploadRva));fused.store(false);
    for(unsigned i=0;i<Count;++i)states[i].original.store(reinterpret_cast<Mode>(base+Specs[i].function),std::memory_order_release);
    for(unsigned i=0;i<Count;++i){auto& s=states[i];s.slot=reinterpret_cast<void* volatile*>(base+Specs[i].table+8);
        DWORD status=replace(s.slot,reinterpret_cast<void*>(s.original.load()),Hook(i));void* actual=nullptr;
        if(!status && (!Read(reinterpret_cast<uintptr_t>(s.slot),actual) || actual!=Hook(i)))status=ERROR_WRITE_FAULT;
        if(status){DWORD rollback=Remove(replace);return rollback?rollback:status;}}
    return ERROR_SUCCESS;
}
template<class Replace> DWORD Stop(Replace replace){LastErrorGuard error;ExclusiveLock lock(installationLock);return Remove(replace);}
} // namespace ui_map_mode_probe
