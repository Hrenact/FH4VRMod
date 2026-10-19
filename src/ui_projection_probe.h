#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include "ui_projection_test.h"
#include "runtime_projection.h"
#include "stereo_trial.h"
#include "ui_clip_projection.h"
#include "ui_affine_clip.h"
#include "ui_pass_probe.h"
namespace ui_projection_probe {
using Setter=void(*)(void*,const float*);
using Prepare=uintptr_t(*)(void*);
inline Prepare originalPrepare=nullptr;
inline void* volatile* prepareSlot=nullptr;
inline thread_local void* trialObject=nullptr;
inline HoldYawGate clipGate;
inline std::atomic<unsigned long long> prepareCalls{0},planesCorrected{0},planesRejected{0};
inline std::atomic<unsigned long long> clipKeyActive{0},clipDrawAttempts{0},clipDisabled{0};
using Text52=void(*)(void*,void*,void*);
using Text54=void(*)(void*,const wchar_t*,float,float,const void*,float,void*);
inline Text52 originalText52=nullptr;
inline Text54 originalText54=nullptr;
inline void* volatile* text52Slot=nullptr;
inline void* volatile* text54Slot=nullptr;
inline std::atomic<unsigned long long> text52Calls{0},text54Calls{0};
inline bool AffineClipActive() {return stereo_trial::Offset(true,GetTickCount64())!=0;}
inline void ObserveText52(void* object,void* geometry,void* text) {
    DWORD incoming=GetLastError(),outgoing=0;
    {
        ui_affine_clip::TextScope scope(object,AffineClipActive());
#ifdef FH4VR_UI_ROOT_PROBE
        ui_root_probe::ObserveEvent("text52",object,_ReturnAddress());
#endif
        SetLastError(incoming);originalText52(object,geometry,text);outgoing=GetLastError();
    }
    ++text52Calls;SetLastError(outgoing);
}
inline void ObserveText54(void* object,const wchar_t* text,float x,float y,const void* color,float size,void* font) {
    DWORD incoming=GetLastError(),outgoing=0;
    {
        ui_affine_clip::TextScope scope(object,AffineClipActive());
#ifdef FH4VR_UI_ROOT_PROBE
        ui_root_probe::ObserveEvent("text54",object,_ReturnAddress());
#endif
        SetLastError(incoming);originalText54(object,text,x,y,color,size,font);outgoing=GetLastError();
    }
    ++text54Calls;SetLastError(outgoing);
}
inline bool ClipTrialActive(bool foreground,bool pressed,uint64_t now,bool plane) {
    // The physical key latch must survive unrelated UI projection setters.
    bool held=clipGate.angle(foreground,pressed,now)!=0;
    if(held)++clipKeyActive;
    return held && plane;
}
inline bool CorrectPreparedPlanes(void* object) {
    struct Stack {uintptr_t data;uint32_t capacity,count;} stack{};
    SIZE_T got=0;auto bytes=static_cast<char*>(object);
    if(!ReadProcessMemory(GetCurrentProcess(),bytes+0x270,&stack,sizeof(stack),&got)||got!=sizeof(stack)||
       !stack.data||!stack.count||stack.count>4096||stack.count>stack.capacity)return false;
    unsigned char rectangle[20];float matrix[16],planes[16];
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(stack.data+(stack.count-1)*20),rectangle,20,&got)||got!=20)return false;
    // Disabled clip keeps the game's own sentinel; never replace it with a synthetic rectangle.
    if(rectangle[16]==0){++clipDisabled;return true;}
    if(rectangle[16]!=1)return false;
    if(!ReadProcessMemory(GetCurrentProcess(),bytes+0x3e0,matrix,sizeof(matrix),&got)||got!=sizeof(matrix))return false;
    float rect[4];memcpy(rect,rectangle,16);
    if(!ui_clip_projection::RectPlanes(matrix,rect,planes))return false;
    // Slot 44 has just copied cached planes (0x470) to draw constants (0x5b0).
    // Change only this draw's constants, keeping the game's cached planes/clip stack intact.
    memcpy(bytes+0x5b0,planes,sizeof(planes));++planesCorrected;return true;
}
inline uintptr_t ObservePrepare(void* object) {
    auto result=originalPrepare(object);DWORD saved=GetLastError();++prepareCalls;
#ifdef FH4VR_UI_PASS_PROBE
    ui_pass_probe::Record(object,true,prepareCalls.load(),_ReturnAddress());
#endif
    if(trialObject==object){++clipDrawAttempts;if(!CorrectPreparedPlanes(object))++planesRejected;}
#ifdef FH4VR_UI_AFFINE_CLIP
    ui_affine_clip::Prepared(object,AffineClipActive());
#endif
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::ObserveEvent("prepare",object,_ReturnAddress());
#endif
    SetLastError(saved);return result;
}
inline Setter original=nullptr;
inline Setter refreshClip=nullptr;
inline std::atomic<unsigned long long> clipRefreshes{0};
inline bool RefreshCurrentClip(void* object) {
    if(!refreshClip)return false;
    struct Stack {uintptr_t data;uint32_t capacity,count;} stack{};
    SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),static_cast<char*>(object)+0x270,&stack,sizeof(stack),&got)||got!=sizeof(stack)||
       !stack.data||!stack.count||stack.count>4096||stack.count>stack.capacity)return false;
    alignas(16) unsigned char rectangle[20]{};
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(stack.data+(stack.count-1)*20),rectangle,20,&got)||got!=20)return false;
    float values[4];memcpy(values,rectangle,16);
    for(float value:values)if(!std::isfinite(value))return false;
    if(rectangle[16]>1)return false;
    refreshClip(object,reinterpret_cast<const float*>(rectangle));++clipRefreshes;return true;
}
inline HMODULE owner=nullptr;
inline std::atomic<unsigned long long> calls{0};
inline std::atomic<unsigned long long> lastLogTick{0};
inline std::atomic<unsigned long long> applied{0};
inline void* volatile* slot=nullptr;
// Diagnostic provenance only. These buckets never decide whether a projection
// is modified; they expose effect calls hidden by the old global log throttle.
inline uintptr_t sourceBase=0;
inline std::atomic<unsigned long long> sourceCalls[4]{},sourceChanged[4]{};
inline constexpr const char* SourceNames[]={"source_main","source_effect_copy","source_effect_blur","source_other"};
inline constexpr const char* SourceChangedNames[]={"source_main_changed","source_effect_copy_changed","source_effect_blur_changed","source_other_changed"};
inline unsigned SourceIndex(void* caller) {
    auto address=reinterpret_cast<uintptr_t>(caller);
    if(!sourceBase || address<sourceBase)return 3;
    switch(address-sourceBase) {
    case 0x3abd2d1:return 0;
    case 0x3deffc1:return 1;
    case 0x3df0e41:return 2;
    default:return 3;
    }
}
inline void Record(const char* event,unsigned long long count,const float* m,void* caller) {
    if(!owner)return;
    DWORD saved=GetLastError();wchar_t path[MAX_PATH]{};
    DWORD length=GetModuleFileNameW(owner,path,MAX_PATH);
    if(length && length<MAX_PATH) {
        auto tail=wcsrchr(path,L'\\');
        if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"ui-projection-probe.log")) {
            FILE* f=nullptr;
            if(!_wfopen_s(&f,path,L"a") && f) {
                fprintf(f,"pid=%lu tick=%llu event=%s calls=%llu caller=%p",GetCurrentProcessId(),GetTickCount64(),event,count,caller);
                if(m)for(unsigned i=0;i<16;++i)fprintf(f," m%u=%.9g",i,m[i]);
                fputc('\n',f);fclose(f);
            }
        }
    }
    SetLastError(saved);
}
inline void Observe(void* object,const float* matrix) {
    void* caller=_ReturnAddress();
    unsigned long long count=++calls;
    DWORD incomingError=GetLastError();DWORD foregroundPid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    // Previous refresh/depth experiments remain disabled; projective clip trial is separate.
    bool active=false;
#ifdef FH4VR_UI_CLIP_EXPERIMENT
    active=ui_projection_test::gate.angle(foregroundPid==GetCurrentProcessId(),
        (GetAsyncKeyState(VK_F11)&0x8000)!=0,GetTickCount64())!=0;
#endif
    uint64_t size=ui_projection_test::dimensions.load();float copy[16];
    const float eye=stereo_trial::Offset(foregroundPid==GetCurrentProcessId(),GetTickCount64());
    const bool plane=eye!=0 && ui_projection_test::Plane(matrix,copy,
        static_cast<uint32_t>(size>>32),static_cast<uint32_t>(size),runtime_projection::verticalRadians.load(),eye,ui_smoothing::Read());
    trialObject=nullptr;
#ifdef FH4VR_UI_PROJECTIVE_CLIP_TRIAL
    if(ClipTrialActive(foregroundPid==GetCurrentProcessId(),
        (GetAsyncKeyState(VK_F11)&0x8000)!=0,GetTickCount64(),plane))trialObject=object;
#endif
    bool changed=plane || (eye==0 && active && ui_projection_test::Shrink(matrix,copy,static_cast<uint32_t>(size>>32),static_cast<uint32_t>(size)));
    SetLastError(incomingError);
    original(object,changed?copy:matrix);DWORD saved=GetLastError();
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::ObserveEvent("setter",object,caller,changed);
#endif
#ifdef FH4VR_UI_AFFINE_CLIP
    // Draws may execute on another thread. Match the actual renderer projection,
    // not the setter's thread-local state or a texture size/category guess.
    if(plane)ui_affine_clip::Remember(object,copy);
#endif
#ifdef FH4VR_UI_PASS_PROBE
    ui_pass_probe::Destination(object,3,caller);
#endif
    if(plane && active)RefreshCurrentClip(object);
    if(changed)++applied;
    unsigned source=SourceIndex(caller);
    auto sourceCount=++sourceCalls[source];
    auto changedCount=changed?++sourceChanged[source]:sourceChanged[source].load();
    if(sourceCount==1 || (changed && changedCount==1))Record(SourceNames[source],sourceCount,matrix,caller);
    auto now=GetTickCount64();auto previous=lastLogTick.load();
    bool periodic=now-previous>=1000 && lastLogTick.compare_exchange_strong(previous,now);
    if(count<=8 || periodic){Record(plane?(active?"projection_clip_refresh_trial":"projection_plane"):changed?"projection_shrunk":"projection",count,matrix,caller);
        if(periodic){Record("applied_total",applied.load(),nullptr,nullptr);Record("clip_refresh_total",clipRefreshes.load(),nullptr,nullptr);
            for(unsigned i=0;i<4;++i){Record(SourceNames[i],sourceCalls[i].load(),nullptr,nullptr);Record(SourceChangedNames[i],sourceChanged[i].load(),nullptr,nullptr);}
            Record("prepare_total",prepareCalls.load(),nullptr,nullptr);Record("projective_clip_total",planesCorrected.load(),nullptr,nullptr);
            Record("clip_key_active",clipKeyActive.load(),nullptr,nullptr);Record("clip_draw_attempts",clipDrawAttempts.load(),nullptr,nullptr);
            Record("clip_disabled",clipDisabled.load(),nullptr,nullptr);
            Record("projective_clip_rejected",planesRejected.load(),nullptr,nullptr);
#ifdef FH4VR_UI_AFFINE_CLIP
            Record("affine_geometry",ui_affine_clip::geometryApplied.load(),nullptr,nullptr);
            Record("affine_text",ui_affine_clip::textApplied.load(),nullptr,nullptr);
            Record("affine_rejected",ui_affine_clip::rejected.load(),nullptr,nullptr);
            Record("affine_disabled",ui_affine_clip::disabled.load(),nullptr,nullptr);
            Record("affine_unmatched",ui_affine_clip::unmatched.load(),nullptr,nullptr);
            Record("affine_cache_changed",ui_affine_clip::cacheChanged.load(),nullptr,nullptr);
            Record("text52_calls",text52Calls.load(),nullptr,nullptr);
            Record("text54_calls",text54Calls.load(),nullptr,nullptr);
#endif
        }}
    SetLastError(saved);
}
template<class Replace> void StopClipHooks(Replace replace) {
    if(text54Slot)replace(text54Slot,reinterpret_cast<void*>(&ObserveText54),reinterpret_cast<void*>(originalText54));
    if(text52Slot)replace(text52Slot,reinterpret_cast<void*>(&ObserveText52),reinterpret_cast<void*>(originalText52));
    if(prepareSlot)replace(prepareSlot,reinterpret_cast<void*>(&ObservePrepare),reinterpret_cast<void*>(originalPrepare));
    if(slot)replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));
}
template<size_t N> bool ValidateHook(uintptr_t base,unsigned index,uintptr_t rva,const unsigned char(&expected)[N]) {
    unsigned char actual[N]{};void* observed=nullptr;
    return ui_affine_clip::Read(reinterpret_cast<void*>(base+rva),actual) && !memcmp(actual,expected,N) &&
        ui_affine_clip::Read(reinterpret_cast<void*>(base+0x5c7ed70+index*sizeof(void*)),observed) && observed==reinterpret_cast<void*>(base+rva);
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace) {
    owner=module;sourceBase=base;
#ifdef FH4VR_UI_AFFINE_CLIP
    const unsigned char expected52[]={0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18};
    const unsigned char expected54[]={0x48,0x85,0xd2,0x0f,0x84,0xc2,0x02,0x00,0x00,0x48,0x8b,0xc4};
    if(!ValidateHook(base,52,0x3e08580,expected52)||!ValidateHook(base,54,0x3e082b0,expected54)){
        Record("text_revision_mismatch",0,nullptr,nullptr);return ERROR_REVISION_MISMATCH;
    }
#endif
#ifdef FH4VR_UI_CLIP_EXPERIMENT
    const unsigned char clipExpected[]={0x48,0x8b,0xc4,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18};
    unsigned char clipBytes[sizeof(clipExpected)]{};SIZE_T clipGot=0;
    auto clipCandidate=reinterpret_cast<Setter>(base+0x3e0b100);
    if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(clipCandidate),clipBytes,sizeof(clipBytes),&clipGot)&&
       clipGot==sizeof(clipBytes)&&!memcmp(clipBytes,clipExpected,sizeof(clipBytes)))refreshClip=clipCandidate;
    else {Record("clip_revision_mismatch",0,nullptr,nullptr);return ERROR_REVISION_MISMATCH;}
