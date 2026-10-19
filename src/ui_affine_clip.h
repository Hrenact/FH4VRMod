#pragma once
#include <windows.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include "ui_clip_projection.h"
namespace ui_affine_clip {
// Real FH4 VS clips dot((position * matWorldView).xyz, normal) - distance.
// It does NOT use projected w in that equation. Geometry retains its full
// projective matrix; only the plane coefficients use the affine xyz mapping.
inline bool ShaderPlanes(const float* combined,const float* rect,float* planes) {
    float affine[16];
    for(unsigned i=0;i<16;++i)if(!std::isfinite(combined[i]))return false;
    memcpy(affine,combined,sizeof(affine));
    affine[3]=affine[7]=affine[11]=0;affine[15]=1;
    return ui_clip_projection::RectPlanes(affine,rect,planes);
}
struct Signature {void* object=nullptr;float projection[16]{};};
inline Signature signatures[64]{};
inline unsigned nextSignature=0;
inline std::mutex signatureLock;
inline std::atomic<unsigned long long> geometryApplied{0},textApplied{0},rejected{0},disabled{0},unmatched{0},cacheChanged{0};
inline void Remember(void* object,const float* projection) {
    std::lock_guard<std::mutex> guard(signatureLock);
    for(const auto& item:signatures)if(item.object==object && !memcmp(item.projection,projection,64))return;
    auto& item=signatures[nextSignature++%64];item.object=object;memcpy(item.projection,projection,64);
}
inline bool Matches(void* object,const float* projection) {
    std::lock_guard<std::mutex> guard(signatureLock);
    for(const auto& item:signatures)if(item.object==object && !memcmp(item.projection,projection,64))return true;
    return false;
}
template<class T> inline bool Read(const void* address,T& value) {
    SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),address,&value,sizeof(T),&n) && n==sizeof(T);
}
inline void Multiply(const float* a,const float* b,float* out) {
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        out[r*4+c]=((a[r*4]*b[c]+a[r*4+1]*b[4+c])+a[r*4+2]*b[8+c])+a[r*4+3]*b[12+c];
}
inline bool Compute(void* object,bool active,bool prepared,float* planes) {
    if(!active)return false;
    auto bytes=static_cast<unsigned char*>(object);float projection[16];
    if(!Read(bytes+0x360,projection)||!Matches(object,projection)){++unmatched;return false;}
    // The shader discards w before clipping. The source local transform must
    // therefore preserve homogeneous w=1 for the rectangle-space derivation.
    float local[16];
    if(!Read(bytes+0x320,local)){++rejected;return false;}
    for(float value:local)if(!std::isfinite(value)){++rejected;return false;}
    if(local[3]!=0 || local[7]!=0 || local[11]!=0 || local[15]!=1){++rejected;return false;}
    struct Stack {uintptr_t data;uint32_t capacity,count;} stack{};
    if(!Read(bytes+0x270,stack)||!stack.data||!stack.count||stack.count>4096||stack.count>stack.capacity){++rejected;return false;}
    struct Rectangle {float values[4];uint8_t enabled,padding[3];} rectangle{};
    static_assert(sizeof(Rectangle)==20,"Native clip stack stride");
    if(stack.data>UINTPTR_MAX-(stack.count-1)*sizeof(Rectangle)){++rejected;return false;}
    if(!Read(reinterpret_cast<void*>(stack.data+(stack.count-1)*sizeof(Rectangle)),rectangle)){++rejected;return false;}
    if(!rectangle.enabled){++disabled;return false;}
    if(rectangle.enabled!=1){++rejected;return false;}
    float combined[16];
    if(prepared){
        // Native Prepare has resolved its combined cache immediately before us.
        if(!Read(bytes+0x3e0,combined)){++rejected;return false;}
    }else{
        // Before text submission its cache may be dirty. Reconstruct B*P*A
        // without calling game code or altering its cache-valid flag.
        float a[16],b[16],intermediate[16];
        if(!Read(bytes+0x2a0,a)||!Read(bytes+0x2e0,b)){++rejected;return false;}
        Multiply(b,projection,intermediate);Multiply(intermediate,a,combined);
    }
    if(!ShaderPlanes(combined,rectangle.values,planes)){++rejected;return false;}
    return true;
}
inline void Prepared(void* object,bool active) {
    float planes[16];if(!Compute(object,active,true,planes))return;
    memcpy(static_cast<unsigned char*>(object)+0x5b0,planes,sizeof(planes));++geometryApplied;
}
struct TextScope {
    void* object=nullptr;float before[16]{},written[16]{};
    explicit TextScope(void* candidate,bool active) {
        if(!Compute(candidate,active,false,written))return;
        auto cache=static_cast<unsigned char*>(candidate)+0x470;
        if(!Read(cache,before)){++rejected;return;}
        object=candidate;memcpy(cache,written,64);++textApplied;
    }
    ~TextScope() {
        if(!object)return;
        auto cache=static_cast<unsigned char*>(object)+0x470;float current[16];
        // Nested scopes restore in reverse order. Do not overwrite a legitimate
        // game-side cache refresh that happened during the original call.
        if(Read(cache,current) && !memcmp(current,written,64))memcpy(cache,before,64);
        else ++cacheChanged;
    }
    TextScope(const TextScope&)=delete;
    TextScope& operator=(const TextScope&)=delete;
};
}
