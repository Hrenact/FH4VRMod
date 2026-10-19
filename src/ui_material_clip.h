#pragma once
#include <atomic>
#include "ui_root_probe.h"
#include "ui_affine_clip.h"
#include "ui_material_clip_math.h"
#include "ui_material_unlit_gate.h"

// Only the verified FZMaterial stack packet, an exact mod projection and an
// verified unlit preset may use this compensation. Native renderer caches,
// material objects and shader bytecode are never changed here.
namespace ui_material_clip {
inline std::atomic<uint64_t> applied{0},scopeSkipped{0},projectionSkipped{0},
    readRejected{0},disabled{0},variantSkipped{0},mathRejected{0},restoreChanged{0},
    shaderMismatch{0};
inline std::atomic<bool> fuse{false};
inline std::atomic<uint64_t> variantReasons[32]{};
inline std::atomic<uint32_t> lastExpectedShader{0},lastObservedShader{0};
inline std::atomic<uint64_t> ordinaryApplied{0},colorOnlyApplied{0};
using ui_root_probe::Read;
using ui_root_probe::Member;
using ui_root_probe::LastErrorGuard;
struct Reader {
    bool operator()(uintptr_t address,void* output,size_t size) const {
        SIZE_T got=0;
        return address && size<=UINTPTR_MAX-address &&
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&got) && got==size;
    }
};
class Scope {
    unsigned char* packet=nullptr;
    uintptr_t beforePointers[3]{},writtenPointers[3]{},owner=0;
    float beforePlanes[16]{};
    ui_material_clip_math::Result values{};
    uint32_t expectedShader=0; // Native binding pipeline ID, not a PS enumeration.
public:
    Scope(unsigned index,uintptr_t base,void* binding,void* context,void* candidate,void* caller,bool active) {
        LastErrorGuard error;
        if(!active || fuse.load(std::memory_order_acquire))return;
        // This return address identifies the live, synchronously consumed stack
        // packet producer verified in PID33092. Other producers stay native.
        auto root=ui_root_probe::current;
        if((index!=0 && index!=3) || !root || !root->expectedRenderer ||
           !base || base>UINTPTR_MAX-0x80cf5d0 ||
           reinterpret_cast<uintptr_t>(caller)!=base+0x1a7bd2f){++scopeSkipped;return;}
        uintptr_t renderer=0,vt=0,renderContext=0;
        if(!Read(base+0x80cf5c8,renderer) || renderer!=reinterpret_cast<uintptr_t>(root->expectedRenderer) ||
           !Read(renderer,vt) || vt!=base+0x5c7ed70 ||
           !Member(renderer,0x40,renderContext) || renderContext!=reinterpret_cast<uintptr_t>(context)){
            ++scopeSkipped;return;
        }
        const auto address=reinterpret_cast<uintptr_t>(candidate);
        if(!Member(address,0x30,beforePointers) || beforePointers[1]!=renderer+0x3a0 ||
           beforePointers[2]!=renderer+0x430 || !Member(address,0x50,beforePlanes)){
            ++readRejected;return;
        }
        float projection[16];
        if(!Member(renderer,0x360,projection) || !ui_affine_clip::Matches(reinterpret_cast<void*>(renderer),projection)){
            ++projectionSkipped;return;
        }
        struct Stack {uintptr_t data;uint32_t capacity,count;} stack{};
        struct Rectangle {float values[4];uint8_t enabled,padding[3];} rectangle{};
        static_assert(sizeof(Rectangle)==20,"Native rectangle stride");
        if(!Member(renderer,0x270,stack) || !stack.data || !stack.count ||
           stack.count>stack.capacity || stack.count>4096 ||
           stack.data>UINTPTR_MAX-(stack.count-1)*sizeof(Rectangle) ||
           !Read(stack.data+(stack.count-1)*sizeof(Rectangle),rectangle)){
            ++readRejected;return;
        }
        if(!rectangle.enabled || rectangle.values[2]==0 || rectangle.values[3]==0){++disabled;return;}
        if(rectangle.enabled!=1){++readRejected;return;}
        auto selection=ui_material_unlit_gate::Evaluate(index,base,reinterpret_cast<uintptr_t>(binding),address,Reader{});
        if(!selection.accepted){++variantSkipped;auto reason=static_cast<unsigned>(selection.reason);
            if(reason<32)++variantReasons[reason];return;}
        float local[16],a[16],b[16],intermediate[16],combined[16],mv[16],d[16],d0[16],cachePlanes[16];
        if(!Member(renderer,0x320,local) || !Member(renderer,0x2a0,a) || !Member(renderer,0x2e0,b) ||
           !Read(beforePointers[0],mv) || !Read(beforePointers[1],d) || !Read(beforePointers[2],d0) ||
           !Member(renderer,0x470,cachePlanes) || memcmp(beforePlanes,cachePlanes,64)){
            ++readRejected;return;
        }
        for(float value:local)if(!std::isfinite(value)){++mathRejected;return;}
        if(local[3]!=0 || local[7]!=0 || local[11]!=0 || local[15]!=1){++mathRejected;return;}
        ui_affine_clip::Multiply(b,projection,intermediate);ui_affine_clip::Multiply(intermediate,a,combined);
        if(!ui_material_clip_math::Compute(mv,combined,d,d0,rectangle.values,values)){++mathRejected;return;}
        owner=selection.owner;expectedShader=selection.expectedShaderId;
        writtenPointers[0]=reinterpret_cast<uintptr_t>(values.mv);
        writtenPointers[1]=reinterpret_cast<uintptr_t>(values.projection);
        writtenPointers[2]=reinterpret_cast<uintptr_t>(values.screenProjection);
        packet=static_cast<unsigned char*>(candidate);
        memcpy(packet+0x30,writtenPointers,sizeof(writtenPointers));
        memcpy(packet+0x50,values.planes,sizeof(values.planes));++applied;
        if(selection.shaderOffset==0x60)++colorOnlyApplied;else ++ordinaryApplied;
    }
    void Returned() {
        LastErrorGuard error;if(!packet)return;
        uint32_t observed=0;lastExpectedShader.store(expectedShader,std::memory_order_relaxed);
        if(!Member(owner,0x1c8,observed) || observed!=expectedShader){
            ++shaderMismatch;fuse.store(true,std::memory_order_release);
        }
        lastObservedShader.store(observed,std::memory_order_relaxed);
    }
    ~Scope() {
        LastErrorGuard error;if(!packet)return;
        // Preserve legitimate native side effects (e.g. color updates). Restore
        // only our temporary pointers/planes, including during exception unwind.
        uintptr_t current[3]{};float planes[16]{};
        if(Read(reinterpret_cast<uintptr_t>(packet+0x30),current)){
            for(unsigned i=0;i<3;++i){
                if(current[i]==writtenPointers[i])memcpy(packet+0x30+8*i,&beforePointers[i],8);
                else ++restoreChanged;
            }
        }else ++restoreChanged;
        if(Read(reinterpret_cast<uintptr_t>(packet+0x50),planes) && !memcmp(planes,values.planes,64))memcpy(packet+0x50,beforePlanes,64);
        else ++restoreChanged;
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
inline void Statistics(FILE* file) {
    fprintf(file," clip_applied=%llu clip_scope_skip=%llu clip_projection_skip=%llu clip_read_rejected=%llu clip_disabled=%llu clip_variant_skip=%llu clip_math_rejected=%llu clip_restore_changed=%llu clip_shader_mismatch=%llu clip_fuse=%u clip_shader_expected=%u clip_shader_observed=%u clip_variant_reasons=",
        applied.load(),scopeSkipped.load(),projectionSkipped.load(),readRejected.load(),disabled.load(),variantSkipped.load(),
        mathRejected.load(),restoreChanged.load(),shaderMismatch.load(),fuse.load()?1u:0u,lastExpectedShader.load(),lastObservedShader.load());
    for(unsigned i=0;i<32;++i)fprintf(file,"%s%llu",i?",":"",variantReasons[i].load());
    fprintf(file," clip_ordinary_applied=%llu clip_color_only_applied=%llu",ordinaryApplied.load(),colorOnlyApplied.load());
}
}
