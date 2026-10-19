#include "ui_material_unlit_gate.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <thread>
#include <atomic>

namespace g=ui_material_unlit_gate;
struct Fixture {
    static constexpr uintptr_t Base=0x100000000,Binding=0x200000000,Packet=0x200001000,
        Renderer=0x200002000,Owner=0x200003000,Preset=0x200004000,Type=0x200005000;
    std::map<uintptr_t,std::vector<unsigned char>> regions;
    uintptr_t fail=0;size_t calls=0;unsigned index;
    explicit Fixture(unsigned i):index(i){
        for(auto address:{Binding,Packet,Renderer,Owner,Preset,Type})regions[address].resize(0x1000);
        regions[Base+0x80cf5c8].resize(8);regions[Base+0x7ee8970].resize(0x32);regions[Base+0x7eebe40].resize(0x32);
        Put(Binding,0,Base+(i==0?0x4fe14e0:0x4fe1528));Put(Binding,0x18,uint32_t(266));Put(Binding,0x60,uint32_t(269));
        Put(Base+0x80cf5c8,0,Renderer);Put(Renderer,0,Base+0x5c7ed70);
        Put(Packet,0,Owner);Put(Packet,8,Preset);Put(Packet,0x10,Type);Put(Packet,0x18,uintptr_t(123));
        Put(Preset,0xc8,uint32_t(0x3f800000));
        Put(Type,0x20,uint64_t(0x12));Put(Type,0x28,uint64_t(0x34));
        Put(Base+0x7ee8970,0x20,uint64_t(0x55));Put(Base+0x7eebe40,0x20,uint64_t(0xaa));
    }
    template<class T> void Put(uintptr_t address,size_t offset,T value){auto& r=regions.at(address);assert(offset+sizeof(value)<=r.size());std::memcpy(r.data()+offset,&value,sizeof(value));}
    bool Read(uintptr_t address,void* out,size_t length){++calls;if(address==fail)return false;
        auto it=regions.upper_bound(address);if(it==regions.begin())return false;--it;
        auto delta=address-it->first;if(delta>it->second.size() || length>it->second.size()-delta)return false;
        std::memcpy(out,it->second.data()+delta,length);return true;}
    g::Result Check(){return g::Evaluate(index,Base,Binding,Packet,[&](uintptr_t p,void* dst,size_t n){return Read(p,dst,n);});}
    void Reject(g::Reason expected){auto snapshot=regions;auto r=Check();assert(!r.accepted && r.reason==expected && regions==snapshot);}
};
static void TestBranches(){
    for(unsigned index:{0u,3u}){
        Fixture f(index);auto before=f.regions;auto r=f.Check();assert(r.accepted && r.expectedShaderId==266 && r.owner==f.Owner && r.shaderOffset==0x18 && before==f.regions);
        f.Put(f.Preset,0x82,uint8_t(1));f.Reject(g::Reason::DirtyPreset);f.Put(f.Preset,0x82,uint8_t(0));
        f.Put(f.Owner,0x1a0,uint8_t(1));f.Reject(g::Reason::LitBranch);f.Put(f.Preset,0x80,uint8_t(1));assert(f.Check().accepted);
        f.Put(f.Renderer,0x8e1,uint8_t(1));f.Put(f.Preset,0x81,uint8_t(1));f.Reject(g::Reason::DepthBranch);
        f.Put(f.Renderer,8,uintptr_t(0x8000));f.Put(f.Renderer,0x10,uintptr_t(0x8070));assert(f.Check().accepted);
        f.Put(f.Renderer,0x10,uintptr_t(0x8071));f.Reject(g::Reason::InvalidAddress);
        f.Put(f.Renderer,0x10,uintptr_t(0x7fff));f.Reject(g::Reason::InvalidAddress);
        f.Put(f.Renderer,0x8e1,uint8_t(0));
        f.Put(f.Binding,0x18,uint32_t(0xffffffff));f.Reject(g::Reason::InvalidShaderId);f.Put(f.Binding,0x18,uint32_t(266));
        f.fail=f.Binding+0x18;f.Reject(g::Reason::ReadFailure);f.fail=0;
        f.Put(f.Renderer,0,uintptr_t(1));f.Reject(g::Reason::RendererMismatch);f.Put(f.Renderer,0,f.Base+0x5c7ed70);
        f.Put(f.Binding,0,uintptr_t(1));f.Reject(g::Reason::BindingMismatch);
    }
    Fixture d(0);d.Put(d.Packet,0x18,uintptr_t(0));auto color=d.Check();assert(color.accepted && color.shaderOffset==0x60 && color.expectedShaderId==269);
    d.Put(d.Packet,0xf0,uintptr_t(99));assert(d.Check().accepted && d.Check().shaderOffset==0x18);
    d.Put(d.Packet,0xd0,uint32_t(0x80000000));assert(d.Check().accepted); // -0 is equal to +0 in original.
    d.Put(d.Packet,0xd0,uint32_t(1));d.Reject(g::Reason::PillBranch);
    d.Put(d.Packet,0xd0,uint32_t(0x7fc00000));d.Reject(g::Reason::NonFiniteSelector);
    Fixture rt(3);rt.Put(rt.Preset,0xc8,uint32_t(0));rt.Reject(g::Reason::SaturationBranch);
    rt.Put(rt.Preset,0xc8,uint32_t(0x7f800000));rt.Reject(g::Reason::NonFiniteSelector);
    for(unsigned unsupported:{1u,2u,4u}){Fixture f(unsupported);f.Reject(g::Reason::UnsupportedPreset);assert(f.calls==0);}
}
static void TestColorOnly(){
    Fixture f(0);f.Put(f.Packet,0x18,uintptr_t(0));
    // The runtime IDs are read, never assumed equal to another process.
    f.Put(f.Binding,0x60,uint32_t(0x12345678));
    for(uint32_t selector:{0u,0x3f800000u,0x7fc00000u,0x7f800000u}){
        f.Put(f.Packet,0xd0,selector);auto before=f.regions;auto r=f.Check();
        assert(r.accepted && r.shaderOffset==0x60 && r.expectedShaderId==0x12345678 && before==f.regions);
    }
    f.Put(f.Preset,0x82,uint8_t(1));f.Reject(g::Reason::DirtyPreset);f.Put(f.Preset,0x82,uint8_t(0));
    f.Put(f.Owner,0x1a0,uint8_t(1));f.Reject(g::Reason::LitBranch);f.Put(f.Owner,0x1a0,uint8_t(0));
    f.Put(f.Renderer,0x8e1,uint8_t(1));f.Put(f.Preset,0x81,uint8_t(1));f.Reject(g::Reason::DepthBranch);f.Put(f.Renderer,0x8e1,uint8_t(0));
    f.Put(f.Type,0x20,uint64_t(0x55));f.Put(f.Type,0x28,uint64_t(0));f.Reject(g::Reason::SpecialType);
    f.Put(f.Type,0x20,uint64_t(0x12));f.Put(f.Type,0x28,uint64_t(0x34));
    f.fail=f.Binding+0x60;f.Reject(g::Reason::ReadFailure);f.fail=0;
    f.Put(f.Binding,0x60,uint32_t(0xffffffff));f.Reject(g::Reason::InvalidShaderId);
    f.Put(f.Binding,0x60,uint32_t(273));f.Put(f.Binding,0x18,uint32_t(0xffffffff));assert(f.Check().accepted);
    // A real texture bypasses ColorOnly; unsupported pill and nonfinite
    // textured selectors retain their earlier rejection behavior.
    f.Put(f.Packet,0xf0,uintptr_t(77));f.Reject(g::Reason::NonFiniteSelector);
    f.Put(f.Packet,0xd0,uint32_t(0x3f800000));f.Reject(g::Reason::PillBranch);
    puts("PASS: ColorOnly priority, dynamic +60 ID, selector independence, dirty/special/lit/depth protection, read/ID failure");
}
static void TestTypes(){
    // Live persistent TypeInfo values: normal FZMaterialPreset is shallower
    // than AlphaOnly/Tint, so the negative-delta path is required for coverage.
    Fixture real(0);real.Put(real.Type,0x20,uint64_t(0));real.Put(real.Type,0x28,uint64_t(0x85962c6a57));real.Put(real.Type,0x30,int8_t(39));
    real.Put(real.Base+0x7ee8970,0x20,uint64_t(0));real.Put(real.Base+0x7ee8970,0x28,uint64_t(0x85962c6a57c));real.Put(real.Base+0x7ee8970,0x30,int8_t(43));
    real.Put(real.Base+0x7eebe40,0x20,uint64_t(0));real.Put(real.Base+0x7eebe40,0x28,uint64_t(0x85962c6a57d));real.Put(real.Base+0x7eebe40,0x30,int8_t(43));assert(real.Check().accepted);
    Fixture f(0);f.Put(f.Type,0x20,uint64_t(0x55));f.Put(f.Type,0x28,uint64_t(0));f.Reject(g::Reason::SpecialType);
    f.Put(f.Type,0x20,uint64_t(0xaa));f.Reject(g::Reason::SpecialType);
    f.Put(f.Type,0x31,uint8_t(4));f.Put(f.Base+0x7ee8970,0x31,uint8_t(1));f.Reject(g::Reason::SpecialType);
    f.Put(f.Type,0x31,uint8_t(0));f.Put(f.Type,0x30,int8_t(-1));assert(f.Check().accepted);
    f.Put(f.Type,0x30,int8_t(127));f.Put(f.Base+0x7ee8970,0x30,int8_t(-1));assert(f.Check().accepted);
    for(unsigned shift:{0u,1u,31u,63u,64u,65u,127u}){
        unsigned char source[0x32]{},target[0x32]{};uint64_t a=0xfedcba9876543210ull,b=0x123456789abcdef0ull;
        std::memcpy(source+0x20,&a,8);std::memcpy(source+0x28,&b,8);source[0x30]=static_cast<unsigned char>(shift);
        uint64_t first=shift<64?a>>shift:0;
        uint64_t second=shift==0?b:shift<64?(a<<(64-shift))|(b>>shift):a>>(shift-64);
        std::memcpy(target+0x20,&first,8);std::memcpy(target+0x28,&second,8);
        assert(g::MatchType(source,target)==g::TypeMatch::Yes);target[0x20]^=1;assert(g::MatchType(source,target)==g::TypeMatch::No);
    }
}
static void TestReadsAndConcurrency(){
    Fixture f(0);auto original=f.regions;
    for(auto address:{f.Binding,f.Base+0x80cf5c8,f.Renderer,f.Packet,f.Preset+0x80,f.Type,f.Base+0x7ee8970,f.Base+0x7eebe40,f.Owner+0x1a0,f.Renderer+0x8e1}){
        f.fail=address;f.Reject(g::Reason::ReadFailure);
    }
    size_t reads=0;auto noRead=[&](uintptr_t,void*,size_t){++reads;return false;};
    auto r=g::Evaluate(0,UINTPTR_MAX-1,f.Binding,f.Packet,noRead);assert(r.reason==g::Reason::InvalidAddress && !reads);
    std::atomic<unsigned> accepted{0};std::thread threads[4];
    for(unsigned t=0;t<4;++t)threads[t]=std::thread([t,&accepted](){Fixture local(t%2?3:0);auto saved=local.regions;
        for(unsigned i=0;i<10000;++i)if(local.Check().accepted)++accepted;assert(saved==local.regions);});
    for(auto& thread:threads)thread.join();assert(accepted==40000 && original==f.regions);
}
int main(){TestBranches();TestColorOnly();TestTypes();TestReadsAndConcurrency();puts("PASS: ordinary unlit and ColorOnly; type predicate boundaries; dirty/lit/depth/special variants skipped; read failures; zero mutation; 40,000 concurrent independent calls");}
