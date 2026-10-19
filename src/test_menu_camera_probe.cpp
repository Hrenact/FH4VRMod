#include "menu_camera_probe.h"
#include <thread>
#include <cassert>
struct Fixture{uintptr_t table;double position[3];};
static void Original(void* object,const void* position){memcpy(static_cast<Fixture*>(object)->position,position,24);SetLastError(4567);}
int main(){
    menu_camera_probe::original=Original;
    auto run=[](){for(unsigned n=0;n<10000;++n){
        Fixture f{menu_camera_probe::tables[n%4],{}};double xyz[]={double(n),double(n+1),double(n+2)};
        menu_camera_probe::Observe(&f,xyz);
        assert(GetLastError()==4567 && !memcmp(f.position,xyz,24));
    }};
    std::thread a(run),b(run),c(run),d(run);a.join();b.join();c.join();d.join();
    for(auto& count:menu_camera_probe::calls)assert(count==10000);
    puts("PASS menu position observer:40000 concurrent calls, unchanged output and last-error");
}
