#pragma once
#include "menu_camera_probe.h"
namespace presentation_camera_probe {
using Setter=void(*)(void*,const void*,const void*,unsigned);
inline Setter original=nullptr;
inline HMODULE owner=nullptr;
inline void* volatile* slot=nullptr;
inline std::atomic<uint64_t> calls{0},lastTick{0};
inline void Observe(void* object,const void* first,const void* second,unsigned flags){
    original(object,first,second,flags);
    const DWORD saved=GetLastError();
    const auto count=++calls;
    if(owner){
        const auto now=GetTickCount64();auto prior=lastTick.load();
        if(count<=2 || (now-prior>=1000 && lastTick.compare_exchange_strong(prior,now))){
            float a[16]{},b[16]{};
            bool valid=menu_camera_probe::Read(first,a,sizeof(a)) && menu_camera_probe::Read(second,b,sizeof(b));
            wchar_t path[MAX_PATH]{};auto n=GetModuleFileNameW(owner,path,MAX_PATH);
            auto tail=n&&n<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
            if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"presentation-camera-probe.log")){
                FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){
                    fprintf(f,"pid=%lu tick=%llu calls=%llu object=%p flags=%u valid=%d a=",GetCurrentProcessId(),now,count,object,flags,valid);
                    for(float v:a)fprintf(f,"%.9g,",v);
                    fputs(" b=",f);for(float v:b)fprintf(f,"%.9g,",v);fputc('\n',f);fclose(f);
                }
            }
        }
    }
    SetLastError(saved);
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace){
    const unsigned char expected[]={0x0f,0x10,0x02,0x0f,0x11,0x41,0x60};unsigned char code[sizeof(expected)]{};
    auto target=reinterpret_cast<Setter>(base+0x38fe000);
    if(!menu_camera_probe::Read(reinterpret_cast<void*>(target),code,sizeof(code))||memcmp(code,expected,sizeof(code)))return ERROR_REVISION_MISMATCH;
    auto s=reinterpret_cast<void* volatile*>(base+0x5b6d3a0+12*8);void* pointer=nullptr;
    if(!menu_camera_probe::Read(const_cast<void**>(s),&pointer,8)||pointer!=reinterpret_cast<void*>(target))return ERROR_REVISION_MISMATCH;
    original=target;owner=module;
    auto status=replace(s,reinterpret_cast<void*>(target),reinterpret_cast<void*>(&Observe));
    if(status==ERROR_SUCCESS)slot=s;
    return status;
}
template<class Replace> void Stop(Replace replace){if(slot)replace(slot,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));}
}
