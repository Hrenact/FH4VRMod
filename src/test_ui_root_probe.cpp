#include <cassert>
#include <stdexcept>
#include <thread>
#include "ui_root_probe.h"

namespace p=ui_root_probe;
constexpr DWORD Incoming=0x1234abcd,Outgoing=0x87654321;
struct Fixture {
    alignas(16) unsigned char ui[0xe0]{};
    alignas(16) unsigned char renderer[0x700]{};
    unsigned char application[0x60]{},manager[0x90]{},target[0x90]{},context[8]{};
    uintptr_t managerTable[16]{};unsigned calls=0;Fixture* child=nullptr;bool throws=false;
    Fixture(){
        auto put=[](void* object,size_t offset,uintptr_t value){memcpy(static_cast<char*>(object)+offset,&value,8);};
        put(ui,0xd0,reinterpret_cast<uintptr_t>(renderer));put(ui,0xd8,reinterpret_cast<uintptr_t>(application));
        put(application,0x48,reinterpret_cast<uintptr_t>(manager));put(manager,0,reinterpret_cast<uintptr_t>(managerTable));
        managerTable[14]=0xabcdef14;managerTable[15]=0xabcdef15;
        put(renderer,0x40,reinterpret_cast<uintptr_t>(context));put(renderer,0x48,0xface);
        put(renderer,0x68,reinterpret_cast<uintptr_t>(renderer)+0x40);put(renderer,0x50,reinterpret_cast<uintptr_t>(target));
        put(renderer,0x70,reinterpret_cast<uintptr_t>(target));put(target,0,0xcafe);
        unsigned size[2]={3172,3172};memcpy(target+0x80,size,8);
        for(unsigned i=0;i<16;++i){float f=float(i);memcpy(renderer+0x360+i*4,&f,4);}
    }
};
static void Mock(void* object,void* context) {
    auto& f=*static_cast<Fixture*>(object);assert(GetLastError()==Incoming);
    assert(context==f.context && p::current && p::current->ui==object && p::current->context==context);
    assert(p::current->expectedRenderer==f.renderer);auto frame=p::current;++f.calls;
    p::ObserveEvent("setter",f.renderer,reinterpret_cast<void*>(0x1111),true);
    p::ObserveEvent("prepare",f.renderer,reinterpret_cast<void*>(0x2222));
    p::ObserveEvent("draw",f.context,reinterpret_cast<void*>(0x3333));
    assert(GetLastError()==Incoming);
    if(f.child){SetLastError(Incoming);p::Observe(f.child,f.child->context);assert(GetLastError()==Outgoing && p::current==frame);}
    assert(frame->direct[0].count==1 && frame->direct[0].matched==1 && frame->direct[0].changed==1);
    assert(frame->direct[7].different==1);
    if(f.child){assert(frame->inclusive[0].count==2 && frame->inclusive[0].matched==1 && frame->inclusive[0].different==1);}
    if(f.throws){SetLastError(0xdead);throw std::runtime_error("mock unwind");}
    SetLastError(Outgoing);
}
static void Call(Fixture& f){SetLastError(Incoming);p::Observe(&f,f.context);assert(GetLastError()==Outgoing && !p::current);}
static void Forwarding(){
    p::original=Mock;p::owner=nullptr;Fixture parent,child;parent.child=&child;
    auto before=p::roots.load();Call(parent);assert(parent.calls==1 && child.calls==1 && p::roots-before==2);
    assert(!p::activeRoots && p::maximumDepth>=2);
    parent.child=nullptr;parent.throws=true;auto beforeUnwind=p::unwinds.load();
    try {Call(parent);assert(false);}catch(const std::runtime_error&){assert(!p::current && !p::activeRoots && GetLastError()==0xdead);}
    assert(p::unwinds-beforeUnwind==1);
    SetLastError(Incoming);auto outside=p::totals[2].outside.load();p::ObserveEvent("text52",parent.renderer,nullptr);
    assert(GetLastError()==Incoming && p::totals[2].outside-outside==1);
    auto unknown=p::totals[8].outside.load();p::ObserveEvent("unrecognized",nullptr,nullptr);assert(p::totals[8].outside-unknown==1);
    before=p::roots.load();auto returned=p::returns.load(),setters=p::totals[0].inScope.load();
    auto run=[](){Fixture f;unsigned char originalUi[sizeof(f.ui)],originalRenderer[sizeof(f.renderer)];
        memcpy(originalUi,f.ui,sizeof(f.ui));memcpy(originalRenderer,f.renderer,sizeof(f.renderer));
        for(unsigned i=0;i<10000;++i)Call(f);
        assert(f.calls==10000 && !memcmp(originalUi,f.ui,sizeof(f.ui)) && !memcmp(originalRenderer,f.renderer,sizeof(f.renderer)));};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    assert(p::roots-before==40000 && p::returns-returned==40000 && p::totals[0].inScope-setters==40000 && !p::activeRoots);
    // A worker callback must remain outside this thread's root, even with the same renderer.
    {p::Scope scope(&parent,parent.context,nullptr);auto old=p::totals[3].outsideWhileRootActive.load();
        std::thread worker([&](){p::ObserveEvent("text54",parent.renderer,nullptr);});worker.join();
        assert(p::totals[3].outsideWhileRootActive-old==1 && !scope.frame.inclusive[3].count);scope.frame.returned=true;}
    puts("PASS root: arguments, incoming/outgoing LastError, exactly once, nested scopes, exception unwinding, 40000 independent multithread calls, cross-thread outside detection");
}
static void Snapshots(){
    Fixture f;unsigned char before[sizeof(f.renderer)];memcpy(before,f.renderer,sizeof(before));
    SetLastError(Incoming);auto s=p::Capture(&f);assert(GetLastError()==Incoming);
    assert(s.renderer==reinterpret_cast<uintptr_t>(f.renderer) && s.application==reinterpret_cast<uintptr_t>(f.application));
    assert(s.manager14==0xabcdef14 && s.manager15==0xabcdef15 && s.commandList==0xface && s.resource==0xcafe);
    assert(s.dimensions[0]==3172 && s.dimensions[1]==3172 && s.projection[15]==15 && s.defaultTarget==s.target);
    assert(!memcmp(before,f.renderer,sizeof(before)));
    auto draw=p::CaptureRenderer(f.renderer,reinterpret_cast<void*>(0x123),true);
    assert(draw.valid && draw.changed && draw.matrices[0][15]==15 && draw.resource==0xcafe && draw.dimensions[0]==3172);
    {p::Scope scope(&f,f.context,nullptr);scope.frame.samples=new p::CallbackSamples;
        for(unsigned i=0;i<20;++i)p::ObserveEvent("draw",f.renderer,reinterpret_cast<void*>(0x123));
        assert(scope.frame.samples->counts[7]==1);
        for(uintptr_t target=1;target<10;++target){memcpy(f.renderer+0x70,&target,8);p::ObserveEvent("draw",f.renderer,nullptr);}
        assert(scope.frame.samples->counts[7]==p::SamplesPerEvent);scope.frame.returned=true;}
    auto invalid=p::Capture(reinterpret_cast<void*>(1));assert(!invalid.valid && !invalid.renderer && GetLastError()==Incoming);
    puts("PASS root snapshots: renderer/application/manager/context/target/projection identity, invalid pointers, read-only and LastError");
}
static void Installation(){
    constexpr size_t Size=0x5b9c000;
    auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,Size,MEM_RESERVE,PAGE_READWRITE));assert(image);
    assert(VirtualAlloc(image+0x3ab9000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(image+0x5b9b000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    memcpy(image+p::FunctionRva,p::Prefix,sizeof(p::Prefix));auto address=reinterpret_cast<void**>(image+p::TableRva+8*p::SlotIndex);
    *address=image+p::FunctionRva;unsigned attempts=0;int mode=0;
    auto replace=[&](void* volatile* at,void* expected,void* next)->DWORD {
        ++attempts;SetLastError(0xbad);assert(at==address);
        if(*at!=expected)return ERROR_REVISION_MISMATCH;
        bool install=next==reinterpret_cast<void*>(&p::Observe);
        if(mode==1 && install)return ERROR_ACCESS_DENIED;
        if((mode==3 || mode==4) && !install)return ERROR_ACCESS_DENIED;
        *at=next;if((mode==2 || mode==4 || mode==6) && install)return ERROR_WRITE_FAULT;
        if((mode==5 || mode==6) && !install)return ERROR_ACCESS_DENIED;
        return ERROR_SUCCESS;
    };
    for(mode=0;mode<=2;++mode){
        attempts=0;SetLastError(Incoming);auto result=p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace);assert(GetLastError()==Incoming);
        if(mode==0){assert(result==ERROR_SUCCESS && *address==reinterpret_cast<void*>(&p::Observe));
            assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_ALREADY_EXISTS);
            mode=3;assert(p::Stop(replace)==ERROR_ACCESS_DENIED && p::slot);mode=0;
            assert(p::Stop(replace)==ERROR_SUCCESS && !p::slot && GetLastError()==Incoming);}
        else assert(result==DWORD(mode==1?ERROR_ACCESS_DENIED:ERROR_WRITE_FAULT) && !p::slot);
        assert(*address==image+p::FunctionRva);
    }
    mode=4;SetLastError(Incoming);
    assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_ACCESS_DENIED && p::slot && GetLastError()==Incoming);
    assert(*address==reinterpret_cast<void*>(&p::Observe));mode=0;
    assert(p::Stop(replace)==ERROR_SUCCESS && !p::slot && *address==image+p::FunctionRva);
    assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_SUCCESS);
    mode=5;assert(p::Stop(replace)==ERROR_ACCESS_DENIED && !p::slot && *address==image+p::FunctionRva);
    auto oldAttempts=attempts;assert(p::Stop(replace)==ERROR_SUCCESS && attempts==oldAttempts);
    mode=6;assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_ACCESS_DENIED && !p::slot && *address==image+p::FunctionRva);
    assert(GetLastError()==Incoming);mode=0;
    attempts=0;image[p::FunctionRva]^=1;
    assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_REVISION_MISMATCH && attempts==0);image[p::FunctionRva]^=1;
    *address=nullptr;assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_REVISION_MISMATCH && attempts==0);
    *address=image+p::FunctionRva;
    {Fixture f;p::Scope scope(&f,f.context,nullptr);assert(p::Start(reinterpret_cast<uintptr_t>(image),nullptr,replace)==ERROR_BUSY);scope.frame.returned=true;}
    assert(p::Stop(replace)==ERROR_SUCCESS);assert(VirtualFree(image,0,MEM_RELEASE));
    puts("PASS root installation: target/prefix validation, single install, failure rollback before/after mutation, retryable Stop failure, active-scope rejection and LastError");
}
int main(){Forwarding();Snapshots();Installation();puts("PASS all UI root probe tests");}
