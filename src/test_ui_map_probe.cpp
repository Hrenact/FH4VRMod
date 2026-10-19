#include <cassert>
#include <stdexcept>
#include <thread>
#include <array>
#include "ui_map_probe.h"
namespace p=ui_map_probe;
static constexpr DWORD Incoming=0x1234,Outgoing=0x5678;
static std::atomic<uint64_t> mockCalls{0};
static thread_local void* expectedNode=nullptr;
static thread_local void* expectedContext=nullptr;
static thread_local bool nesting=false;
static bool Active(){SetLastError(0x9999);return true;}
static bool ThrowActivity(){SetLastError(0xabcd);throw std::runtime_error("diagnostic query");}
static void Mock(void* node,void* context){
    assert(node==expectedNode && context==expectedContext && GetLastError()==Incoming);++mockCalls;
    if(nesting){nesting=false;SetLastError(Incoming);p::Observe(node,context);assert(GetLastError()==Outgoing);}
    SetLastError(Outgoing);
}
static void Throw(void* node,void* context){assert(node==expectedNode && context==expectedContext && GetLastError()==Incoming);SetLastError(0xdead);throw std::runtime_error("native");}
static void Call(void* node,void* context){expectedNode=node;expectedContext=context;SetLastError(Incoming);p::Observe(node,context);assert(GetLastError()==Outgoing);}
template<class T> static void Put(unsigned char* memory,size_t offset,const T& value){memcpy(memory+offset,&value,sizeof(value));}
struct ImageFixture {
    unsigned char* memory=nullptr;uintptr_t base=0;
    ImageFixture(){memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x80d0000,MEM_RESERVE,PAGE_NOACCESS));assert(memory);base=reinterpret_cast<uintptr_t>(memory);
        for(auto offset:{p::FunctionRva,p::TableRva,p::ColRva,p::RendererPointerRva})assert(VirtualAlloc(memory+(offset&~uintptr_t(4095)),4096,MEM_COMMIT,PAGE_READWRITE));
        memcpy(memory+p::FunctionRva,p::Prefix,sizeof(p::Prefix));memcpy(memory+p::ColRva,p::ColValues,sizeof(p::ColValues));
        Put(memory,p::TableRva-8,base+p::ColRva);Restore();}
    ~ImageFixture(){assert(VirtualFree(memory,0,MEM_RELEASE));}
    void Restore(){Put(memory,p::CellRva,base+p::FunctionRva);}
    void* Value(){void* v=nullptr;memcpy(&v,memory+p::CellRva,8);return v;}
    bool Native(){return Value()==reinterpret_cast<void*>(base+p::FunctionRva);}
};
static void TestForwarding(){
    p::owner.store(nullptr);p::original.store(&Mock);p::imageBase.store(0);
    unsigned char node[0x200]{},context[256]{};for(unsigned i=0;i<256;++i){node[i]=static_cast<unsigned char>(i);context[i]=static_cast<unsigned char>(255-i);}
    auto savedNode=std::array<unsigned char,sizeof(node)>{};auto savedContext=std::array<unsigned char,sizeof(context)>{};
    memcpy(savedNode.data(),node,sizeof(node));memcpy(savedContext.data(),context,sizeof(context));
    ui_root_probe::Frame frame{},parent{};frame.id=14;frame.depth=2;frame.parent=&parent;parent.id=7;frame.expectedRenderer=node;
    ui_root_probe::current=&frame;auto copy=frame;nesting=true;auto before=mockCalls.load();Call(node,context);
    assert(mockCalls.load()==before+2 && ui_root_probe::current==&frame && !memcmp(&copy,&frame,sizeof(frame)));
    assert(!memcmp(node,savedNode.data(),sizeof(node)) && !memcmp(context,savedContext.data(),sizeof(context)));
    p::original.store(&Throw);expectedNode=node;expectedContext=context;auto returned=p::returns.load(),unwound=p::unwinds.load();
    SetLastError(Incoming);bool caught=false;try{p::Observe(node,context);}catch(const std::runtime_error&){caught=true;assert(GetLastError()==0xdead);}
    assert(caught && p::returns.load()==returned && p::unwinds.load()==unwound+1 && !p::activeCalls.load());
    assert(ui_root_probe::current==&frame);ui_root_probe::current=nullptr;p::original.store(&Mock);
    before=mockCalls.load();ui_root_probe::activeRoots.store(1);std::thread threads[4];
    for(unsigned t=0;t<4;++t)threads[t]=std::thread([t](){unsigned char ownNode[0x200]{},ownContext[256]{};
        memset(ownNode,static_cast<int>(t+1),sizeof(ownNode));memset(ownContext,static_cast<int>(t+7),sizeof(ownContext));
        for(unsigned n=0;n<10000;++n)Call(ownNode,ownContext);
        for(auto byte:ownNode)assert(byte==t+1);for(auto byte:ownContext)assert(byte==t+7);assert(!ui_root_probe::current);});
    for(auto& thread:threads)thread.join();ui_root_probe::activeRoots.store(0);
    assert(mockCalls.load()==before+40000 && !p::activeCalls.load() && p::outsideActive.load()==40000);
    puts("PASS: ABI arguments/context untouched, exactly once, nested TLS, native exception, LastError, 40,000 independent concurrent calls");
}
static void TestCapture(ImageFixture& image){
    unsigned char node[0x160]{},renderer[0x700]{},map[0x200]{},view[16]{},target[0x100]{},clip[20]{},context[256]{};
    p::imageBase.store(image.base);p::activityReader.store(&Active);
    Put(node,0,image.base+p::TableRva);Put(node,0xc0,uint32_t(6));Put(node,0x140,reinterpret_cast<uintptr_t>(map));Put(node,0x158,reinterpret_cast<uintptr_t>(view));
    Put(map,0,image.base+p::MapRendererVtableRva);Put(view,0,uintptr_t(0x554433));Put(view,8,reinterpret_cast<uintptr_t>(map));
    Put(map,0x78,uint32_t(9));float cb[52];for(unsigned i=0;i<52;++i)cb[i]=i+0.125f;Put(map,0xc0,cb);
    Put(image.memory,p::RendererPointerRva,reinterpret_cast<uintptr_t>(renderer));Put(renderer,0,image.base+p::RendererVtableRva);
    Put(renderer,0x40,reinterpret_cast<uintptr_t>(context));Put(renderer,0x70,reinterpret_cast<uintptr_t>(target));
    unsigned size[2]={3172,3172};Put(target,0x80,size);Put(target,0,uintptr_t(0x123123));
    Put(renderer,0x270,reinterpret_cast<uintptr_t>(clip));Put(renderer,0x278,uint32_t(1));Put(renderer,0x27c,uint32_t(1));clip[16]=1;
    float projection[16]{};for(unsigned i=0;i<4;++i)projection[i*5]=1;Put(renderer,0x360,projection);ui_affine_clip::Remember(renderer,projection);
    ui_root_probe::Frame root{};root.id=31;root.depth=1;root.expectedRenderer=renderer;root.context=context;ui_root_probe::current=&root;
    unsigned char nodeBefore[sizeof(node)],rendererBefore[sizeof(renderer)],mapBefore[sizeof(map)],contextBefore[sizeof(context)];
    memcpy(nodeBefore,node,sizeof(node));memcpy(rendererBefore,renderer,sizeof(renderer));memcpy(mapBefore,map,sizeof(map));memcpy(contextBefore,context,sizeof(context));
    SetLastError(0x2468);auto s=p::Capture(node,context,reinterpret_cast<void*>(0x111));assert(GetLastError()==0x2468);
    assert(s.valid==2047 && s.rootMatches && s.contextMatches && s.projectionMatches && s.vrKnown && s.vrActive);
    assert(s.flags==6 && s.mapRenderer==s.viewRenderer && s.mapConstantsValid && !memcmp(s.mapConstants,cb,sizeof(cb)) && s.mapState==9);
    assert(s.rendererState.valid && s.rendererState.clipValid && s.rendererState.dimensions[0]==3172);
    assert(!memcmp(nodeBefore,node,sizeof(node)) && !memcmp(rendererBefore,renderer,sizeof(renderer)) && !memcmp(mapBefore,map,sizeof(map)) && !memcmp(contextBefore,context,sizeof(context)));
    Put(view,8,uintptr_t(123));auto changed=p::Capture(node,context,nullptr);assert(!changed.mapConstantsValid);Put(view,8,reinterpret_cast<uintptr_t>(map));
    Put(map,0,uintptr_t(123));assert(!p::Capture(node,context,nullptr).mapConstantsValid);Put(map,0,image.base+p::MapRendererVtableRva);
    projection[0]=2;Put(renderer,0x360,projection);assert(!p::Capture(node,context,nullptr).projectionMatches);projection[0]=1;Put(renderer,0x360,projection);
    auto noContext=p::Capture(node,nullptr,nullptr);assert(!noContext.contextMatches);p::activityReader.store(&ThrowActivity);
    SetLastError(0x2468);auto queryThrows=p::Capture(node,context,nullptr);assert(!queryThrows.vrKnown && GetLastError()==0x2468);p::activityReader.store(&Active);
    auto invalid=p::Capture(reinterpret_cast<void*>(UINTPTR_MAX-7),context,nullptr);assert(!(invalid.valid&127));
    // Real entry/after logging; saved snapshots must never dereference their
    // correlation addresses later. Each root/VR bucket admits its own first
    // four and 1 Hz; native or outside calls cannot consume the VR/root budget.
    p::owner.store(GetModuleHandleW(nullptr));
    for(unsigned bucket=0;bucket<4;++bucket){p::eligibleCalls[bucket].store(0);p::lastLogTick[bucket].store(GetTickCount64());
        for(unsigned i=0;i<4;++i)assert(p::ShouldSample(bucket));assert(!p::ShouldSample(bucket));
        p::lastLogTick[bucket].store(GetTickCount64()-1000);assert(p::ShouldSample(bucket));assert(!p::ShouldSample(bucket));}
    assert(!p::ShouldSample(4));
    auto copied=s;copied.node=1;copied.renderer=1;copied.mapRenderer=1;copied.mapView=1;copied.viewRenderer=1;
    SetLastError(0x3456);p::Record("entry",777,copied);p::Record("return",777,copied,&s);assert(GetLastError()==0x3456);
    for(auto& count:p::eligibleCalls)count.store(0);p::original.store(&Mock);Call(node,context);
    p::original.store(&Throw);expectedNode=node;expectedContext=context;SetLastError(Incoming);bool caught=false;
    try{p::Observe(node,context);}catch(const std::runtime_error&){caught=true;assert(GetLastError()==0xdead);}assert(caught && !p::activeCalls.load());
    p::original.store(&Mock);p::owner.store(nullptr);assert(!p::ShouldSample(0));p::activityReader.store(nullptr);ui_root_probe::current=nullptr;
    puts("PASS: bounded synchronized-scope CPU snapshots, exact P/context identity, guarded map CB, immutable state, sample rate, logging and query/native exceptions");
}
struct Replacer {
    bool failInstall=false,afterInstall=false,failRemove=false,afterRemove=false,lie=false;
    DWORD operator()(void* volatile* cell,void* expected,void* next){
        const bool install=next==p::Hook(),fail=install?failInstall:failRemove,after=install?afterInstall:afterRemove;
        SetLastError(0xbad);if(fail && !after)return ERROR_ACCESS_DENIED;if(lie)return ERROR_SUCCESS;
        if(InterlockedCompareExchangePointer(cell,next,expected)!=expected)return ERROR_INVALID_DATA;
        return fail?ERROR_ACCESS_DENIED:ERROR_SUCCESS;}
};
static void TestInstallation(ImageFixture& image){
    auto success=Replacer{};SetLastError(0x1111);assert(p::Start(0,nullptr,success)==ERROR_REVISION_MISMATCH && GetLastError()==0x1111);
    assert(p::Start(UINTPTR_MAX-100,nullptr,success)==ERROR_REVISION_MISMATCH);
    for(uintptr_t at:{p::FunctionRva,p::ColRva,p::TableRva-8,p::CellRva}){
        image.memory[at]^=1;assert(p::Start(image.base,nullptr,success)==ERROR_REVISION_MISMATCH && !p::Pending());image.memory[at]^=1;assert(image.Native());}
    p::activeCalls.store(1);assert(p::Start(image.base,nullptr,success)==ERROR_BUSY);p::activeCalls.store(0);
    ui_root_probe::activeRoots.store(1);assert(p::Start(image.base,nullptr,success)==ERROR_BUSY);ui_root_probe::activeRoots.store(0);
    for(bool after:{false,true}){Replacer fail{};fail.failInstall=true;fail.afterInstall=after;SetLastError(0x2222);
        assert(p::Start(image.base,nullptr,fail,&Active)==ERROR_ACCESS_DENIED && GetLastError()==0x2222 && !p::Pending() && image.Native());}
    Replacer rollback{};rollback.failInstall=true;rollback.afterInstall=true;rollback.failRemove=true;
    assert(p::Start(image.base,nullptr,rollback)==ERROR_ACCESS_DENIED && p::Pending() && image.Value()==p::Hook());
    assert(p::Start(image.base,nullptr,success)==ERROR_ALREADY_EXISTS);assert(p::Stop(success)==ERROR_SUCCESS && !p::Pending() && image.Native());
    rollback.afterRemove=true;assert(p::Start(image.base,nullptr,rollback)==ERROR_ACCESS_DENIED && !p::Pending() && image.Native());
    Replacer lie{};lie.lie=true;assert(p::Start(image.base,nullptr,lie)==ERROR_WRITE_FAULT && !p::Pending() && image.Native());
    assert(p::Start(image.base,nullptr,success,&Active)==ERROR_SUCCESS && p::activityReader.load()==&Active);
    Replacer stop{};stop.failRemove=true;SetLastError(0x3333);assert(p::Stop(stop)==ERROR_ACCESS_DENIED && GetLastError()==0x3333 && p::Pending());
    assert(p::Stop(success)==ERROR_SUCCESS && !p::Pending() && image.Native());
    assert(p::Start(image.base,nullptr,success)==ERROR_SUCCESS);stop.afterRemove=true;
    assert(p::Stop(stop)==ERROR_ACCESS_DENIED && !p::Pending() && image.Native());
    assert(p::Start(image.base,nullptr,success)==ERROR_SUCCESS);Put(image.memory,p::CellRva,uintptr_t(0x777));
    assert(p::Stop(success)==ERROR_INVALID_DATA && !p::Pending() && image.Value()==reinterpret_cast<void*>(0x777));image.Restore();
    assert(p::Stop(success)==ERROR_SUCCESS);puts("PASS: native prefix/COL/slot, failure before/after CAS, rollback retry, lying helper, active guard and third-party preservation");
}
int main(){TestForwarding();ImageFixture image;TestCapture(image);TestInstallation(image);puts("ui_map_probe tests passed");}