#endif
    auto candidate=reinterpret_cast<Setter>(base+0x3e0b0c0);
    auto address=reinterpret_cast<void* volatile*>(base+0x5c7ed70+11*sizeof(void*));
    const unsigned char expected[]={0x0f,0x10,0x02,0x0f,0x11,0x81,0x60,0x03,0x00,0x00};
    unsigned char bytes[sizeof(expected)]{};SIZE_T got=0;void* observed=nullptr;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(candidate),bytes,sizeof(bytes),&got) ||
       got!=sizeof(bytes) || memcmp(bytes,expected,sizeof(bytes)) ||
       !ReadProcessMemory(GetCurrentProcess(),const_cast<void**>(address),&observed,sizeof(observed),&got) ||
       got!=sizeof(observed) || observed!=reinterpret_cast<void*>(candidate)) {
        Record("revision_mismatch",0,nullptr,nullptr);return ERROR_REVISION_MISMATCH;
    }
    original=candidate;slot=address;
    DWORD status=replace(slot,reinterpret_cast<void*>(original),reinterpret_cast<void*>(&Observe));
#if defined(FH4VR_UI_PROJECTIVE_CLIP_TRIAL) || defined(FH4VR_UI_PASS_PROBE) || defined(FH4VR_UI_AFFINE_CLIP)
    if(status==ERROR_SUCCESS){
        const unsigned char expectedPrepare[]={0x48,0x8b,0xc4,0x53,0x48,0x81,0xec,0x50,0x01,0x00,0x00};
        unsigned char prefix[sizeof(expectedPrepare)]{};void* observedPrepare=nullptr;
        auto prepareAddress=reinterpret_cast<void* volatile*>(base+0x5c7ed70+44*sizeof(void*));
        auto prepareCandidate=reinterpret_cast<Prepare>(base+0x3e0bcb0);
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(prepareCandidate),prefix,sizeof(prefix),&got)||got!=sizeof(prefix)||
           memcmp(prefix,expectedPrepare,sizeof(prefix))||
           !ReadProcessMemory(GetCurrentProcess(),const_cast<void**>(prepareAddress),&observedPrepare,sizeof(void*),&got)||got!=sizeof(void*)||
           observedPrepare!=reinterpret_cast<void*>(prepareCandidate))status=ERROR_REVISION_MISMATCH;
        else {originalPrepare=prepareCandidate;prepareSlot=prepareAddress;
            status=replace(prepareSlot,reinterpret_cast<void*>(originalPrepare),reinterpret_cast<void*>(&ObservePrepare));}
        Record(status==ERROR_SUCCESS?"prepare_enabled":"prepare_attach_failed",status,nullptr,nullptr);
        if(status!=ERROR_SUCCESS)replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));
    }
