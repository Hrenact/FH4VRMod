#include <cassert>
#include <stdexcept>
#include <thread>
#include <array>
#include <string>
#include "ui_material_probe.h"

namespace p=ui_material_probe;
static std::atomic<uint64_t> originals[4]{};
static constexpr DWORD Incoming=0x1234,Outgoing=0x5678;
static thread_local bool nested=false;
template<unsigned I> static void Mock(void* a,void* b,void* c) {
    assert(a==reinterpret_cast<void*>(0x111));assert(b==reinterpret_cast<void*>(0x222));assert(c==reinterpret_cast<void*>(0x333));
    assert(GetLastError()==Incoming);++originals[I];
    if constexpr(I==0)if(nested){nested=false;SetLastError(Incoming);p::Observe<1>(a,b,c);assert(GetLastError()==Outgoing+1);}
    SetLastError(Outgoing+I);
}
static void Throw(void*,void*,void*) {assert(GetLastError()==Incoming);SetLastError(0xdead);throw std::runtime_error("original");}
static void Call(unsigned i) {
    auto fn=reinterpret_cast<p::Draw>(p::Hook(i));SetLastError(Incoming);
    fn(reinterpret_cast<void*>(0x111),reinterpret_cast<void*>(0x222),reinterpret_cast<void*>(0x333));
    assert(GetLastError()==Outgoing+i);
}
static void ConfigureMocks() {
    p::owner.store(nullptr);p::imageBase.store(0);
    p::states[0].original.store(&Mock<0>);p::states[1].original.store(&Mock<1>);
    p::states[2].original.store(&Mock<2>);p::states[3].original.store(&Mock<3>);
}
static void TestForwarding() {
    ConfigureMocks();ui_root_probe::Frame frame{};frame.id=42;frame.depth=1;frame.expectedRenderer=reinterpret_cast<void*>(0x444);
    auto copy=frame;ui_root_probe::current=&frame;
    for(unsigned i=0;i<4;++i)Call(i);
    nested=true;Call(0);
    assert(ui_root_probe::current==&frame);assert(!memcmp(&copy,&frame,sizeof(frame)));ui_root_probe::current=nullptr;
    for(unsigned i=0;i<4;++i){assert(p::states[i].outside.load()==0);assert(p::states[i].samples.load()==0);}
    auto oldReturns=p::states[0].returned.load();auto oldUnwinds=p::states[0].unwinds.load();
    p::states[0].original.store(&Throw);bool caught=false;SetLastError(Incoming);
    try {p::Observe<0>(nullptr,nullptr,nullptr);}catch(const std::runtime_error&){caught=true;assert(GetLastError()==0xdead);}
    assert(caught && !p::activeCalls.load());assert(p::states[0].returned.load()==oldReturns);
    assert(p::states[0].unwinds.load()==oldUnwinds+1);p::states[0].original.store(&Mock<0>);
    uint64_t before=0;for(auto& value:originals)before+=value.load();
    ui_root_probe::activeRoots.store(1);
    std::thread threads[4];for(unsigned t=0;t<4;++t)threads[t]=std::thread([t](){for(unsigned n=0;n<10000;++n)Call((t+n)%4);});
    for(auto& thread:threads)thread.join();ui_root_probe::activeRoots.store(0);
    uint64_t after=0;for(auto& value:originals)after+=value.load();assert(after-before==40000);
    for(auto& state:p::states){assert(state.outsideActive.load()==10000);assert(state.samples.load()==0);}
    assert(!p::activeCalls.load());puts("PASS: 40,000 concurrent calls, nesting, TLS unchanged, exactly once, LastError and exception propagation");
}

