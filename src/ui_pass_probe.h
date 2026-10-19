#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <intrin.h>
#ifdef FH4VR_UI_ROOT_PROBE
#include "ui_root_probe.h"
#endif
#ifdef FH4VR_UI_LAYER_CAPTURE
#include "ui_layer_capture.h"
#include "ui_isolated_capture.h"
#endif
namespace ui_pass_probe {
using Begin=uintptr_t(*)(void*);
inline Begin original=nullptr;
inline Begin originalEnd=nullptr;
inline Begin originalLayerEnd=nullptr;
// Slot 25: rectangle draw with two additional integer/pointer argument slots.
using Draw=uintptr_t(*)(void*,void*,uintptr_t,uintptr_t);
inline Draw originalDraw=nullptr;
inline void* volatile* drawSlot=nullptr;
inline std::atomic<unsigned long long> drawCalls{0};
inline void* volatile* slot=nullptr;
inline void* volatile* endSlot=nullptr;
inline void* volatile* layerEndSlot=nullptr;
inline std::atomic<unsigned long long> layerEnds{0},lastLayer{0};
inline HMODULE owner=nullptr;
inline std::atomic<unsigned long long> calls{0},lastBegin{0},lastDraw{0};
inline std::atomic<unsigned long long> ends{0},lastEnd{0},pairedEnds{0},unpairedEnds{0},overlappingBegins{0};
inline thread_local void* openObject=nullptr;
template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T n=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&n)&&n==sizeof(T);
}
// Snapshot each distinct destination/caller, rather than one globally sampled draw.
// Raw descriptors are read immediately; no COM calls or resource retention here.
struct DestinationKey {uintptr_t object=0,target=0,caller=0;int phase=0;};
inline std::mutex destinationLock;
inline DestinationKey destinationKeys[128]{};
inline unsigned destinationCount=0;
inline UINT64 destinationWindow=0;
inline bool SameDestination(const DestinationKey& a,const DestinationKey& b) {
    return a.object==b.object && a.target==b.target && a.caller==b.caller && a.phase==b.phase;
}
inline void Destination(void* object,int phase,void* caller) {
    if(!owner)return;
    DWORD saved=GetLastError();
    std::lock_guard<std::mutex> guard(destinationLock);
    auto now=GetTickCount64();if(now-destinationWindow>=10000){destinationWindow=now;destinationCount=0;}
    uintptr_t obj=reinterpret_cast<uintptr_t>(object),target=0;
    Read(obj+0x70,target);
    DestinationKey key{obj,target,reinterpret_cast<uintptr_t>(caller),phase};
    bool duplicate=false;for(unsigned i=0;i<destinationCount;++i)if(SameDestination(key,destinationKeys[i]))duplicate=true;
    if(duplicate || destinationCount==128){SetLastError(saved);return;}
    destinationKeys[destinationCount++]=key;
    uintptr_t backend=0,context=0,defaultTarget=0,layer=0,resource=0;
    Read(obj+0x68,backend);Read(backend+8,context);Read(backend+0x10,defaultTarget);Read(obj+0x650,layer);
    unsigned dimensions[2]{},format=0;unsigned char state=0;
    Read(target,resource);Read(target+0x80,dimensions);Read(target+0x78,format);Read(target+0x88,state);
    float projection[16]{},local[16]{};Read(obj+0x360,projection);Read(obj+0x3a0,local);
    wchar_t path[MAX_PATH]{};auto length=GetModuleFileNameW(owner,path,MAX_PATH);
    if(length && length<MAX_PATH){auto tail=wcsrchr(path,L'\\');
        if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"ui-destination-probe.log")){
            FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){
                fprintf(f,"pid=%lu tid=%lu tick=%llu phase=%d object=%p caller=%p target=%p resource=%p default_target=%p layer=%p context=%p size=%ux%u format=%u state=%u",
                    GetCurrentProcessId(),GetCurrentThreadId(),now,phase,object,caller,
                    reinterpret_cast<void*>(target),reinterpret_cast<void*>(resource),reinterpret_cast<void*>(defaultTarget),
                    reinterpret_cast<void*>(layer),reinterpret_cast<void*>(context),dimensions[0],dimensions[1],format,unsigned(state));
                for(unsigned i=0;i<16;++i)fprintf(f," p%u=%.9g",i,projection[i]);
                for(unsigned i=0;i<16;++i)fprintf(f," l%u=%.9g",i,local[i]);
                fputc('\n',f);fclose(f);
            }
        }
    }
    SetLastError(saved);
}
inline void Record(void* object,int phase,unsigned long long count,void* caller=nullptr) {
    if(!owner)return;
    if(phase==1)Destination(object,phase,caller);
    auto now=GetTickCount64();auto& tick=phase==2?lastEnd:phase==1?lastDraw:lastBegin;auto old=tick.load();
    if(count>8 && (now-old<1000 || !tick.compare_exchange_strong(old,now)))return;
    DWORD saved=GetLastError();uintptr_t obj=reinterpret_cast<uintptr_t>(object),backend=0,context=0,target=0,defaultTarget=0;
    Read(obj+0x68,backend);if(backend){Read(backend+8,context);Read(backend+0x10,defaultTarget);}
    Read(obj+0x70,target); // Set by native target-binding function 0x3e096d0.
    unsigned dimensions[2]{};if(target)Read(target+0x80,dimensions);
    uintptr_t attachments[5]{};for(unsigned i=0;i<5;++i)if(target)Read(target+0x18+i*0x10,attachments[i]);
    float projection[16]{},local[16]{};Read(obj+0x360,projection);Read(obj+0x320,local);
    uintptr_t targetVtable=0,contextVtable=0;Read(target,targetVtable);Read(context,contextVtable);
    wchar_t path[MAX_PATH]{};auto length=GetModuleFileNameW(owner,path,MAX_PATH);
    if(length && length<MAX_PATH){auto tail=wcsrchr(path,L'\\');
        if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"ui-pass-probe.log")){
            FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){
                fprintf(f,"pid=%lu tid=%lu tick=%llu phase=%s calls=%llu object=%p backend=%p context=%p context_vtable=%p target=%p target_vtable=%p default_target=%p size=%ux%u",
                    GetCurrentProcessId(),GetCurrentThreadId(),now,phase==2?"end_enter":phase==1?"prepare":"begin_return",count,object,
                    reinterpret_cast<void*>(backend),reinterpret_cast<void*>(context),reinterpret_cast<void*>(contextVtable),
                    reinterpret_cast<void*>(target),reinterpret_cast<void*>(targetVtable),reinterpret_cast<void*>(defaultTarget),dimensions[0],dimensions[1]);
                for(unsigned i=0;i<5;++i)fprintf(f," attachment%u=%p",i,reinterpret_cast<void*>(attachments[i]));
                fprintf(f," begins=%llu ends=%llu paired=%llu unpaired=%llu overlap=%llu",calls.load(),ends.load(),pairedEnds.load(),unpairedEnds.load(),overlappingBegins.load());
                for(unsigned i=0;i<16;++i)fprintf(f," p%u=%.9g",i,projection[i]);
                for(unsigned i=0;i<16;++i)fprintf(f," l%u=%.9g",i,local[i]);
                fputc('\n',f);fclose(f);
            }
        }
    }
    SetLastError(saved);
}
inline uintptr_t Observe(void* object) {
    auto result=original(object);auto saved=GetLastError();if(openObject)++overlappingBegins;openObject=object;
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::ObserveEvent("begin",object,_ReturnAddress());
#endif
    auto n=++calls;Record(object,0,n);SetLastError(saved);return result;
}
inline uintptr_t ObserveEnd(void* object) {
    auto saved=GetLastError();auto n=++ends;
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::ObserveEvent("end",object,_ReturnAddress());
#endif
    if(openObject==object)++pairedEnds;else ++unpairedEnds;
    openObject=nullptr;Record(object,2,n);SetLastError(saved);return originalEnd(object);
}
inline uintptr_t ObserveDraw(void* object,void* batch,uintptr_t arg3,uintptr_t arg4) {
    uintptr_t result=0;DWORD incoming=GetLastError(),saved=incoming;bool isolated=false;
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::ObserveEvent("draw",object,_ReturnAddress());
#endif
#if defined(FH4VR_UI_LAYER_CAPTURE) && defined(FH4VR_UI_DESTINATION_CAPTURE)
    if(ui_layer_capture::remaining.load()){
        uintptr_t obj=reinterpret_cast<uintptr_t>(object),target=0,backend=0,context=0,resource=0;
        uintptr_t rtv=0,secondRtv=0,dsv=0;unsigned dimensions[2]{},format=0;unsigned char state=255;
        if(Read(obj+0x70,target) && target && Read(target,resource) && resource &&
           Read(target+0x18,rtv) && rtv && Read(target+0x28,secondRtv) && !secondRtv && Read(target+0x38,dsv) && !dsv &&
           Read(target+0x80,dimensions) && dimensions[0] && dimensions[1] && dimensions[0]<=4096 && dimensions[1]<=4096 &&
           Read(target+0x78,format) && format==28 && Read(target+0x88,state) && state==0 &&
           Read(obj+0x68,backend) && backend && Read(backend+8,context) && context){
            isolated=ui_isolated_capture::Try(reinterpret_cast<ID3D12GraphicsCommandList*>(context),
                reinterpret_cast<ID3D12Resource*>(resource),{rtv},[&](){
                    SetLastError(incoming);result=originalDraw(object,batch,arg3,arg4);saved=GetLastError();
                });
        }else ui_layer_capture::Gate("skip_isolation_target");
    }
#endif
    if(!isolated){SetLastError(incoming);result=originalDraw(object,batch,arg3,arg4);saved=GetLastError();}
    ++drawCalls;
    Destination(object,5,_ReturnAddress());
#if defined(FH4VR_UI_LAYER_CAPTURE) && defined(FH4VR_UI_LEGACY_DESTINATION_CAPTURE)
    if(ui_layer_capture::remaining.load()){
        ++ui_layer_capture::callbackCount;
        uintptr_t obj=reinterpret_cast<uintptr_t>(object),target=0,backend=0,context=0,resource=0;
        unsigned dimensions[2]{},format=0;unsigned char state=255;
        if(!Read(obj+0x70,target) || !target || !Read(target,resource) || !resource ||
           !Read(target+0x80,dimensions) || !Read(target+0x78,format) || !Read(target+0x88,state))
            ui_layer_capture::Gate("skip_draw_target_read");
        else if(format!=28 || state!=0 || !dimensions[0] || !dimensions[1] || dimensions[0]>4096 || dimensions[1]>4096)
            ui_layer_capture::Gate("skip_draw_target_state",state);
        else if(!Read(obj+0x68,backend) || !backend || !Read(backend+8,context) || !context)
            ui_layer_capture::Gate("skip_draw_context");
        else{
            auto hr=ui_layer_capture::Record(reinterpret_cast<ID3D12GraphicsCommandList*>(context),
                reinterpret_cast<ID3D12Resource*>(resource),D3D12_RESOURCE_STATE_RENDER_TARGET,true);
            if(FAILED(hr)){ui_layer_capture::remaining=0;ui_layer_capture::Log("draw_record_failed",0,hr);}
        }
    }
#endif
    SetLastError(saved);return result;
}
inline uintptr_t ObserveLayerEnd(void* object) {
    auto result=originalLayerEnd(object);DWORD saved=GetLastError();auto n=++layerEnds;
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::ObserveEvent("layer_end",object,_ReturnAddress());
#endif
#if defined(FH4VR_UI_LAYER_CAPTURE) && !defined(FH4VR_UI_DESTINATION_CAPTURE)
    if(ui_layer_capture::remaining.load()){
        ++ui_layer_capture::callbackCount;
        uintptr_t texture=0,resource=0,backend=0,context=0;unsigned char shaderState=0;
        if(!result)ui_layer_capture::Gate("skip_null_wrapper");
        else if(!Read(result+0x18,texture) || !texture)ui_layer_capture::Gate("skip_texture_read");
        else if(!Read(texture,resource) || !resource)ui_layer_capture::Gate("skip_resource_read");
        else if(!Read(texture+0x88,shaderState))ui_layer_capture::Gate("skip_state_read");
        else if(shaderState!=1)ui_layer_capture::Gate("skip_shader_state",shaderState);
        else if(!Read(reinterpret_cast<uintptr_t>(object)+0x68,backend) || !backend || !Read(backend+8,context) || !context)ui_layer_capture::Gate("skip_context_read");
        else{
            auto hr=ui_layer_capture::Record(reinterpret_cast<ID3D12GraphicsCommandList*>(context),reinterpret_cast<ID3D12Resource*>(resource));
            if(FAILED(hr)){ui_layer_capture::remaining=0;ui_layer_capture::Log("record_failed",0,hr);}
        }
    }
#endif
    if(owner){auto now=GetTickCount64();auto previous=lastLayer.load();
        if(n<=16 || (now-previous>=1000 && lastLayer.compare_exchange_strong(previous,now))){
            uintptr_t texture=0,parent=0;float bounds[4]{};unsigned dimensions[2]{};
            if(result){Read(result+0x18,texture);Read(result+0x20,parent);Read(result+8,bounds);}
            if(texture)Read(texture+0x80,dimensions);
            // The layer descriptor is short-lived. Copy it here, not later using stale log pointers.
            unsigned char descriptor[0x90]{};bool descriptorValid=Read(texture,descriptor);
            wchar_t path[MAX_PATH]{};auto length=GetModuleFileNameW(owner,path,MAX_PATH);
            if(length && length<MAX_PATH){auto tail=wcsrchr(path,L'\\');
                if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"ui-layer-probe.log")){
                    FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a") && f){
                        fprintf(f,"pid=%lu tid=%lu tick=%llu calls=%llu object=%p wrapper=%p texture=%p parent=%p size=%ux%u bounds=%.9g,%.9g,%.9g,%.9g",
                            GetCurrentProcessId(),GetCurrentThreadId(),now,n,object,reinterpret_cast<void*>(result),
                            reinterpret_cast<void*>(texture),reinterpret_cast<void*>(parent),dimensions[0],dimensions[1],bounds[0],bounds[1],bounds[2],bounds[3]);
                        fprintf(f," descriptor_valid=%u descriptor=",descriptorValid?1u:0u);
                        if(descriptorValid)for(auto byte:descriptor)fprintf(f,"%02x",static_cast<unsigned>(byte));
                        // Pointer-shaped words are read only; do not call unverified COM interfaces.
                        if(descriptorValid)for(unsigned offset:{0u,8u,0x10u,0x48u,0x50u,0x58u,0x60u}){
                            uintptr_t candidate=0;memcpy(&candidate,descriptor+offset,sizeof(candidate));
                            uintptr_t words[4]{};
                            if(candidate>=0x10000 && candidate<0x0000800000000000ULL && Read(candidate,words)){
                                fprintf(f," nested_%x=%p,%p,%p,%p",offset,reinterpret_cast<void*>(words[0]),
                                    reinterpret_cast<void*>(words[1]),reinterpret_cast<void*>(words[2]),reinterpret_cast<void*>(words[3]));
                            }
                        }
                        fputc('\n',f);
                        fclose(f);
                    }
                }
            }
        }
    }
    SetLastError(saved);return result;
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace) {
    owner=module;auto candidate=reinterpret_cast<Begin>(base+0x3dfd390);
#ifdef FH4VR_UI_LAYER_CAPTURE
    ui_layer_capture::owner=module;
#endif
    auto address=reinterpret_cast<void* volatile*>(base+0x5c7ed70+2*sizeof(void*));
    const unsigned char expected[]={0x40,0x53,0x48,0x83,0xec,0x50};
    unsigned char actual[sizeof(expected)]{};void* observed=nullptr;
    if(!Read(reinterpret_cast<uintptr_t>(candidate),actual)||memcmp(actual,expected,sizeof(expected))||
       !Read(reinterpret_cast<uintptr_t>(address),observed)||observed!=reinterpret_cast<void*>(candidate))return ERROR_REVISION_MISMATCH;
    auto endCandidate=reinterpret_cast<Begin>(base+0x3e034f0);
    auto endAddress=reinterpret_cast<void* volatile*>(base+0x5c7ed70+3*sizeof(void*));
    const unsigned char endExpected[]={0xc2,0,0};unsigned char endBytes[3]{};void* endObserved=nullptr;
    if(!Read(reinterpret_cast<uintptr_t>(endCandidate),endBytes)||memcmp(endBytes,endExpected,3)||
       !Read(reinterpret_cast<uintptr_t>(endAddress),endObserved)||endObserved!=reinterpret_cast<void*>(endCandidate))return ERROR_REVISION_MISMATCH;
    auto layerCandidate=reinterpret_cast<Begin>(base+0x3e018b0);
    auto layerAddress=reinterpret_cast<void* volatile*>(base+0x5c7ed70+5*sizeof(void*));
    const unsigned char layerExpected[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    unsigned char layerBytes[sizeof(layerExpected)]{};void* layerObserved=nullptr;
    if(!Read(reinterpret_cast<uintptr_t>(layerCandidate),layerBytes)||memcmp(layerBytes,layerExpected,sizeof(layerExpected))||
       !Read(reinterpret_cast<uintptr_t>(layerAddress),layerObserved)||layerObserved!=reinterpret_cast<void*>(layerCandidate))return ERROR_REVISION_MISMATCH;
    original=candidate;slot=address;originalEnd=endCandidate;endSlot=endAddress;
    auto drawCandidate=reinterpret_cast<Draw>(base+0x3e0b310);
    auto drawAddress=reinterpret_cast<void* volatile*>(base+0x5c7ed70+25*sizeof(void*));
    const unsigned char drawExpected[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10};
    unsigned char drawBytes[sizeof(drawExpected)]{};void* drawObserved=nullptr;
    if(!Read(reinterpret_cast<uintptr_t>(drawCandidate),drawBytes)||memcmp(drawBytes,drawExpected,sizeof(drawExpected))||
       !Read(reinterpret_cast<uintptr_t>(drawAddress),drawObserved)||drawObserved!=reinterpret_cast<void*>(drawCandidate))return ERROR_REVISION_MISMATCH;
    auto status=replace(slot,reinterpret_cast<void*>(original),reinterpret_cast<void*>(&Observe));
    if(status!=ERROR_SUCCESS)return status;
    status=replace(endSlot,reinterpret_cast<void*>(originalEnd),reinterpret_cast<void*>(&ObserveEnd));
    if(status!=ERROR_SUCCESS)replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));
    if(status==ERROR_SUCCESS){
        originalLayerEnd=layerCandidate;layerEndSlot=layerAddress;
        status=replace(layerEndSlot,reinterpret_cast<void*>(originalLayerEnd),reinterpret_cast<void*>(&ObserveLayerEnd));
        if(status!=ERROR_SUCCESS){replace(endSlot,reinterpret_cast<void*>(&ObserveEnd),reinterpret_cast<void*>(originalEnd));replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));}
    }
    if(status==ERROR_SUCCESS){
        originalDraw=drawCandidate;drawSlot=drawAddress;
        status=replace(drawSlot,reinterpret_cast<void*>(originalDraw),reinterpret_cast<void*>(&ObserveDraw));
        if(status!=ERROR_SUCCESS){
            replace(layerEndSlot,reinterpret_cast<void*>(&ObserveLayerEnd),reinterpret_cast<void*>(originalLayerEnd));
            replace(endSlot,reinterpret_cast<void*>(&ObserveEnd),reinterpret_cast<void*>(originalEnd));
            replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));
        }
    }
    return status;
}
}
