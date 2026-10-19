#pragma once
#include "menu_camera_probe.h"
#include "eye_offset.h"
#ifdef FH4VR_MULTICAM_VR
#include "stereo_head.h"
#include "runtime_projection.h"
#endif
#ifdef FH4VR_MULTICAM_OFFSET_TEST
#include "stereo_trial.h"
#endif
namespace multicam_probe {
using Getter=void*(*)(void*,void*);
inline Getter original[2]{};
inline void* volatile* slots[2]{};
inline HMODULE owner=nullptr;
inline std::atomic<uint64_t> calls[2]{},lastTicks[2]{};
inline constexpr unsigned slotIndex[]={12,52};
inline constexpr uintptr_t rvas[]={0x3150680,0x3150960};
using Scalar=float(*)(void*);
inline Scalar scalarOriginal[2]{};
inline void* volatile* scalarSlots[2]{};
inline std::atomic<uint64_t> scalarCalls[2]{};
inline float ProjectionValue(unsigned index,float raw,bool active,float target){
    if(!active || !std::isfinite(raw))return raw;
    if(index==0)return raw>.2f && raw<2.4f && std::isfinite(target) && target>.2f && target<2.4f?target:raw;
    return std::abs(raw-.1f)<.001f?.01f:raw;
}
template<unsigned I> float ObserveScalar(void* object){
    float raw=scalarOriginal[I](object);DWORD saved=GetLastError();++scalarCalls[I];
#ifdef FH4VR_MULTICAM_VR
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    raw=ProjectionValue(I,raw,stereo_trial::Offset(foreground==GetCurrentProcessId(),GetTickCount64())!=0,runtime_projection::verticalRadians.load());
#endif
    SetLastError(saved);return raw;
}
inline void* scalarObserver(unsigned i){return i==0?reinterpret_cast<void*>(&ObserveScalar<0>):reinterpret_cast<void*>(&ObserveScalar<1>);}
inline EyeOffsetGate offsetGate;
inline std::atomic<uint64_t> failureLogs{0};
inline bool Shift(unsigned index,void* output,const float* view,float eye){
    if(index==0)return ApplyEyeOffset(static_cast<float*>(output),eye);
    if(index!=1 || !view)return false;
    // Getter copies 32 bytes, but only the first 24 are confirmed XYZ. The tail
    // varies during normal driving/cinematics and is NOT a validated homogeneous W.
    // Supply a local W to the math helper; never read/validate/rewrite the tail.
    double position[4]={0,0,0,1};memcpy(position,output,3*sizeof(double));
    if(!ApplyEyePosition(position,view,eye))return false;
    memcpy(output,position,3*sizeof(double));return true;
}
template<unsigned I> void* Observe(void* object,void* output){
    void* result=original[I](object,output);const DWORD saved=GetLastError();const auto count=++calls[I];
    float eye=0;bool applied=false;unsigned failure=0;bool forceLog=false;
#ifdef FH4VR_MULTICAM_VR
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    eye=stereo_trial::Offset(foreground==GetCurrentProcessId(),GetTickCount64());
    if(eye!=0 && result && result==output){
        alignas(16) float view[16]{};
        bool valid=false;
        if constexpr(I==0){memcpy(view,output,sizeof(view));valid=true;}
        else valid=menu_camera_probe::Read(static_cast<char*>(object)+0x330,view,sizeof(view));
        if(!valid)failure=1;
        else if(!stereo_head::Apply(view))failure=2;
        else {
            if constexpr(I==0){applied=Shift(I,view,nullptr,eye);if(applied)memcpy(output,view,sizeof(view));}
            else applied=Shift(I,output,view,eye);
            if(!applied)failure=3;
        }
        if(applied)++stereo_trial::applied;else ++stereo_trial::rejected;
        if(failure)forceLog=failureLogs.fetch_add(1)<16;
    }
#endif
#ifdef FH4VR_MULTICAM_OFFSET_TEST
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    eye=offsetGate.offset(foreground==GetCurrentProcessId() && stereo_trial::command.load()==0,
        (GetAsyncKeyState(VK_F4)&0x8000)!=0,(GetAsyncKeyState(VK_F5)&0x8000)!=0,GetTickCount64());
    if(eye!=0 && result && result==output){
        alignas(16) float view[16]{};
        if constexpr(I==0)applied=Shift(I,output,nullptr,eye);
        else if(menu_camera_probe::Read(static_cast<char*>(object)+0x330,view,sizeof(view)))applied=Shift(I,output,view,eye);
    }
#endif
    if(owner){
        const auto now=GetTickCount64();auto prior=lastTicks[I].load();
        if(forceLog || count<=2 || (now-prior>=1000 && lastTicks[I].compare_exchange_strong(prior,now))){
            float matrix[16]{};double position[4]{};float fov=0;
            bool valid=menu_camera_probe::Read(output,I==0?static_cast<void*>(matrix):static_cast<void*>(position),I==0?sizeof(matrix):sizeof(position));
            menu_camera_probe::Read(static_cast<char*>(object)+0x370,&fov,sizeof(fov));
            wchar_t path[MAX_PATH]{};auto n=GetModuleFileNameW(owner,path,MAX_PATH);
            auto tail=n&&n<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
            if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"multicam-probe.log")){
                FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){
                    fprintf(f,"pid=%lu tick=%llu slot=%u calls=%llu object=%p valid=%d fov=%.9g eye=%.6g applied=%d failure=%u fov_calls=%llu near_calls=%llu values=",GetCurrentProcessId(),now,slotIndex[I],count,object,valid,fov,eye,applied,failure,scalarCalls[0].load(),scalarCalls[1].load());
                    if constexpr(I==0){for(float v:matrix)fprintf(f,"%.9g,",v);}else{for(double v:position)fprintf(f,"%.12g,",v);}
                    fputc('\n',f);fclose(f);
                }
            }
        }
    }
    SetLastError(saved);return result;
}
inline void* observer(unsigned i){return i==0?reinterpret_cast<void*>(&Observe<0>):reinterpret_cast<void*>(&Observe<1>);}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace){
    const unsigned char expected[2][7]={{0x0f,0x28,0x81,0x30,0x03,0x00,0x00},{0x48,0x8b,0x81,0x78,0x03,0x00,0x00}};
    for(unsigned i=0;i<2;++i){
        unsigned char code[7]{};void* pointer=nullptr;auto target=reinterpret_cast<void*>(base+rvas[i]);
        auto s=reinterpret_cast<void* volatile*>(base+0x5306aa0+slotIndex[i]*8);
        if(!menu_camera_probe::Read(target,code,7)||memcmp(code,expected[i],7)||!menu_camera_probe::Read(const_cast<void**>(s),&pointer,8)||pointer!=target)return ERROR_REVISION_MISMATCH;
    }
    owner=module;
#ifdef FH4VR_MULTICAM_VR
    constexpr uintptr_t scalarRvas[]={0x3150390,0x3150750};constexpr unsigned scalarIndices[]={19,49};
    const unsigned char scalarCode[2][8]={{0xf3,0x0f,0x10,0x81,0x70,0x03,0,0},{0x48,0x8b,0x89,0xd8,0x02,0,0,0x48}};
    for(unsigned i=0;i<2;++i){
        unsigned char code[8]{};void* pointer=nullptr;auto target=reinterpret_cast<void*>(base+scalarRvas[i]);
        auto s=reinterpret_cast<void* volatile*>(base+0x5306aa0+scalarIndices[i]*8);
        if(!menu_camera_probe::Read(target,code,8)||memcmp(code,scalarCode[i],8)||!menu_camera_probe::Read(const_cast<void**>(s),&pointer,8)||pointer!=target)return ERROR_REVISION_MISMATCH;
        scalarOriginal[i]=reinterpret_cast<Scalar>(target);
    }
#endif
    for(unsigned i=0;i<2;++i){
        original[i]=reinterpret_cast<Getter>(base+rvas[i]);
        auto s=reinterpret_cast<void* volatile*>(base+0x5306aa0+slotIndex[i]*8);
        auto status=replace(s,reinterpret_cast<void*>(original[i]),observer(i));
        if(status){for(unsigned j=0;j<i;++j){replace(slots[j],observer(j),reinterpret_cast<void*>(original[j]));slots[j]=nullptr;}return status;}
        slots[i]=s;
    }
#ifdef FH4VR_MULTICAM_VR
    for(unsigned i=0;i<2;++i){
        auto s=reinterpret_cast<void* volatile*>(base+0x5306aa0+scalarIndices[i]*8);
        auto status=replace(s,reinterpret_cast<void*>(scalarOriginal[i]),scalarObserver(i));
        if(status){
            for(unsigned j=0;j<i;++j){replace(scalarSlots[j],scalarObserver(j),reinterpret_cast<void*>(scalarOriginal[j]));scalarSlots[j]=nullptr;}
            for(unsigned j=0;j<2;++j){replace(slots[j],observer(j),reinterpret_cast<void*>(original[j]));slots[j]=nullptr;}
            return status;
        }
        scalarSlots[i]=s;
    }
#endif
    return ERROR_SUCCESS;
}
template<class Replace> void Stop(Replace replace){for(unsigned i=0;i<2;++i){if(slots[i])replace(slots[i],observer(i),reinterpret_cast<void*>(original[i]));if(scalarSlots[i])replace(scalarSlots[i],scalarObserver(i),reinterpret_cast<void*>(scalarOriginal[i]));}}
}
