#include <windows.h>
#include <cassert>
#include <cstring>
#include <thread>
#include "ui_projection_probe.h"
struct Fixture {float values[16];const float* pointer;};
static void Original(void* object,const float* matrix) {
    auto f=static_cast<Fixture*>(object);memcpy(f->values,matrix,64);f->pointer=matrix;SetLastError(1234);
}
static unsigned clipCalls=0;
static void Clip(void*,const float* rectangle){assert(rectangle[0]==10 && rectangle[2]==300);++clipCalls;}
static uintptr_t PrepareFixture(void* object) {
    auto bytes=static_cast<unsigned char*>(object);
    memcpy(bytes+0x5b0,bytes+0x470,64);SetLastError(4321);return 0x12345678;
}
static uintptr_t PassFixture(void* object) {
    ++*static_cast<unsigned*>(object);SetLastError(9876);return reinterpret_cast<uintptr_t>(object);
}
static uintptr_t DrawFixture(void* object,void* batch,uintptr_t arg3,uintptr_t arg4) {
    assert(object==batch && arg3==0x12345678 && arg4==0x123456789abcdef0ULL);++*static_cast<unsigned*>(object);SetLastError(1357);
    return reinterpret_cast<uintptr_t>(batch);
}
static void TestPassProbe() {
    ui_pass_probe::original=PassFixture;
    ui_pass_probe::originalEnd=PassFixture;
    ui_pass_probe::originalLayerEnd=PassFixture;
    ui_pass_probe::originalDraw=DrawFixture;
    auto run=[](){unsigned count=0;for(unsigned i=0;i<10000;++i){
        assert(ui_pass_probe::Observe(&count)==reinterpret_cast<uintptr_t>(&count));
        assert(count==4*i+1 && GetLastError()==9876);
        assert(ui_pass_probe::ObserveEnd(&count)==reinterpret_cast<uintptr_t>(&count));
        assert(count==4*i+2 && GetLastError()==9876);
        assert(ui_pass_probe::ObserveLayerEnd(&count)==reinterpret_cast<uintptr_t>(&count));
        assert(count==4*i+3 && GetLastError()==9876);
        assert(ui_pass_probe::ObserveDraw(&count,&count,0x12345678,0x123456789abcdef0ULL)==reinterpret_cast<uintptr_t>(&count));
        assert(count==4*i+4 && GetLastError()==1357);
    }};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    assert(ui_pass_probe::calls==40000);ui_pass_probe::original=nullptr;
    assert(ui_pass_probe::ends==40000 && ui_pass_probe::pairedEnds==40000);
    assert(ui_pass_probe::overlappingBegins==0 && ui_pass_probe::unpairedEnds==0);
    ui_pass_probe::originalEnd=nullptr;
    assert(ui_pass_probe::layerEnds==40000);ui_pass_probe::originalLayerEnd=nullptr;
    assert(ui_pass_probe::drawCalls==40000);ui_pass_probe::originalDraw=nullptr;
    puts("PASS UI pass probe: 40000 concurrent calls, object identity, return and LastError preserved");
}
static void TestDestinations() {
    // Exercise real snapshot/logging path with concurrent valid objects and a null target.
    unsigned char objects[4][0x700]{},targets[3][0x90]{},before[4][0x700]{};
    for(unsigned i=0;i<3;++i){uintptr_t p=reinterpret_cast<uintptr_t>(targets[i]);memcpy(objects[i]+0x70,&p,sizeof(p));}
    memcpy(before,objects,sizeof(objects));
    ui_pass_probe::owner=GetModuleHandleW(nullptr);
    ui_pass_probe::destinationCount=0;ui_pass_probe::destinationWindow=GetTickCount64();
    auto run=[&](unsigned n){for(unsigned i=0;i<10000;++i){
        SetLastError(2468);ui_pass_probe::Destination(objects[n],1,reinterpret_cast<void*>(0x1234));
        assert(GetLastError()==2468);
    }};
    std::thread a(run,0),b(run,1),c(run,2),d(run,3);a.join();b.join();c.join();d.join();
    assert(ui_pass_probe::destinationCount==4 && !memcmp(before,objects,sizeof(objects)));
    ui_pass_probe::owner=nullptr;
    puts("PASS UI destination snapshots: 40000 concurrent reads, distinct objects, null target, bounded logging, unchanged objects and LastError");
}
static void TestProjectiveClip() {
    using ui_projection_probe::ClipTrialActive;
    assert(!ClipTrialActive(true,false,100,false));
    assert(ClipTrialActive(true,true,200,true));
    assert(!ClipTrialActive(true,true,201,false));
    assert(ClipTrialActive(true,true,202,true));
    assert(!ClipTrialActive(true,true,5200,true));
    assert(!ClipTrialActive(true,true,5300,false));
    assert(!ClipTrialActive(true,true,5301,true));
    assert(!ClipTrialActive(true,false,5400,false));
    assert(ClipTrialActive(true,true,5500,true));
    assert(!ClipTrialActive(false,true,5600,true));
    assert(!ClipTrialActive(true,true,5700,true));
    assert(!ClipTrialActive(true,false,5800,true));
    const float rect[4]={600,500,1000,1200};
    float p[16]={};p[0]=2.f/2400;p[5]=-2.f/2400;p[10]=.001f;p[12]=-1;p[13]=1;p[15]=1;
    const float identity[9]={1,0,0,0,1,0,0,0,1};
    for(float turn:{-.6f,-.2f,0.f,.2f,.6f})for(float eye:{-.032f,.032f}){
        float co=std::cos(turn),si=std::sin(turn),rotation[9]={co,0,si,0,1,0,-si,0,co};
        ui_smoothing::Filter filter;filter.Update(identity,0);auto offset=filter.Update(rotation,16);
        float m[16],planes[16];assert(ui_projection_test::Plane(p,m,2400,2400,1.8f,eye,offset));
        assert(ui_clip_projection::RectPlanes(m,rect,planes));
        // Independent point test: project corners, inside/outside points, and multiple UI depths.
        for(float x:{550.f,600.f,900.f,1600.f,1650.f})for(float y:{450.f,500.f,900.f,1700.f,1750.f})for(float z:{0.f,20.f,200.f}){
            float position[4]={x,y,z,1},projected[4]={};
            for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)projected[c]+=position[r]*m[4*r+c];
            assert(projected[3]>0);
            const float expected[4]={y-500,x-600,1700-y,1600-x};
            for(unsigned n=0;n<4;++n){
                double actual=-planes[4*n+3]*projected[3];
                for(unsigned c=0;c<3;++c)actual+=planes[4*n+c]*projected[c];
                assert(std::abs(actual-expected[n])<.001);
            }
        }
        alignas(16) unsigned char renderer[0x600]{};
        struct Rect {float v[4];unsigned enabled;} box{{600,500,1000,1200},1};
        struct Stack {uintptr_t data;unsigned capacity,count;} stack{reinterpret_cast<uintptr_t>(&box),1,1};
        memcpy(renderer+0x270,&stack,sizeof(stack));memcpy(renderer+0x3e0,m,64);
        for(unsigned i=0;i<64;++i)renderer[0x470+i]=static_cast<unsigned char>(i+1);
        unsigned char before[sizeof(renderer)];memcpy(before,renderer,sizeof(renderer));
        ui_projection_probe::originalPrepare=PrepareFixture;ui_projection_probe::trialObject=nullptr;
        assert(ui_projection_probe::ObservePrepare(renderer)==0x12345678 && GetLastError()==4321);
        assert(!memcmp(renderer+0x5b0,renderer+0x470,64));
        ui_projection_probe::trialObject=renderer;
        assert(ui_projection_probe::ObservePrepare(renderer)==0x12345678 && GetLastError()==4321);
        assert(!memcmp(renderer+0x5b0,planes,64));
        assert(!memcmp(renderer,before,0x5b0) && !memcmp(renderer+0x5f0,before+0x5f0,16));
        box.enabled=0;ui_projection_probe::ObservePrepare(renderer);assert(!memcmp(renderer+0x5b0,renderer+0x470,64));
        box.enabled=2;assert(!ui_projection_probe::CorrectPreparedPlanes(renderer));
        stack.count=2;memcpy(renderer+0x270,&stack,sizeof(stack));assert(!ui_projection_probe::CorrectPreparedPlanes(renderer));
    }
    float singular[16]={},output[16];for(float& v:output)v=123;
    assert(!ui_clip_projection::RectPlanes(singular,rect,output));for(float v:output)assert(v==123);
    ui_projection_probe::trialObject=nullptr;ui_projection_probe::originalPrepare=nullptr;
    puts("PASS projective UI clip: both eyes, yaw, boundaries/depths, disabled clip, draw isolation and return preservation");
}
int main() {
    TestPassProbe();
    TestDestinations();
    TestProjectiveClip();
    alignas(16) unsigned char renderer[0x290]{};
    struct ClipRect {float values[4];unsigned enabled;} rect{{10,20,300,400},1};
    struct Stack {uintptr_t data;unsigned capacity,count;} stack{reinterpret_cast<uintptr_t>(&rect),1,1};
    memcpy(renderer+0x270,&stack,sizeof(stack));ui_projection_probe::refreshClip=Clip;
    assert(ui_projection_probe::RefreshCurrentClip(renderer)&&clipCalls==1);
    stack.count=0;memcpy(renderer+0x270,&stack,sizeof(stack));assert(!ui_projection_probe::RefreshCurrentClip(renderer));
    stack.count=2;memcpy(renderer+0x270,&stack,sizeof(stack));assert(!ui_projection_probe::RefreshCurrentClip(renderer));
    ui_projection_probe::refreshClip=nullptr;
    const float identity[9]={1,0,0,0,1,0,0,0,1};
    const float turn=.1f,cosine=std::cos(turn),sine=std::sin(turn);
    const float yaw[9]={cosine,0,sine,0,1,0,-sine,0,cosine};
    ui_smoothing::Filter filter;
    auto zero=filter.Update(identity,100);assert(zero.x==0 && zero.y==0);
    auto lag=filter.Update(yaw,116);assert(std::abs(lag.x-std::tan(turn))<1e-6f);
    auto previous=lag.x;
    for(uint64_t t=132;t<=900;t+=16){auto o=filter.Update(yaw,t);assert(o.x<=previous+1e-6f);previous=o.x;}
    assert(std::abs(previous-std::tan(turn))<1e-6f); // held in dead zone, not chased
    auto reset=filter.Update(identity,916,true);assert(std::abs(reset.x)<1e-6f);
    auto gap=filter.Update(yaw,2000);assert(std::abs(gap.x-std::tan(turn))<1e-6f);
    const float pitch[9]={1,0,0,0,cosine,-sine,0,sine,cosine};
    const float roll[9]={cosine,-sine,0,sine,cosine,0,0,0,1};
    for(const float* tilted:{pitch,roll}){
        ui_smoothing::Filter recenter;recenter.Update(tilted,100,true);
        auto upright=recenter.Update(identity,116);
        assert(std::abs(upright.x)<1e-6f && std::abs(upright.y)<1e-6f);
        for(unsigned i=0;i<9;++i)assert(std::abs(upright.basis[i]-identity[i])<1e-6f);
    }
    filter={};filter.Update(identity,100);auto up=filter.Update(pitch,116);assert(up.y<0);
    const float big[9]={0,0,1,0,1,0,-1,0,0};
    filter={};filter.Update(identity,100);auto sideways=filter.Update(big,116);
    assert(filter.ready && std::abs(filter.forward[2]+1)<1e-6f);
    for(uint64_t t:{uint64_t(1000),uint64_t(5000),uint64_t(10000)})filter.Update(big,t);
    auto returned=filter.Update(identity,10016);
    for(unsigned i=0;i<9;++i)assert(std::abs(returned.basis[i]-identity[i])<1e-6f);
    auto moved=filter.Update(big,10032,true);assert(std::abs(moved.x)<1e-6f);
    assert(std::abs(filter.forward[0]+1)<1e-6f);
    float p[16]={};p[0]=2.f/2400;p[5]=-2.f/2400;p[10]=p[0];p[12]=-1;p[13]=1;p[15]=1;
    float result[16]{};
    assert(ui_projection_test::Shrink(p,result,2400,2400));
    assert(result[12]==-.75f && result[13]==.75f && result[10]==p[10] && result[15]==1);
    assert(std::abs((2400*result[0]+result[12])-.75f)<1e-6f);
    assert(!ui_projection_test::Shrink(p,result,1920,1080));
    const float vertical=1.8f, tangent=std::tan(vertical*.5f);
    float left[16],right[16];
    float hidden[16];
    assert(ui_projection_test::Plane(p,hidden,2400,2400,vertical,0,sideways));
    assert(hidden[12]==2 && hidden[15]==1 && hidden[0]==0);
    assert(ui_projection_test::Plane(p,left,2400,2400,vertical,-.032f));
    assert(ui_projection_test::Plane(p,right,2400,2400,vertical,.032f));
    const float lc=1200*left[0]+left[12],rc=1200*right[0]+right[12];
    assert(lc>rc && std::abs((lc-rc)*tangent-.032f)<1e-6f);
    float shifted[16];
    assert(ui_projection_test::Plane(p,shifted,2400,2400,vertical,0,lag));
    // Reconstruct camera rays, rotate back to world: panel points stay anchored.
    for(float px:{0.f,1200.f,2400.f}) {
        const float clipx=px*shifted[0]+1200*shifted[4]+shifted[12];
        const float clipw=px*shifted[3]+1200*shifted[7]+shifted[15];
        const float worldX=cosine*(clipx*tangent)-sine*clipw;
        const float worldZ=-sine*(clipx*tangent)-cosine*clipw;
        assert(std::abs(worldX-(px/1200-1)*std::tan(50.f*3.14159265359f/360.f))<1e-6f);
        assert(std::abs(worldZ+1)<1e-6f);
    }
    assert(left[10]==p[10] && left[15]==1 && p[12]==-1);
    float compressed[16];
    assert(ui_projection_test::Plane(p,compressed,2400,2400,vertical,0,lag,true));
    for(unsigned i=0;i<16;++i)if(i%4!=2)assert(compressed[i]==shifted[i]);
    for(float px:{0.f,2400.f})for(float py:{0.f,2400.f})for(float z:{0.f,1200.f}){
        const float w=px*compressed[3]+py*compressed[7]+compressed[15];
        const float depth=px*compressed[2]+py*compressed[6]+z*compressed[10]+compressed[14];
        assert(depth>0 && depth<w);
    }
    assert(std::abs(left[5]*2400*tangent+2*std::tan(50.f*3.14159265359f/360.f))<1e-6f);
    assert(!ui_projection_test::Plane(p,result,2400,2400,0,.032f));
    assert(!ui_projection_test::Plane(p,result,2400,2400,vertical,.2f));
    assert(!ui_projection_test::Plane(p,result,0,2400,vertical,.032f));
    p[12]=-1.7777778f;assert(!ui_projection_test::Shrink(p,result,2400,2400));
    HoldYawGate hold;assert(hold.angle(true,false,100)==0);
    assert(hold.angle(true,true,200)!=0);assert(hold.angle(true,true,5200)==0);
    assert(hold.angle(true,true,6000)==0);assert(hold.angle(true,false,6100)==0);
    assert(hold.angle(true,true,6200)!=0);assert(hold.angle(false,true,6300)==0);
    ui_projection_probe::original=Original;
    auto run=[](){for(unsigned n=0;n<10000;++n){
        float m[16];for(unsigned i=0;i<16;++i)m[i]=float(n+i);
        Fixture f{};ui_projection_probe::Observe(&f,m);
        assert(GetLastError()==1234 && f.pointer==m && !memcmp(f.values,m,64));
    }};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    assert(ui_projection_probe::calls==40000);
    puts("PASS UI projection observer:40000 concurrent calls, unchanged pointer/data and last-error");
}