#endif
#ifdef FH4VR_UI_AFFINE_CLIP
    if(status==ERROR_SUCCESS){
        originalText52=reinterpret_cast<Text52>(base+0x3e08580);
        originalText54=reinterpret_cast<Text54>(base+0x3e082b0);
        text52Slot=reinterpret_cast<void* volatile*>(base+0x5c7ed70+52*sizeof(void*));
        text54Slot=reinterpret_cast<void* volatile*>(base+0x5c7ed70+54*sizeof(void*));
        status=replace(text52Slot,reinterpret_cast<void*>(originalText52),reinterpret_cast<void*>(&ObserveText52));
        if(status==ERROR_SUCCESS)status=replace(text54Slot,reinterpret_cast<void*>(originalText54),reinterpret_cast<void*>(&ObserveText54));
        Record(status==ERROR_SUCCESS?"text_clip_enabled":"text_clip_attach_failed",status,nullptr,nullptr);
    }
#endif
#ifdef FH4VR_UI_PASS_PROBE
    if(status==ERROR_SUCCESS){
        auto passStatus=ui_pass_probe::Start(base,module,replace);
        Record(passStatus==ERROR_SUCCESS?"ui_pass_enabled":"ui_pass_attach_failed",passStatus,nullptr,nullptr);
        if(passStatus!=ERROR_SUCCESS)status=passStatus;
    }
#endif
    if(status!=ERROR_SUCCESS)StopClipHooks(replace);
    Record(status==ERROR_SUCCESS?"enabled":"attach_failed",status,nullptr,nullptr);return status;
}
}
