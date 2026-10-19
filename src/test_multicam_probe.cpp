#include "multicam_probe.h"
#include <thread>
#include <cassert>
#include <limits>
#include "head_pose.h"
struct Fixture {float matrix[16];double position[4];};
template<unsigned I> static void* Original(void* object,void* output){
    assert(GetLastError()==1234);auto f=static_cast<Fixture*>(object);
    memcpy(output,I==0?static_cast<void*>(f->matrix):static_cast<void*>(f->position),I==0?64:32);SetLastError(5678);return output;
}
int main(){
    assert(multicam_probe::ProjectionValue(0,.9f,false,1.8f)==.9f);
    assert(multicam_probe::ProjectionValue(0,.9f,true,1.8f)==1.8f);
    assert(multicam_probe::ProjectionValue(0,.9f,true,0)==.9f);
    assert(multicam_probe::ProjectionValue(1,.1f,true,1.8f)==.01f);
    assert(multicam_probe::ProjectionValue(1,.5f,true,1.8f)==.5f);
    // Matching view/position shifts must still map the shifted eye to the origin.
    float view[16]={0,0,-1,0,0,1,0,0,1,0,0,0,-30,-20,10,1};
    double position[4]={10,20,30,1};
    float shifted[16];memcpy(shifted,view,sizeof(view));
    assert(multicam_probe::Shift(0,shifted,nullptr,.032f));
    assert(multicam_probe::Shift(1,position,view,.032f));
    for(unsigned col=0;col<3;++col){double v=shifted[12+col];for(unsigned row=0;row<3;++row)v+=position[row]*shifted[row*4+col];assert(std::abs(v)<2e-6);}
    for(uint64_t tail:{uint64_t(0),uint64_t(0x3ff07b5800000000),uint64_t(0x7ff8000000001234),uint64_t(0x7ff0000000000000)}){
        double opaque[4]={10,20,30,0};memcpy(&opaque[3],&tail,8);
        assert(multicam_probe::Shift(1,opaque,view,.032f));
        assert(opaque[0]==10 && opaque[1]==20 && std::abs(opaque[2]-30.032)<1e-7);
        assert(!memcmp(&opaque[3],&tail,8));
    }
    double invalid[4]={std::numeric_limits<double>::quiet_NaN(),20,30,0},copy[4];memcpy(copy,invalid,sizeof(copy));
    assert(!multicam_probe::Shift(1,invalid,view,.032f) && !memcmp(copy,invalid,sizeof(copy)));
    const float rotation[9]={0,0,-1,0,1,0,1,0,0};
    for(float eye:{-.032f,.032f}){
        float headView[16];memcpy(headView,view,sizeof(view));double headPosition[4]={10,20,30,1};
        assert(ApplyHeadRotation(headView,rotation));
        float eyeView[16];memcpy(eyeView,headView,sizeof(headView));
        assert(multicam_probe::Shift(0,eyeView,nullptr,eye));
        assert(multicam_probe::Shift(1,headPosition,headView,eye));
        for(unsigned col=0;col<3;++col){double v=eyeView[12+col];for(unsigned row=0;row<3;++row)v+=headPosition[row]*eyeView[row*4+col];assert(std::abs(v)<2e-6);}
    }
    multicam_probe::original[0]=Original<0>;multicam_probe::original[1]=Original<1>;
    auto run=[](){for(unsigned n=0;n<10000;++n){
        Fixture f{};for(unsigned i=0;i<16;++i)f.matrix[i]=float(n+i);for(unsigned i=0;i<4;++i)f.position[i]=double(n+i)+.123456789;
        struct Output{unsigned char data[64];uint64_t guard;};Output m{},p{};m.guard=p.guard=0x12345678;
        memset(p.data+32,0xCD,32);
        SetLastError(1234);assert(multicam_probe::Observe<0>(&f,m.data)==m.data);assert(GetLastError()==5678 && !memcmp(m.data,f.matrix,64));
        SetLastError(1234);assert(multicam_probe::Observe<1>(&f,p.data)==p.data);assert(GetLastError()==5678 && !memcmp(p.data,f.position,32));
        for(unsigned i=32;i<64;++i)assert(p.data[i]==0xCD);
        assert(m.guard==0x12345678 && p.guard==0x12345678);
    }};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    assert(multicam_probe::calls[0]==40000 && multicam_probe::calls[1]==40000);
    puts("PASS multicam:40000 calls per getter, exact size/value/return/error preservation");
}
