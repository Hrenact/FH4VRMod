#define FH4VR_UI_AFFINE_CLIP
#include <cassert>
#include <thread>
#include "ui_projection_probe.h"

struct Fixture {
    alignas(16) unsigned char renderer[0x620]{};
    struct Rectangle {float values[4];unsigned char enabled,padding[3];} rect{{10,20,300,400},1,{0xa5,0xa5,0xa5}};
    struct Stack {uintptr_t data;unsigned capacity,count;} stack{};
    float corrected[16]{},native[16]{},replacement[16]{};
    unsigned calls52=0,calls54=0,mode=0,depth=0;
    bool expectCorrected=true;
    void Init() {
        float identity[16]{};identity[0]=identity[5]=identity[10]=identity[15]=1;
        float projection[16];memcpy(projection,identity,64);
        projection[0]=.003f;projection[5]=-.004f;projection[10]=.002f;
        projection[3]=.0006f;projection[7]=-.0002f;projection[12]=-.6f;projection[13]=.7f;
        memcpy(renderer+0x2a0,identity,64);memcpy(renderer+0x2e0,identity,64);
        memcpy(renderer+0x320,identity,64);
        memcpy(renderer+0x360,projection,64);memcpy(renderer+0x3e0,projection,64);
        stack={reinterpret_cast<uintptr_t>(&rect),1,1};memcpy(renderer+0x270,&stack,sizeof(stack));
        assert(ui_affine_clip::ShaderPlanes(projection,rect.values,corrected));
        assert(ui_clip_projection::RectPlanes(projection,rect.values,native));
        assert(memcmp(corrected,native,64));memcpy(renderer+0x470,native,64);
        for(unsigned i=0;i<16;++i)replacement[i]=1000.f+i;
        ui_affine_clip::Remember(renderer,projection);
    }
};
static constexpr DWORD Incoming=0xface,Outgoing=0xcafe;
static const wchar_t Words[]=L"FH4 UI text";
static const float Color[4]={.125f,.25f,.5f,1.f};
static void Original54(void* object,const wchar_t* text,float x,float y,const void* color,float size,void* font) {
    auto& f=*static_cast<Fixture*>(object);
    assert(GetLastError()==Incoming);
    assert(text==Words && x==12.375f && y==-7.625f && color==Color && size==31.875f && font==&f.rect);
    assert(!memcmp(f.renderer+0x470,f.expectCorrected?f.corrected:f.native,64));
    ++f.calls54;SetLastError(Outgoing);
}
static void Original52(void* object,void* geometry,void* text) {
    auto& f=*static_cast<Fixture*>(object);
    assert(GetLastError()==Incoming && geometry==&f.stack && text==&f.rect);
    assert(!memcmp(f.renderer+0x470,f.expectCorrected?f.corrected:f.native,64));++f.calls52;
    if(f.mode==1 && !f.depth){
        ++f.depth;SetLastError(Incoming);
        ui_projection_probe::ObserveText52(object,geometry,text);
        assert(GetLastError()==Outgoing && !memcmp(f.renderer+0x470,f.corrected,64));
        SetLastError(Incoming);
        ui_projection_probe::ObserveText54(object,Words,12.375f,-7.625f,Color,31.875f,&f.rect);
        assert(GetLastError()==Outgoing && !memcmp(f.renderer+0x470,f.corrected,64));--f.depth;
    }
    if(f.mode==2)memcpy(f.renderer+0x470,f.replacement,64);
    SetLastError(Outgoing);
}
static uintptr_t OriginalPrepare(void* object) {
    assert(GetLastError()==Incoming);auto bytes=static_cast<unsigned char*>(object);
    memcpy(bytes+0x5b0,bytes+0x470,64);SetLastError(Outgoing);return 0x123456789abcdef0ULL;
}
static void Call52(Fixture& f) {
    SetLastError(Incoming);ui_projection_probe::ObserveText52(&f,&f.stack,&f.rect);assert(GetLastError()==Outgoing);
}
static void TestForwarding() {
    ui_projection_probe::originalText52=Original52;ui_projection_probe::originalText54=Original54;
    ui_projection_probe::originalPrepare=OriginalPrepare;
    stereo_trial::command=1;stereo_trial::deadline=0;
    Fixture f;f.Init();Call52(f);assert(f.calls52==1 && !memcmp(f.renderer+0x470,f.native,64));
    SetLastError(Incoming);ui_projection_probe::ObserveText54(&f,Words,12.375f,-7.625f,Color,31.875f,&f.rect);
    assert(GetLastError()==Outgoing && f.calls54==1 && !memcmp(f.renderer+0x470,f.native,64));
    f.mode=1;Call52(f);assert(f.calls52==3 && f.calls54==2 && !memcmp(f.renderer+0x470,f.native,64));
    f.mode=2;Call52(f);assert(!memcmp(f.renderer+0x470,f.replacement,64));
    memcpy(f.renderer+0x470,f.native,64);f.mode=0;
    SetLastError(Incoming);assert(ui_projection_probe::ObservePrepare(&f)==0x123456789abcdef0ULL);
    assert(GetLastError()==Outgoing && !memcmp(f.renderer+0x5b0,f.corrected,64));
    assert(!memcmp(f.renderer+0x470,f.native,64));
    stereo_trial::command=0;f.expectCorrected=false;Call52(f);
    stereo_trial::command=1;stereo_trial::deadline=1;Call52(f);stereo_trial::deadline=0;
    f.renderer[0x360]^=1;Call52(f);f.renderer[0x360]^=1;
    puts("PASS text ABI: register/stack float and pointer arguments, exactly-once calls, nested scopes, cache refresh, inactive/mismatched bypass and LastError");
    const auto before52=ui_projection_probe::text52Calls.load(),before54=ui_projection_probe::text54Calls.load();
    auto run=[](){Fixture own;own.Init();for(unsigned i=0;i<10000;++i){
        Call52(own);SetLastError(Incoming);
        ui_projection_probe::ObserveText54(&own,Words,12.375f,-7.625f,Color,31.875f,&own.rect);
        assert(GetLastError()==Outgoing && !memcmp(own.renderer+0x470,own.native,64));
    }assert(own.calls52==10000 && own.calls54==10000);};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    assert(ui_projection_probe::text52Calls-before52==40000 && ui_projection_probe::text54Calls-before54==40000);
    stereo_trial::command=0;
    puts("PASS 40000 calls per text entry across four threads with independent renderer caches");
}
static void TestHookRollback() {
    const size_t size=0x5c80000;
    auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE,PAGE_READWRITE));assert(image);
    for(size_t rva:{size_t(0x3e08000),size_t(0x3e0b000),size_t(0x5c7e000)})
        assert(VirtualAlloc(image+rva,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char s[]={0x0f,0x10,0x02,0x0f,0x11,0x81,0x60,0x03,0x00,0x00};
    const unsigned char p[]={0x48,0x8b,0xc4,0x53,0x48,0x81,0xec,0x50,0x01,0x00,0x00};
    const unsigned char t52[]={0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18};
    const unsigned char t54[]={0x48,0x85,0xd2,0x0f,0x84,0xc2,0x02,0x00,0x00,0x48,0x8b,0xc4};
    memcpy(image+0x3e0b0c0,s,sizeof(s));memcpy(image+0x3e0bcb0,p,sizeof(p));
    memcpy(image+0x3e08580,t52,sizeof(t52));memcpy(image+0x3e082b0,t54,sizeof(t54));
    auto table=reinterpret_cast<void**>(image+0x5c7ed70);
    const unsigned slots[4]={11,44,52,54};const uintptr_t rvas[4]={0x3e0b0c0,0x3e0bcb0,0x3e08580,0x3e082b0};
    for(unsigned i=0;i<4;++i)table[slots[i]]=image+rvas[i];
    for(unsigned fail=0;fail<=4;++fail){
        unsigned attempts=0;
        auto replace=[&](void* volatile* address,void* expected,void* next)->DWORD{
            bool installing=next==reinterpret_cast<void*>(&ui_projection_probe::Observe)||next==reinterpret_cast<void*>(&ui_projection_probe::ObservePrepare)||
                next==reinterpret_cast<void*>(&ui_projection_probe::ObserveText52)||next==reinterpret_cast<void*>(&ui_projection_probe::ObserveText54);
            if(installing && ++attempts==fail)return ERROR_ACCESS_DENIED;
            if(*address!=expected)return ERROR_REVISION_MISMATCH;*address=next;return ERROR_SUCCESS;
        };
        auto status=ui_projection_probe::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace);
        assert(status==static_cast<DWORD>(fail?ERROR_ACCESS_DENIED:ERROR_SUCCESS));
        if(!fail)ui_projection_probe::StopClipHooks(replace);
        for(unsigned i=0;i<4;++i)assert(table[slots[i]]==image+rvas[i]);
    }
    image[0x3e082b0]=0;unsigned mutations=0;
    auto reject=[&](void* volatile*,void*,void*)->DWORD{++mutations;return ERROR_SUCCESS;};
    assert(ui_projection_probe::Start(reinterpret_cast<uintptr_t>(image),nullptr,reject)==ERROR_REVISION_MISMATCH && mutations==0);
    VirtualFree(image,0,MEM_RELEASE);
    puts("PASS version guard and all four hook-install failure rollback points");
}
int main(){TestHookRollback();TestForwarding();}