struct ImageFixture {
    unsigned char* memory=nullptr;uintptr_t base=0;
    ImageFixture(){memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x80d0000,MEM_RESERVE,PAGE_NOACCESS));assert(memory);base=reinterpret_cast<uintptr_t>(memory);
        for(const auto& spec:p::Specs){Commit(spec.function);Commit(spec.table);memcpy(memory+spec.function,spec.prefix,spec.length);}
        Commit(p::RendererPointerRva);Restore();}
    ~ImageFixture(){assert(VirtualFree(memory,0,MEM_RELEASE));}
    void Commit(uintptr_t rva){assert(VirtualAlloc(memory+(rva&~uintptr_t(4095)),4096,MEM_COMMIT,PAGE_READWRITE));}
    void Restore(){for(const auto& spec:p::Specs){auto native=reinterpret_cast<void*>(base+spec.function);memcpy(memory+spec.table,&native,8);}}
    void* Value(unsigned i)const{void* value=nullptr;memcpy(&value,memory+p::Specs[i].table,8);return value;}
    bool Native(unsigned i)const{return Value(i)==reinterpret_cast<void*>(base+p::Specs[i].function);}
    void AllNative()const{for(unsigned i=0;i<4;++i)assert(Native(i));}
};
template<class T> static void Put(unsigned char* object,size_t offset,const T& value){memcpy(object+offset,&value,sizeof(value));}
static void TestCapture(ImageFixture& image) {
    unsigned char binding[16]{},packet[0x100]{},renderer[0x700]{},target[0x100]{},clip[20]{};
    float matrices[3][16]{};for(unsigned m=0;m<3;++m)for(unsigned n=0;n<16;++n)matrices[m][n]=static_cast<float>(100*m+n)+0.25f;
    uintptr_t bindingVt=0x771100,rendererVt=image.base+p::RendererVtableRva;
    Put(binding,0,bindingVt);Put(renderer,0,rendererVt);
    auto rendererAddress=reinterpret_cast<uintptr_t>(renderer);Put(image.memory,p::RendererPointerRva,rendererAddress);p::imageBase.store(image.base);
    for(unsigned m=0;m<3;++m){auto pointer=reinterpret_cast<uintptr_t>(matrices[m]);Put(packet,0x30+8*m,pointer);}
    for(unsigned i=0;i<64;++i)packet[0x50+i]=static_cast<unsigned char>(i+1);
    auto clipAddress=reinterpret_cast<uintptr_t>(clip),targetAddress=reinterpret_cast<uintptr_t>(target);
    Put(renderer,0x270,clipAddress);unsigned one=1;Put(renderer,0x278,one);Put(renderer,0x27c,one);
    Put(renderer,0x70,targetAddress);unsigned dimensions[2]={3172,3172};Put(target,0x80,dimensions);
    for(unsigned m=0;m<ui_root_probe::MatrixCount;++m){float values[16]{};for(unsigned n=0;n<16;++n)values[n]=static_cast<float>(m*16+n);
        Put(renderer,ui_root_probe::MatrixOffsets[m],values);}
    clip[16]=1;auto token=p::RootToken{};token.id=7;token.depth=2;token.expectedRenderer=rendererAddress;
    auto originalPacket=std::array<unsigned char,0x100>{};memcpy(originalPacket.data(),packet,sizeof(packet));
    unsigned char originalRenderer[sizeof(renderer)];memcpy(originalRenderer,renderer,sizeof(renderer));
    SetLastError(0x4321);auto snapshot=p::Capture(binding,reinterpret_cast<void*>(0x99),packet,reinterpret_cast<void*>(0x123456),token);
    assert(GetLastError()==0x4321 && snapshot.valid==511);assert(snapshot.renderer==rendererAddress && snapshot.rendererVtable==rendererVt);
    assert(snapshot.expectedVtable==rendererVt && snapshot.rendererState.valid && snapshot.rendererState.clipValid);
    assert(!memcmp(snapshot.packetMatrices,matrices,sizeof(matrices)));assert(!memcmp(snapshot.packetRaw50,packet+0x50,64));
    assert(!memcmp(snapshot.rendererState.clipTop,clip,sizeof(clip)));assert(snapshot.rendererState.dimensions[0]==3172);
    assert(!memcmp(originalPacket.data(),packet,sizeof(packet)));assert(!memcmp(originalRenderer,renderer,sizeof(renderer)));
    assert(snapshot.raw50Class==2);
    float sentinel[16]{};for(unsigned i=3;i<16;i+=4)sentinel[i]=-1;memcpy(packet+0x50,sentinel,sizeof(sentinel));
    auto raw=p::CaptureRawPlanes(packet);assert(raw.valid && raw.category==1);memcpy(packet,originalPacket.data(),sizeof(packet));
    auto rawCopy=p::Capture(binding,nullptr,packet,nullptr,token,&raw);assert(rawCopy.raw50Class==1 && rawCopy.captureTick==raw.tick);
    assert(!memcmp(rawCopy.packetRaw50,sentinel,sizeof(sentinel)));
    uintptr_t wrongVtable=0x772200;Put(renderer,0,wrongVtable);
    auto wrong=p::Capture(binding,nullptr,packet,nullptr,token);assert((wrong.valid&4) && !wrong.rendererState.valid);Put(renderer,0,rendererVt);
    auto bad=p::Capture(reinterpret_cast<void*>(1),nullptr,reinterpret_cast<void*>(UINTPTR_MAX-7),nullptr,{});
    assert(!(bad.valid&(1|8|16|32|64|128)));
    auto pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(pages);
    auto partial=pages+4096-0x48;for(unsigned m=0;m<3;++m){auto pointer=reinterpret_cast<uintptr_t>(matrices[m]);Put(partial,0x30+m*8,pointer);}
    DWORD protection=0;assert(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&protection));
    auto bounded=p::Capture(binding,nullptr,partial,nullptr,token);assert(bounded.valid&8);assert(!(bounded.valid&128));
    assert((bounded.valid&(16|32|64))==(16|32|64));
    auto crossing=reinterpret_cast<uintptr_t>(pages+4096-32);Put(partial,0x30,crossing);
    bounded=p::Capture(binding,nullptr,partial,nullptr,token);assert(!(bounded.valid&16));
    assert(VirtualFree(pages,0,MEM_RELEASE));
    // Record only owns copied data: invalidate the correlation addresses before
    // a real log write. No deferred read from packet/matrix/renderer is allowed.
    snapshot.packet=1;snapshot.binding=1;snapshot.renderer=1;
    for(auto& pointer:snapshot.matrixPointers)pointer=UINTPTR_MAX;
    wchar_t path[MAX_PATH]{};assert(GetModuleFileNameW(nullptr,path,MAX_PATH));auto slash=wcsrchr(path,L'\\');assert(slash);
    assert(!wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ui-material-probe.log"));
    WIN32_FILE_ATTRIBUTE_DATA before{},after{};bool hadFile=GetFileAttributesExW(path,GetFileExInfoStandard,&before)!=FALSE;
    SetLastError(0xabcd);p::owner.store(nullptr);p::Record(0,123,snapshot,true,token);assert(GetLastError()==0xabcd);
    bool hasFile=GetFileAttributesExW(path,GetFileExInfoStandard,&after)!=FALSE;
    assert(hadFile==hasFile);if(hadFile)assert(before.nFileSizeHigh==after.nFileSizeHigh && before.nFileSizeLow==after.nFileSizeLow);
    p::owner.store(GetModuleHandleW(nullptr));SetLastError(0xabcd);p::Record(0,124,snapshot,true,token);assert(GetLastError()==0xabcd);
    assert(GetFileAttributesExW(path,GetFileExInfoStandard,&after));
    assert(!hadFile || after.nFileSizeHigh>before.nFileSizeHigh || after.nFileSizeLow>before.nFileSizeLow);
    auto now=GetTickCount64();for(auto& tick:p::states[0].sampledTicks)tick.store(now);
    for(bool inside:{false,true})for(unsigned category=0;category<3;++category){
        assert(p::ShouldSample(0,inside,category,1));assert(!p::ShouldSample(0,inside,category,5));}
    // Exhausting sentinel samples does not hide a first nontrivial-plane sample.
    assert(!p::ShouldSample(0,true,1,5));assert(p::ShouldSample(0,true,2,1));
    // Exercise LastError through actual failed RPM reads and actual logging,
    // not just the owner-null fast path used by the concurrency test.
    Call(2);p::states[0].original.store(&Throw);SetLastError(Incoming);bool caught=false;
    try {p::Observe<0>(nullptr,nullptr,nullptr);}catch(const std::runtime_error&){caught=true;assert(GetLastError()==0xdead);}
    assert(caught && !p::activeCalls.load());p::states[0].original.store(&Mock<0>);
    p::owner.store(nullptr);assert(!p::ShouldSample(0,true,2,1));
    puts("PASS: bounded packet/renderer capture, no writes/deferred dereference, owner-only logging, independent raw-plane sampling buckets");
}

