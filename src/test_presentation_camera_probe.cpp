#include "presentation_camera_probe.h"
#include <thread>
#include <cassert>
struct Fixture {float a[16],b[16];unsigned flags;};
static void Original(void* object,const void* a,const void* b,unsigned flags){
    assert(GetLastError()==1234);auto f=static_cast<Fixture*>(object);
    memcpy(f->a,a,64);memcpy(f->b,b,64);f->flags=flags;SetLastError(5678);
}
int main(){
    presentation_camera_probe::original=Original;
    auto run=[](){for(unsigned n=0;n<10000;++n){
        Fixture f{};float a[16]{},b[16]{};for(unsigned i=0;i<16;++i){a[i]=float(n+i);b[i]=-float(n+i);}
        SetLastError(1234);presentation_camera_probe::Observe(&f,a,b,n);
        assert(GetLastError()==5678 && f.flags==n && !memcmp(a,f.a,64) && !memcmp(b,f.b,64));
    }};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    assert(presentation_camera_probe::calls==40000);
    puts("PASS presentation matrix passthrough:40000 calls; matrices,flags,last-error preserved");
}
