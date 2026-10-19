#include <cassert>
#include <stdexcept>
#include <thread>
#include <array>
#include "ui_map_mode_probe.h"
namespace p=ui_map_mode_probe;
static constexpr DWORD Incoming=0x1234,Outgoing=0x5678;
static thread_local void* expectedMode=nullptr;
static thread_local void* expectedBundle=nullptr;
static thread_local bool nest=false;
static std::atomic<uint64_t> nativeCalls{0},uploadCalls{0};
static p::Controls controls{};
static bool throwNative=false,throwUpload=false;
static void* expectedRenderer=nullptr;
static float uploaded[52]{};
static p::Controls Inputs(){SetLastError(0x7777);return controls;}
static void Native(void* mode,void* bundle){
    assert(GetLastError()==Incoming && mode==expectedMode && bundle==expectedBundle);++nativeCalls;
    if(throwNative){SetLastError(0xdead);throw std::runtime_error("native mode");}
    if(nest){nest=false;SetLastError(Incoming);p::Dispatch(0,mode,bundle,nullptr);assert(GetLastError()==Outgoing);}
    SetLastError(Outgoing);
}
static void Uploader(void* renderer,const void* source,uint32_t bytes){
    assert(renderer==expectedRenderer && source && bytes==0xd0);++uploadCalls;
    memcpy(uploaded,source,sizeof(uploaded));
    if(throwUpload){SetLastError(0xbeef);throw std::runtime_error("native upload");}
    SetLastError(0x9999);
}
static void Call(void* mode,void* bundle,void* caller=nullptr,unsigned index=0){
    expectedMode=mode;expectedBundle=bundle;SetLastError(Incoming);p::Dispatch(index,mode,bundle,caller);assert(GetLastError()==Outgoing);
}
template<class T> static void Put(unsigned char* object,size_t at,const T& value){memcpy(object+at,&value,sizeof(value));}
struct Image {
    unsigned char* memory;uintptr_t base;
    Image(){memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x80d0000,MEM_RESERVE,PAGE_NOACCESS));assert(memory);base=reinterpret_cast<uintptr_t>(memory);
        for(const auto& s:p::Specs){Commit(s.function);Commit(s.table);Commit(s.col);
            memcpy(memory+s.function,p::ModePrefix,sizeof(p::ModePrefix));
            uint32_t col[]={1,0,0,s.type,s.hierarchy,static_cast<uint32_t>(s.col)};Put(memory,s.col,col);Put(memory,s.table-8,base+s.col);}
        Commit(p::UploadRva);memcpy(memory+p::UploadRva,p::UploadPrefix,sizeof(p::UploadPrefix));
        Commit(p::CallsiteRva);memcpy(memory+p::CallsiteRva,p::CallsiteBytes,sizeof(p::CallsiteBytes));
        Commit(ui_map_probe::RendererPointerRva);Restore();}
    void Commit(uintptr_t offset){assert(VirtualAlloc(memory+(offset&~uintptr_t(4095)),4096,MEM_COMMIT,PAGE_READWRITE));}
    void Restore(){for(const auto& s:p::Specs)Put(memory,s.table+8,base+s.function);}
    void* Value(unsigned i){void* value=nullptr;memcpy(&value,memory+p::Specs[i].table+8,8);return value;}
    bool AllNative(){for(unsigned i=0;i<p::Count;++i)if(Value(i)!=reinterpret_cast<void*>(base+p::Specs[i].function))return false;return true;}
    ~Image(){assert(VirtualFree(memory,0,MEM_RELEASE));}
};
static void Forwarding(){
    p::owner.store(nullptr);p::controlsReader.store(&Inputs);controls={};
    for(auto& s:p::states)s.original.store(&Native);
    unsigned char mode[96]{},bundle[64]{};nest=true;auto before=nativeCalls.load();Call(mode,bundle);
    assert(nativeCalls.load()==before+2 && !p::activeCalls.load());
    throwNative=true;expectedMode=mode;expectedBundle=bundle;SetLastError(Incoming);bool caught=false;
    try{p::Dispatch(0,mode,bundle,nullptr);}catch(const std::runtime_error&){caught=true;assert(GetLastError()==0xdead);}
    assert(caught && !p::activeCalls.load());throwNative=false;
    before=nativeCalls.load();std::thread threads[4];
    for(auto& thread:threads)thread=std::thread([]{unsigned char a[96]{},b[64]{};for(unsigned n=0;n<10000;++n)Call(a,b);});
    for(auto& thread:threads)thread.join();assert(nativeCalls.load()==before+40000 && !p::activeCalls.load() && !uploadCalls.load());
    puts("PASS: void(two pointers), exactly once, native unwind/LastError, 40,000 concurrent passthrough calls");
}
struct Fixture {
    Image& image;unsigned char mode[96]{},bundle[64]{},node[0x160]{},view[24]{},map[0x200]{},renderer[0x700]{},backend[32]{},context[32]{},clip[20]{};
    ui_root_probe::Frame root{};ui_map_probe::MapScope* scope=nullptr;float projection[16]{},combined[16]{},cb[52]{};
    Fixture(Image& i):image(i){
        p::imageBase.store(i.base);p::upload.store(&Uploader);expectedRenderer=renderer;
        Put(mode,0,i.base+p::Specs[0].table);Put(mode,0x1a,uint8_t(0));Put(bundle,0,reinterpret_cast<uintptr_t>(renderer));
        Put(mode,0x38,uint32_t(1234));Put(bundle,0x18,uint32_t(1234));
        Put(node,0,i.base+ui_map_probe::TableRva);Put(node,0xc0,uint32_t(2));Put(node,0x140,reinterpret_cast<uintptr_t>(map));Put(node,0x158,reinterpret_cast<uintptr_t>(view));
        Put(view,8,reinterpret_cast<uintptr_t>(map));Put(map,0,i.base+ui_map_probe::MapRendererVtableRva);
        Put(map,0x78,uint32_t(1));Put(map,0x80,reinterpret_cast<uintptr_t>(context));Put(map,0x88,reinterpret_cast<uintptr_t>(bundle));
        Put(renderer,0,i.base+ui_map_probe::RendererVtableRva);Put(i.memory,ui_map_probe::RendererPointerRva,reinterpret_cast<uintptr_t>(renderer));
        Put(renderer,0x40,reinterpret_cast<uintptr_t>(context));Put(renderer,0x68,reinterpret_cast<uintptr_t>(backend));Put(backend,8,uintptr_t(0x445566));
        for(unsigned n=0;n<4;++n)projection[n*5]=combined[n*5]=1;
        projection[3]=combined[3]=.0008f;projection[7]=combined[7]=-.0003f;combined[14]=4;
        Put(renderer,0x360,projection);Put(renderer,0x3e0,combined);Put(renderer,0x420,uint8_t(1));
        ui_affine_clip::Remember(renderer,projection);
        float rectangle[]={3,3,708,372};Put(clip,0,rectangle);clip[16]=1;
        Put(renderer,0x270,reinterpret_cast<uintptr_t>(clip));Put(renderer,0x278,uint32_t(1));Put(renderer,0x27c,uint32_t(1));
        double k[16]{},c[16],m[16];for(unsigned n=0;n<4;++n)k[n*5]=1;k[7]=-1;k[15]=650;
        for(unsigned n=0;n<16;++n)c[n]=combined[n];ui_map_clip_math::detail::Product(k,c,m);
        for(unsigned n=0;n<16;++n){cb[n]=static_cast<float>(m[n]);cb[16+n]=float(n)+.0625f;cb[32+n]=float(n)-3.25f;}
        cb[48]=.75f;cb[49]=1234;cb[50]=567;cb[51]=0;
        Put(map,0xc0,cb);Put(renderer,0x470,*reinterpret_cast<float(*)[16]>(cb+32));
        root.id=101;root.expectedRenderer=renderer;root.context=context;ui_root_probe::current=&root;scope=new ui_map_probe::MapScope(node,context);
        controls={true,true};p::fused.store(false);
    }
    ~Fixture(){delete scope;ui_root_probe::current=nullptr;assert(!ui_map_probe::currentMap);}
    void* Caller(){return reinterpret_cast<void*>(image.base+p::ReturnRva);}
    void Invoke(){Call(mode,bundle,Caller());}
    template<class Mutation> void Reject(Mutation mutation,p::Reason reason){
        auto before=uploadCalls.load(),count=p::reasons[reason].load();mutation();Invoke();
        assert(uploadCalls.load()==before && p::reasons[reason].load()==count+1);
    }
};
static void Eligibility(Image& image){
    Fixture f(image);p::owner.store(nullptr);
    unsigned char savedMap[sizeof(f.map)],savedRenderer[sizeof(f.renderer)],savedNode[sizeof(f.node)];
    memcpy(savedMap,f.map,sizeof(f.map));memcpy(savedRenderer,f.renderer,sizeof(f.renderer));memcpy(savedNode,f.node,sizeof(f.node));
    auto before=uploadCalls.load();f.Invoke();assert(uploadCalls.load()==before+1);
    ui_map_clip_math::Result wanted;float rect[]={3,3,708,372};assert(ui_map_clip_math::Compute(f.cb,f.combined,rect,wanted));
    assert(!memcmp(uploaded,wanted.mv,64) && !memcmp(uploaded+32,wanted.planes,64));
    assert(!memcmp(uploaded+16,f.cb+16,64) && !memcmp(uploaded+48,f.cb+48,16));
    assert(!memcmp(savedMap,f.map,sizeof(f.map)) && !memcmp(savedRenderer,f.renderer,sizeof(f.renderer)) && !memcmp(savedNode,f.node,sizeof(f.node)));
    assert(wanted.mv[7]==-1 && wanted.mv[15]==650); // Preserve native map perspective.
    for(unsigned index=1;index<p::Count;++index){Put(f.mode,0,image.base+p::Specs[index].table);before=uploadCalls.load();
        Call(f.mode,f.bundle,f.Caller(),index);assert(uploadCalls.load()==before+1);}Put(f.mode,0,image.base+p::Specs[0].table);
    f.Reject([&]{ui_map_probe::currentMap=nullptr;},p::Scope);ui_map_probe::currentMap=&f.scope->frame;
    f.Reject([&]{ui_map_probe::mapTlsMismatches.store(1);},p::Scope);ui_map_probe::mapTlsMismatches.store(0);
    before=uploadCalls.load();Call(f.mode,f.bundle,reinterpret_cast<void*>(image.base+p::ReturnRva+1));assert(uploadCalls.load()==before);
    f.Reject([&]{Put(f.mode,0,uintptr_t(123));},p::ModeType);Put(f.mode,0,image.base+p::Specs[0].table);
    f.Reject([&]{Put(f.node,0xc0,uint32_t(0));},p::Node);Put(f.node,0xc0,uint32_t(2));
    f.Reject([&]{Put(f.view,8,uintptr_t(123));},p::MapIdentity);Put(f.view,8,reinterpret_cast<uintptr_t>(f.map));
    f.Reject([&]{Put(f.map,0,uintptr_t(123));},p::MapIdentity);Put(f.map,0,image.base+ui_map_probe::MapRendererVtableRva);
    f.Reject([&]{Put(f.bundle,0,uintptr_t(123));},p::Bundle);Put(f.bundle,0,reinterpret_cast<uintptr_t>(f.renderer));
    f.Reject([&]{Put(f.bundle,0x18,uint32_t(123));},p::Binding);Put(f.bundle,0x18,uint32_t(1234));
    f.Reject([&]{Put(f.map,0x80,uintptr_t(123));},p::Context);Put(f.map,0x80,reinterpret_cast<uintptr_t>(f.context));
    f.Reject([&]{Put(f.backend,8,uintptr_t(0));},p::Context);Put(f.backend,8,uintptr_t(0x445566));
    f.Reject([&]{Put(f.map,0x78,uint32_t(2));},p::MapState);Put(f.map,0x78,uint32_t(1));
    f.Reject([&]{Put(f.map,0x1d0,uint8_t(1));},p::MapState);Put(f.map,0x1d0,uint8_t(0));
    f.Reject([&]{Put(f.renderer,0x360,2.f);},p::Projection);Put(f.renderer,0x360,f.projection);
    f.Reject([&]{f.clip[16]=0;},p::Clip);f.clip[16]=1;
    f.Reject([&]{Put(f.clip,8,0.f);},p::Clip);Put(f.clip,8,708.f);
    f.Reject([&]{Put(f.renderer,0x420,uint8_t(0));},p::Cache);Put(f.renderer,0x420,uint8_t(1));
    f.Reject([&]{Put(f.map,0x140,99.f);},p::Cache);Put(f.map,0xc0,f.cb);
    float zeros[16]{};f.Reject([&]{Put(f.renderer,0x3e0,zeros);},p::Math);Put(f.renderer,0x3e0,f.combined);
    f.Reject([&]{p::upload.store(nullptr);},p::NoUpload);p::upload.store(&Uploader);
    auto token=p::CurrentToken();p::Snapshot a,b;assert(p::Capture(0,f.mode,f.bundle,f.Caller(),token,a)==p::Eligible);b=a;
    assert(p::Same(a,b));b.cb[16]+=1;assert(!p::Same(a,b));b=a;b.command++;assert(!p::Same(a,b));
    b=a;b.boundPipeline++;assert(!p::Same(a,b));
    // Disabling VR prevents correction immediately, but does not itself restore
    // a GPU binding. The next native map Begin uploads the untouched CPU CB.
    before=uploadCalls.load();controls.vr=false;f.Invoke();assert(uploadCalls.load()==before);
    assert(!memcmp(uploaded,wanted.mv,64));
    Uploader(f.renderer,f.cb,sizeof(f.cb)); // Model 4133410 -> 413fbd0 native Begin.
    before=uploadCalls.load();f.Invoke();assert(uploadCalls.load()==before && !memcmp(uploaded,f.cb,sizeof(f.cb)));
    // Re-enabling VR takes effect on the very next eligible callback. There is
    // no key, release-to-arm state, timer or one-shot latch.
    controls.vr=true;f.Invoke();assert(uploadCalls.load()==before+1 && !memcmp(uploaded,wanted.mv,64));
    for(unsigned n=0;n<3;++n)f.Invoke();assert(uploadCalls.load()==before+4);
    before=uploadCalls.load();controls.foreground=false;f.Invoke();assert(uploadCalls.load()==before);
    Uploader(f.renderer,f.cb,sizeof(f.cb)); // Next native Begin after focus loss.
    before=uploadCalls.load();f.Invoke();assert(uploadCalls.load()==before && !memcmp(uploaded,f.cb,sizeof(f.cb)));
    controls.foreground=true;f.Invoke();assert(uploadCalls.load()==before+1 && !memcmp(uploaded,wanted.mv,64));
    // VR and foreground are independent requirements; focus alone cannot enable.
    controls.vr=false;controls.foreground=false;before=uploadCalls.load();f.Invoke();
    controls.foreground=true;f.Invoke();assert(uploadCalls.load()==before);
    controls.vr=true;f.Invoke();assert(uploadCalls.load()==before+1);
    // An unavailable activity/control reader is fail closed and recovers without
    // a key transition when the reader becomes available again.
    before=uploadCalls.load();p::controlsReader.store(nullptr);f.Invoke();assert(uploadCalls.load()==before);
    p::controlsReader.store(&Inputs);f.Invoke();assert(uploadCalls.load()==before+1);
    throwUpload=true;expectedMode=f.mode;expectedBundle=f.bundle;SetLastError(Incoming);bool caught=false;
    try{p::Dispatch(0,f.mode,f.bundle,f.Caller());}catch(const std::runtime_error&){caught=true;assert(GetLastError()==0xbeef);}
    assert(caught && p::fused.load() && !p::activeCalls.load());throwUpload=false;before=uploadCalls.load();f.Invoke();assert(uploadCalls.load()==before);p::fused.store(false);
    p::owner.store(GetModuleHandleW(nullptr));p::Record(0,a,p::Eligible,true,true,&wanted);p::owner.store(nullptr);
    assert(!memcmp(savedMap,f.map,sizeof(f.map)) && !memcmp(savedRenderer,f.renderer,sizeof(f.renderer)) && !memcmp(savedNode,f.node,sizeof(f.node)));
    puts("PASS: all seven mode guards, native perspective kept, exact MVP/metadata, no shared CPU changes, context/projection/clip rejection, automatic VR/focus lifecycle, next-Begin restore, upload exception fuse");
}
struct Replacer {
    int failIndex=-1;bool afterInstall=false,failRemove=false,afterRemove=false,lie=false;
    DWORD operator()(void* volatile* slot,void* expected,void* next){
        int index=-1;for(unsigned i=0;i<p::Count;++i)if(next==p::Hook(i))index=static_cast<int>(i);
        bool installing=index>=0,fail=installing?index==failIndex:failRemove,after=installing?afterInstall:afterRemove;
        SetLastError(0xbad);if(fail && !after)return ERROR_ACCESS_DENIED;if(lie)return ERROR_SUCCESS;
        if(InterlockedCompareExchangePointer(slot,next,expected)!=expected)return ERROR_INVALID_DATA;return fail?ERROR_ACCESS_DENIED:ERROR_SUCCESS;
    }
};
static void Installation(Image& image){
    Replacer good;SetLastError(0x2222);assert(p::Start(0,nullptr,good)==ERROR_REVISION_MISMATCH && GetLastError()==0x2222);
    for(uintptr_t at:{p::UploadRva,p::CallsiteRva,p::CallsiteRva+9,p::Specs[0].function,p::Specs[0].col,p::Specs[0].table-8,p::Specs[0].table+8}){
        image.memory[at]^=1;assert(p::Start(image.base,nullptr,good)==ERROR_REVISION_MISMATCH && !p::Pending());image.memory[at]^=1;}
    p::activeCalls.store(1);assert(p::Start(image.base,nullptr,good)==ERROR_BUSY);p::activeCalls.store(0);
    ui_map_probe::activeCalls.store(1);assert(p::Start(image.base,nullptr,good)==ERROR_BUSY);ui_map_probe::activeCalls.store(0);
    for(int index=0;index<7;++index)for(bool after:{false,true}){Replacer bad;bad.failIndex=index;bad.afterInstall=after;
        assert(p::Start(image.base,nullptr,bad)==ERROR_ACCESS_DENIED && !p::Pending() && image.AllNative());}
    Replacer bad;bad.failIndex=3;bad.afterInstall=true;bad.failRemove=true;
    assert(p::Start(image.base,nullptr,bad)==ERROR_ACCESS_DENIED && p::Pending());assert(p::Start(image.base,nullptr,good)==ERROR_ALREADY_EXISTS);
    assert(p::Stop(good)==ERROR_SUCCESS && !p::Pending() && image.AllNative());bad.afterRemove=true;
    assert(p::Start(image.base,nullptr,bad)==ERROR_ACCESS_DENIED && !p::Pending() && image.AllNative());
    Replacer lie;lie.lie=true;assert(p::Start(image.base,nullptr,lie)==ERROR_WRITE_FAULT && !p::Pending() && image.AllNative());
    assert(p::Start(image.base,nullptr,good)==ERROR_SUCCESS);Replacer stop;stop.failRemove=true;SetLastError(0x3333);
    assert(p::Stop(stop)==ERROR_ACCESS_DENIED && GetLastError()==0x3333 && p::Pending());assert(p::Stop(good)==ERROR_SUCCESS && image.AllNative());
    assert(p::Start(image.base,nullptr,good)==ERROR_SUCCESS);Put(image.memory,p::Specs[2].table+8,uintptr_t(0x888));
    assert(p::Stop(good)==ERROR_INVALID_DATA && !p::Pending() && image.Value(2)==reinterpret_cast<void*>(0x888));image.Restore();
    assert(p::Stop(good)==ERROR_SUCCESS);puts("PASS: seven prefix/COL/slot guards, before/after-CAS failure at each slot, partial rollback retry, stop errors, third-party slot preservation");
}
int main(){Forwarding();Image image;Eligibility(image);Installation(image);puts("ui_map_mode_probe tests passed");}
