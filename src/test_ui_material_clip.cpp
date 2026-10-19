#include <cassert>
#include <stdexcept>
#include <thread>
#include <array>
#include "ui_material_clip.h"
namespace c=ui_material_clip;
template<class T> static void Put(unsigned char* memory,size_t offset,const T& value){memcpy(memory+offset,&value,sizeof(value));}
static void Identity(float* m){memset(m,0,64);for(unsigned i=0;i<4;++i)m[5*i]=1;}
struct Fixture {
    unsigned char* image=nullptr;uintptr_t base=0;
    unsigned char renderer[0x1000]{},binding[0x100]{},owner[0x200]{},preset[0x100]{},type[0x40]{},clip[20]{},packet[0x100]{};
    float mv[16]{},p[16]{},d[16]{};
    ui_root_probe::Frame root{};
    Fixture(){
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x80d0000,MEM_RESERVE,PAGE_NOACCESS));assert(image);base=reinterpret_cast<uintptr_t>(image);
        for(auto offset:{0x80cf5c8u,0x7ee8970u,0x7eebe40u})assert(VirtualAlloc(image+(offset&~4095u),4096,MEM_COMMIT,PAGE_READWRITE));
        Put(image,0x80cf5c8,reinterpret_cast<uintptr_t>(renderer));Put(renderer,0,base+0x5c7ed70);Put(renderer,0x40,uintptr_t(0x222));
        Put(binding,0,base+0x4fe14e0);Put(binding,0x18,uint32_t(266));Put(owner,0x1c8,uint32_t(266));
        Put(packet,0,reinterpret_cast<uintptr_t>(owner));Put(packet,8,reinterpret_cast<uintptr_t>(preset));Put(packet,0x10,reinterpret_cast<uintptr_t>(type));Put(packet,0x18,uintptr_t(0x123));
        Put(preset,0xc8,1.0f);Put(type,0x20,uint64_t(0x12));Put(type,0x28,uint64_t(0x34));Put(image,0x7ee8970+0x20,uint64_t(0x55));Put(image,0x7eebe40+0x20,uint64_t(0xaa));
        float a[16],b[16],l[16],s[16];Identity(a);Identity(b);Identity(l);Identity(s);Identity(p);Identity(d);
        p[0]=.002f;p[5]=-.002f;p[10]=.002f;p[3]=.0002f;p[7]=.0001f;p[12]=-.3f;p[13]=.2f;p[14]=4;p[15]=.9f;
        s[0]=s[5]=100;s[10]=.0001f;ui_affine_clip::Multiply(s,p,mv);
        d[0]=d[5]=4;d[10]=1.00004f;d[11]=1;d[14]=-.3f;d[15]=0;
        Put(renderer,0x2a0,a);Put(renderer,0x2e0,b);Put(renderer,0x320,l);Put(renderer,0x360,p);Put(renderer,0x3a0,d);Put(renderer,0x430,d);
        float rect[4]={0,0,100,100};Put(clip,0,rect);clip[16]=1;Put(renderer,0x270,reinterpret_cast<uintptr_t>(clip));Put(renderer,0x278,uint32_t(1));Put(renderer,0x27c,uint32_t(1));
        float planes[16]{};for(unsigned i=3;i<16;i+=4)planes[i]=-10;Put(renderer,0x470,planes);Put(packet,0x50,planes);
        uintptr_t pointers[]={reinterpret_cast<uintptr_t>(mv),reinterpret_cast<uintptr_t>(renderer+0x3a0),reinterpret_cast<uintptr_t>(renderer+0x430)};Put(packet,0x30,pointers);
        root.expectedRenderer=renderer;root.id=1;ui_affine_clip::Remember(renderer,p);c::fuse.store(false);
    }
    ~Fixture(){assert(VirtualFree(image,0,MEM_RELEASE));}
    void* Caller()const{return reinterpret_cast<void*>(base+0x1a7bd2f);}
    void Root(){ui_root_probe::current=&root;}
};
static void Basic(){
    Fixture f;f.Root();auto before=std::array<unsigned char,0x100>{};memcpy(before.data(),f.packet,0x100);
    auto renderer=std::array<unsigned char,0x1000>{};memcpy(renderer.data(),f.renderer,0x1000);
    auto count=c::applied.load();SetLastError(123);
    {c::Scope s(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);
        assert(GetLastError()==123 && c::applied.load()==count+1);
        uintptr_t pointers[3];memcpy(pointers,f.packet+0x30,24);auto modified=reinterpret_cast<float*>(pointers[0]);
        assert(modified!=f.mv && modified[3]==0 && modified[7]==0 && modified[11]==0 && modified[15]==1);
        double error=0;assert(ui_material_clip_math::detail::Preserved(f.mv,f.d,modified,reinterpret_cast<float*>(pointers[1]),error));
        unsigned char outer[0x100];memcpy(outer,f.packet,0x100);
        {c::Scope nested(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);assert(!memcmp(outer,f.packet,0x100));}
        f.packet[0x90]=42;SetLastError(456);s.Returned();assert(GetLastError()==456 && !c::fuse.load());
    }
    before[0x90]=42;assert(!memcmp(before.data(),f.packet,0x100));assert(!memcmp(renderer.data(),f.renderer,0x1000));assert(GetLastError()==456);
    bool caught=false;try {c::Scope s(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);SetLastError(789);throw std::runtime_error("native");}
    catch(const std::runtime_error&){caught=true;assert(GetLastError()==789);}assert(caught && !memcmp(before.data(),f.packet,0x100));
    // Do not clobber a legitimate native update merely because it occurred
    // inside the scope. Other two temporary pointers still have to be restored.
    auto changedCount=c::restoreChanged.load();uintptr_t nativePointer=reinterpret_cast<uintptr_t>(f.d);
    float nativePlanes[16]{};nativePlanes[0]=17;
    {c::Scope s(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);
        Put(f.packet,0x30,nativePointer);Put(f.packet,0x50,nativePlanes);}
    assert(!memcmp(f.packet+0x30,&nativePointer,8) && !memcmp(f.packet+0x38,before.data()+0x38,16));
    assert(!memcmp(f.packet+0x50,nativePlanes,64) && c::restoreChanged.load()==changedCount+2);
    memcpy(f.packet,before.data(),0x100);
    auto rejects=[&](bool active,void* caller,void* context){auto n=c::applied.load();c::Scope s(0,f.base,f.binding,context,f.packet,caller,active);assert(c::applied.load()==n && !memcmp(before.data(),f.packet,0x100));};
    rejects(false,f.Caller(),reinterpret_cast<void*>(0x222));rejects(true,nullptr,reinterpret_cast<void*>(0x222));rejects(true,f.Caller(),nullptr);
    ui_root_probe::current=nullptr;rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));f.Root();
    float changed=f.p[0]+.01f;Put(f.renderer,0x360,changed);rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));Put(f.renderer,0x360,f.p);
    f.clip[16]=0;rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));f.clip[16]=1;
    Put(f.clip,8,0.0f);rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));Put(f.clip,8,100.0f);
    f.owner[0x1a0]=1;rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));f.owner[0x1a0]=0;
    Put(f.renderer,0x320+12,.01f);rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));Put(f.renderer,0x320+12,0.0f);
    Put(f.owner,0x1c8,uint32_t(999));{c::Scope s(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);s.Returned();}
    assert(c::fuse.load());rejects(true,f.Caller(),reinterpret_cast<void*>(0x222));c::fuse.store(false);
    assert(!memcmp(before.data(),f.packet,0x100));ui_root_probe::current=nullptr;
    puts("PASS: compensated GPU matrices, exact gates, native color side effects, nesting, exception restoration, LastError, mismatch fuse");
}
static void Concurrent(){
    Fixture f;auto count=c::applied.load(),restores=c::restoreChanged.load();std::thread threads[4];
    for(auto& thread:threads)thread=std::thread([&](){ui_root_probe::Frame frame{};frame.id=2;frame.expectedRenderer=f.renderer;ui_root_probe::current=&frame;
        for(unsigned n=0;n<10000;++n){unsigned char packet[0x100];memcpy(packet,f.packet,0x100);SetLastError(333);
            {c::Scope s(0,f.base,f.binding,reinterpret_cast<void*>(0x222),packet,f.Caller(),true);assert(GetLastError()==333);s.Returned();}
            assert(!memcmp(packet,f.packet,0x100) && GetLastError()==333);}
        ui_root_probe::current=nullptr;});
    for(auto& thread:threads)thread.join();assert(c::applied.load()==count+40000 && c::restoreChanged.load()==restores && !c::fuse.load());
    puts("PASS: 40,000 concurrent disjoint stack packets restored, immutable shared renderer; does not assert same-renderer mutation safety");
}
static void ColorOnly(){
    Fixture f;f.Root();Put(f.packet,0x18,uintptr_t(0));
    Put(f.binding,0x60,uint32_t(817));Put(f.owner,0x1c8,uint32_t(817));
    // An unused Pill selector must not change the earlier native ColorOnly branch.
    Put(f.packet,0xd0,1.0f);Put(f.packet,0xd4,2.0f);
    unsigned char saved[0x100];memcpy(saved,f.packet,sizeof(saved));
    auto applied=c::colorOnlyApplied.load(),ordinary=c::ordinaryApplied.load();
    {c::Scope scope(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);
        assert(c::colorOnlyApplied.load()==applied+1 && c::ordinaryApplied.load()==ordinary);
        scope.Returned();assert(!c::fuse.load() && c::lastExpectedShader.load()==817);}
    assert(!memcmp(saved,f.packet,sizeof(saved)));
    // A different actual native choice must still trip the existing safety fuse.
    Put(f.owner,0x1c8,uint32_t(266));
    {c::Scope scope(0,f.base,f.binding,reinterpret_cast<void*>(0x222),f.packet,f.Caller(),true);scope.Returned();}
    assert(c::fuse.load() && !memcmp(saved,f.packet,sizeof(saved)));c::fuse.store(false);
    ui_root_probe::current=nullptr;
    puts("PASS: ColorOnly scoped compensation, native branch priority, dynamic pipeline verification and restoration");
}
int main(){Basic();Concurrent();ColorOnly();puts("ui_material_clip scope tests passed");}
