#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
namespace menu_camera_probe {
using Setter=void(*)(void*,const void*);
inline Setter original=nullptr;
inline HMODULE owner=nullptr;
inline uintptr_t baseAddress=0;
inline void* volatile* slots[4]{};
inline constexpr uintptr_t tables[]={0x4ea33d0,0x4ea3c10,0x4ea3450,0x4f14898};
inline std::atomic<uint64_t> calls[4]{},lastTicks[4]{};
inline bool Read(const void* source,void* dest,SIZE_T size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),source,dest,size,&got)&&got==size;}
inline void Observe(void* object,const void* position) {
    DWORD incoming=GetLastError();uintptr_t table=0;Read(object,&table,sizeof(table));
    int index=-1;for(int i=0;i<4;++i)if(table==baseAddress+tables[i])index=i;
    uint64_t count=index<0?0:++calls[index];
    SetLastError(incoming);original(object,position);DWORD saved=GetLastError();
    if(owner && index>=0){
        auto now=GetTickCount64(),previous=lastTicks[index].load();
        if(count<=2 || (now-previous>=1000 && lastTicks[index].compare_exchange_strong(previous,now))){
            double xyz[3]{};uintptr_t camera=0,vt=0,positionMethod=0,viewMethod=0;
            bool valid=Read(position,xyz,sizeof(xyz));
            Read(static_cast<char*>(object)+0x60,&camera,8);
            if(camera && Read(reinterpret_cast<void*>(camera),&vt,8)){
                Read(reinterpret_cast<void*>(vt+52*8),&positionMethod,8);
                Read(reinterpret_cast<void*>(vt+12*8),&viewMethod,8);
            }
            wchar_t path[MAX_PATH]{};auto n=GetModuleFileNameW(owner,path,MAX_PATH);
            auto tail=n&&n<MAX_PATH?wcsrchr(path,L'\\'):nullptr;
            if(tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"menu-camera-probe.log")){
                FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){
                    fprintf(f,"pid=%lu tick=%llu class=%d calls=%llu object=%p xyz_valid=%d xyz=%.9g,%.9g,%.9g camera=%llx vtable=%llx position=%llx view=%llx\n",
                        GetCurrentProcessId(),now,index,count,object,valid,xyz[0],xyz[1],xyz[2],camera,vt,positionMethod,viewMethod);fclose(f);
                }
            }
        }
    }
    SetLastError(saved);
}
template<class Replace> DWORD Start(uintptr_t base,HMODULE module,Replace replace){
    const unsigned char expected[]={0x0f,0x10,0x02,0x0f,0x11,0x41,0x18};unsigned char code[sizeof(expected)]{};
    auto target=reinterpret_cast<Setter>(base+0x1604740);
    if(!Read(reinterpret_cast<void*>(target),code,sizeof(code))||memcmp(code,expected,sizeof(code)))return ERROR_REVISION_MISMATCH;
    for(int i=0;i<4;++i){void* pointer=nullptr;auto s=reinterpret_cast<void* volatile*>(base+tables[i]+9*8);
        if(!Read(const_cast<void**>(s),&pointer,8)||pointer!=reinterpret_cast<void*>(target))return ERROR_REVISION_MISMATCH;
    }
    original=target;owner=module;baseAddress=base;
    for(int i=0;i<4;++i){auto s=reinterpret_cast<void* volatile*>(base+tables[i]+9*8);
        auto status=replace(s,reinterpret_cast<void*>(target),reinterpret_cast<void*>(&Observe));
        if(status){for(int j=0;j<i;++j)replace(slots[j],reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(target));return status;}
        slots[i]=s;
    }
    return ERROR_SUCCESS;
}
template<class Replace> void Stop(Replace replace){for(auto s:slots)if(s)replace(s,reinterpret_cast<void*>(&Observe),reinterpret_cast<void*>(original));}
}
