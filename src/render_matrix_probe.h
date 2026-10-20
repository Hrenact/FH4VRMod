#pragma once
#include "menu_camera_probe.h"
#include "frame_order_trace.h"
#include "stereo_trial.h"
#include "render_source_tracker.h"
#include <intrin.h>
namespace render_matrix_probe {
using Setter=void(*)(void*,const float*);
inline Setter original[2]{};
inline void* volatile* slots[2]{};
inline uintptr_t gameBase=0;
template<unsigned I> void Observe(void* object,const float* matrix){
    const DWORD before=GetLastError();
    const void* caller=_ReturnAddress();
    // Copy only while tracing. Fingerprints identify equal data, not unique frames.
    if(frame_order_trace::recorder.Active()
#ifdef FH4VR_SOURCE_DRIVEN_STEREO
       || stereo_trial::command.load()!=0
#endif
       ){
        const auto token=render_source::tracker.Epoch();
        float copy[16]{};
        if(menu_camera_probe::Read(matrix,copy,sizeof(copy))){
            frame_order_trace::Record(I==0?frame_order_trace::RenderMatrixQueued:frame_order_trace::RenderMatrixConsumed,
                stereo_trial::command.load(),caller,object,frame_order_trace::MatrixFingerprint(copy));
#ifdef FH4VR_RENDER_CYCLE_LATCH
            if constexpr(I==0){
                if(reinterpret_cast<uintptr_t>(caller)==gameBase+0x30c7661){
                    uintptr_t target=0;
                    if(menu_camera_probe::Read(static_cast<char*>(object)+8,&target,sizeof(target)))
                        render_source::tracker.Queue(token,target,copy,render_cycle::state.Read());
                }
            }else if(reinterpret_cast<uintptr_t>(caller)==gameBase+0x3940766){
                render_source::tracker.Consume(token,reinterpret_cast<uintptr_t>(object),copy,GetCurrentThreadId());
            }
#endif
        }else if constexpr(I==1){
            if(reinterpret_cast<uintptr_t>(caller)==gameBase+0x3940766)
                render_source::tracker.Consume(token,reinterpret_cast<uintptr_t>(object),nullptr,GetCurrentThreadId());
        }
    }
    SetLastError(before);
    original[I](object,matrix);
}
inline void* Observer(unsigned i){return i?reinterpret_cast<void*>(&Observe<1>):reinterpret_cast<void*>(&Observe<0>);}
template<class Replace> DWORD Start(uintptr_t base,Replace replace){
    gameBase=base;
    constexpr uintptr_t tables[]={0x5b7d358,0x5b72ce8};
    constexpr uintptr_t methods[]={0x397d980,0x39094d0};
    const unsigned char expected[2][7]={{0x48,0x89,0x5c,0x24,0x10,0x48,0x89},
                                      {0x0f,0x10,0x02,0x0f,0x11,0x41,0x20}};
    for(unsigned i=0;i<2;++i){
        unsigned char code[7]{};void* pointer=nullptr;
        auto target=reinterpret_cast<void*>(base+methods[i]);
        auto methodSlot=reinterpret_cast<void* volatile*>(base+tables[i]+45*8);
        if(!menu_camera_probe::Read(target,code,7)||memcmp(code,expected[i],7)||
           !menu_camera_probe::Read(const_cast<void**>(methodSlot),&pointer,8)||pointer!=target)
            return ERROR_REVISION_MISMATCH;
    }
    for(unsigned i=0;i<2;++i){
        original[i]=reinterpret_cast<Setter>(base+methods[i]);
        auto methodSlot=reinterpret_cast<void* volatile*>(base+tables[i]+45*8);
        auto status=replace(methodSlot,reinterpret_cast<void*>(original[i]),Observer(i));
        if(status){
            for(unsigned j=0;j<i;++j){replace(slots[j],Observer(j),reinterpret_cast<void*>(original[j]));slots[j]=nullptr;}
            return status;
        }
        slots[i]=methodSlot;
    }
    return ERROR_SUCCESS;
}
template<class Replace> void Stop(Replace replace){
    for(unsigned i=0;i<2;++i)if(slots[i]){
        replace(slots[i],Observer(i),reinterpret_cast<void*>(original[i]));slots[i]=nullptr;
    }
}
}
