#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

// Selection only: callers must separately require an active matching screen-UI
// root, the exact mod-written projection, and the verified packet matrix origin.
// Verified against 1a7c100 / 1a7f020 and their constructors in image 16224.
// No calls into the game, no mutation, no cached packet or object pointers.
namespace ui_material_unlit_gate {
enum class Reason {
    Accepted, UnsupportedPreset, InvalidAddress, ReadFailure, BindingMismatch,
    RendererMismatch, DirtyPreset, TypeRangeUnknown, SpecialType,
    LitBranch, DepthBranch, ColorOnlyBranch, PillBranch, SaturationBranch,
    NonFiniteSelector, InvalidShaderId
};
struct Result {
    bool accepted=false;
    Reason reason=Reason::ReadFailure;
    uint32_t expectedShaderId=0;
    uintptr_t owner=0;
    unsigned shaderOffset=0;
};
inline const char* Name(Reason r) {
    switch(r) {
    case Reason::Accepted:return "accepted";
    case Reason::UnsupportedPreset:return "unsupported-preset";
    case Reason::InvalidAddress:return "invalid-address";
    case Reason::ReadFailure:return "read-failure";
    case Reason::BindingMismatch:return "binding-mismatch";
    case Reason::RendererMismatch:return "renderer-mismatch";
    case Reason::DirtyPreset:return "dirty-preset";
    case Reason::TypeRangeUnknown:return "type-range-unknown";
    case Reason::SpecialType:return "special-type";
    case Reason::LitBranch:return "lit-branch";
    case Reason::DepthBranch:return "depth-branch";
    case Reason::ColorOnlyBranch:return "color-only-branch";
    case Reason::PillBranch:return "pill-branch";
    case Reason::SaturationBranch:return "saturation-branch";
    case Reason::NonFiniteSelector:return "nonfinite-selector";
    case Reason::InvalidShaderId:return "invalid-shader-id";
    }
    return "unknown";
}
template<class T> inline T Value(const unsigned char* p,size_t at) {
    T v{};std::memcpy(&v,p+at,sizeof(v));return v;
}
template<class Reader,class T> inline bool Member(Reader& read,uintptr_t p,size_t off,T& out) {
    constexpr auto max=(std::numeric_limits<uintptr_t>::max)();
    return p && off<=max-p && sizeof(out)-1<=max-(p+off) && read(p+off,&out,sizeof(out));
}
enum class TypeMatch {No,Yes,Unknown};
inline TypeMatch MatchType(const unsigned char* source,const unsigned char* target) {
    // Exact 3b96740 arithmetic, including unsigned branch comparison and x64
    // variable-shift count masking. Negative differences occur for the normal
    // base preset (depth39) tested against AlphaOnly/Tint (depth43).
    if((source[0x31]&4) && (target[0x31]&1))return TypeMatch::Yes;
    const int delta=int(Value<int8_t>(source,0x30))-int(Value<int8_t>(target,0x30));
    const uint32_t shift=static_cast<uint32_t>(delta);
    const uint64_t a=Value<uint64_t>(source,0x20),b=Value<uint64_t>(source,0x28);
    uint64_t first=0,second=0;
    if(!delta){first=a;second=b;}
    else if(shift<64){first=a>>shift;second=(a<<(64-shift))|(b>>shift);}
    else second=a>>((shift-64u)&63u);
    return first==Value<uint64_t>(target,0x20) && second==Value<uint64_t>(target,0x28)
        ? TypeMatch::Yes:TypeMatch::No;
}
inline bool FiniteBits(uint32_t bits) {return (bits&0x7f800000u)!=0x7f800000u;}

template<class Reader>
Result Evaluate(unsigned index,uintptr_t base,uintptr_t binding,uintptr_t packet,Reader&& read) {
    Result result{};
    auto reject=[&](Reason reason){result.reason=reason;return result;};
    if(index!=0 && index!=3)return reject(Reason::UnsupportedPreset);
    constexpr auto max=(std::numeric_limits<uintptr_t>::max)();
    if(!base || base>max-0x80cf5d0 || !binding || !packet)return reject(Reason::InvalidAddress);
    uintptr_t bindingVt=0,renderer=0,rendererVt=0;
    if(!Member(read,binding,0,bindingVt))return reject(Reason::ReadFailure);
    if(bindingVt!=base+(index==0?0x4fe14e0:0x4fe1528))return reject(Reason::BindingMismatch);
    if(!Member(read,base,0x80cf5c8,renderer) || !Member(read,renderer,0,rendererVt))
        return reject(Reason::ReadFailure);
    if(rendererVt!=base+0x5c7ed70)return reject(Reason::RendererMismatch);
    unsigned char packetFields[0xf8]{};
    if(!Member(read,packet,0,packetFields))return reject(Reason::ReadFailure);
    result.owner=Value<uintptr_t>(packetFields,0);
    const auto preset=Value<uintptr_t>(packetFields,8);
    unsigned char flags[3]{};
    if(!Member(read,preset,0x80,flags))return reject(Reason::ReadFailure);
    // The original may refresh all preset fields if dirty; do not predict that.
    if(flags[2])return reject(Reason::DirtyPreset);
    if(index==0){
        unsigned char source[0x32]{},alpha[0x32]{},tint[0x32]{};
        if(!Member(read,Value<uintptr_t>(packetFields,0x10),0,source)
            || !Member(read,base,0x7ee8970,alpha) || !Member(read,base,0x7eebe40,tint))
            return reject(Reason::ReadFailure);
        auto a=MatchType(source,alpha);
        if(a==TypeMatch::Unknown)return reject(Reason::TypeRangeUnknown);
        if(a==TypeMatch::Yes)return reject(Reason::SpecialType);
        auto t=MatchType(source,tint);
        if(t==TypeMatch::Unknown)return reject(Reason::TypeRangeUnknown);
        if(t==TypeMatch::Yes)return reject(Reason::SpecialType);
    }
    unsigned char lit=0,depth=0;
    if(!Member(read,result.owner,0x1a0,lit))return reject(Reason::ReadFailure);
    if(lit && !flags[0])return reject(Reason::LitBranch);
    if(!Member(read,renderer,0x8e1,depth))return reject(Reason::ReadFailure);
    if(depth){
        uintptr_t range[2]{};
        if(!Member(read,renderer,8,range))return reject(Reason::ReadFailure);
        // A valid vector must have monotonic pointers and a whole 0x70 stride.
        if(range[1]<range[0] || (range[1]-range[0])%0x70)return reject(Reason::InvalidAddress);
        if(range[1]==range[0] && flags[1])return reject(Reason::DepthBranch);
    }
    unsigned shaderOffset=0x18;
    if(index==0){
        if(!Value<uintptr_t>(packetFields,0x18) && !Value<uintptr_t>(packetFields,0xf0)){
            // Native priority: ColorOnly precedes the pill test. PS enumCE
            // (56ff2a0) consumes only TEXCOORD1.z, independent of matMV.
            // The shader uses the same VS46 and common packet constants path.
            shaderOffset=0x60;
        }else{
            const auto x=Value<uint32_t>(packetFields,0xd0),y=Value<uint32_t>(packetFields,0xd4);
            if(!FiniteBits(x) || !FiniteBits(y))return reject(Reason::NonFiniteSelector);
            if((x&0x7fffffffu) || (y&0x7fffffffu))return reject(Reason::PillBranch);
        }
    }else{
        uint32_t saturation=0;
        if(!Member(read,preset,0xc8,saturation))return reject(Reason::ReadFailure);
        if(!FiniteBits(saturation))return reject(Reason::NonFiniteSelector);
        if(saturation!=0x3f800000u)return reject(Reason::SaturationBranch);
    }
    if(!Member(read,binding,shaderOffset,result.expectedShaderId))return reject(Reason::ReadFailure);
    if(result.expectedShaderId==0xffffffffu)return reject(Reason::InvalidShaderId);
    result.shaderOffset=shaderOffset;result.accepted=true;result.reason=Reason::Accepted;return result;
}
} // namespace ui_material_unlit_gate