struct Replacer {
    ImageFixture* image;int failInstall=-1,failRemove=-1;bool afterInstall=false,afterRemove=false;unsigned mutations=0;
    DWORD operator()(void* volatile* slot,void* expected,void* next) {
        unsigned index=4;for(unsigned i=0;i<4;++i)if(reinterpret_cast<uintptr_t>(slot)==image->base+p::Specs[i].table)index=i;assert(index<4);
        bool install=next==p::Hook(index),fail=static_cast<int>(index)==(install?failInstall:failRemove);
        bool after=install?afterInstall:afterRemove;if(fail && !after)return ERROR_ACCESS_DENIED;
        auto seen=InterlockedCompareExchangePointer(slot,next,expected);if(seen!=expected)return ERROR_INVALID_DATA;
        ++mutations;SetLastError(0xbad);return fail?ERROR_ACCESS_DENIED:ERROR_SUCCESS;
    }
};
static void TestInstallation(ImageFixture& image) {
    auto success=[&](void* volatile* a,void* b,void* c){return Replacer{&image}(a,b,c);};
    SetLastError(0x1357);assert(p::Start(0,nullptr,success)==ERROR_REVISION_MISMATCH);assert(GetLastError()==0x1357);
    assert(p::Start(UINTPTR_MAX-100,nullptr,success)==ERROR_REVISION_MISMATCH);
    for(unsigned i=0;i<4;++i){auto& byte=image.memory[p::Specs[i].function];byte^=1;
        assert(p::Start(image.base,nullptr,success)==ERROR_REVISION_MISMATCH);image.AllNative();assert(!p::Pending());byte^=1;}
    for(unsigned i=0;i<4;++i){auto at=image.memory+p::Specs[i].table;uintptr_t third=0x123456;Put(at,0,third);
        assert(p::Start(image.base,nullptr,success)==ERROR_REVISION_MISMATCH);assert(!p::Pending());image.Restore();}
    p::activeCalls.store(1);assert(p::Start(image.base,nullptr,success)==ERROR_BUSY);p::activeCalls.store(0);
    ui_root_probe::activeRoots.store(1);assert(p::Start(image.base,nullptr,success)==ERROR_BUSY);ui_root_probe::activeRoots.store(0);
    for(int index=0;index<4;++index)for(bool after:{false,true}){
        Replacer replace{&image};replace.failInstall=index;replace.afterInstall=after;
        auto call=[&](void* volatile* a,void* b,void* c){return replace(a,b,c);};SetLastError(0x1357);
        assert(p::Start(image.base,nullptr,call)==ERROR_ACCESS_DENIED);assert(GetLastError()==0x1357);assert(!p::Pending());image.AllNative();
    }
    // Failed rollback before CAS keeps the one live hook and permits Stop retry.
    Replacer rollback{&image};rollback.failInstall=2;rollback.afterInstall=true;rollback.failRemove=1;
    auto rollbackCall=[&](void* volatile* a,void* b,void* c){return rollback(a,b,c);};
    assert(p::Start(image.base,nullptr,rollbackCall)==ERROR_ACCESS_DENIED);assert(p::Pending());
    assert(image.Value(1)==p::Hook(1));assert(image.Native(0) && image.Native(2) && image.Native(3));
    assert(p::Start(image.base,nullptr,success)==ERROR_ALREADY_EXISTS);assert(p::Stop(success)==ERROR_SUCCESS);image.AllNative();assert(!p::Pending());
    // Failed rollback after CAS clears the removed hook, preserves its error.
    rollback.afterRemove=true;assert(p::Start(image.base,nullptr,rollbackCall)==ERROR_ACCESS_DENIED);assert(!p::Pending());image.AllNative();
    assert(p::Start(image.base,nullptr,success)==ERROR_SUCCESS);for(unsigned i=0;i<4;++i)assert(image.Value(i)==p::Hook(i));
    assert(p::Start(image.base,nullptr,success)==ERROR_ALREADY_EXISTS);
    Replacer stop{&image};stop.failRemove=3;auto stopCall=[&](void* volatile* a,void* b,void* c){return stop(a,b,c);};
    assert(p::Stop(stopCall)==ERROR_ACCESS_DENIED);assert(p::Pending());assert(image.Value(3)==p::Hook(3));
    for(unsigned i=0;i<3;++i)assert(image.Native(i));assert(p::Stop(success)==ERROR_SUCCESS);assert(!p::Pending());
    assert(p::Start(image.base,nullptr,success)==ERROR_SUCCESS);stop.afterRemove=true;
    assert(p::Stop(stopCall)==ERROR_ACCESS_DENIED);assert(!p::Pending());image.AllNative();
    assert(p::Start(image.base,nullptr,success)==ERROR_SUCCESS);uintptr_t third=0x999999;Put(image.memory,p::Specs[2].table,third);
    assert(p::Stop(success)==ERROR_INVALID_DATA);assert(!p::Pending());assert(image.Value(2)==reinterpret_cast<void*>(third));
    image.Restore();assert(p::Stop(success)==ERROR_SUCCESS);
    puts("PASS: four-slot validation, active-scope refusal, failure before/after CAS, rollback retry, partial Stop, third-party preservation");
}
int main(){TestForwarding();ImageFixture image;TestCapture(image);TestInstallation(image);puts("ui_material_probe: all tests passed");}
