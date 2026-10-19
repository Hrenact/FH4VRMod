#include <windows.h>
#include <atomic>
#include <cstdio>
#include <thread>
#include "MinHook.h"
struct Input { unsigned long long a,b; };
using Function=void(__fastcall*)(void*,void*);
static Function original;
static std::atomic<unsigned> hits{0},failures{0};
#ifdef TEST_CAMERA_PROLOGUE
extern "C" void __fastcall TestCameraTarget(void*,void*);
#define Target TestCameraTarget
#else
__declspec(noinline) static void __fastcall Target(void* input,void* output) {
    auto value=static_cast<Input*>(input);
    *static_cast<unsigned long long*>(output)=value->a ^ value->b;
}
#endif
static void __fastcall Hook(void* input,void* output) {
    original(input,output);
    hits.fetch_add(1,std::memory_order_relaxed);
}
int main() {
    if (MH_Initialize()!=MH_OK) return 1;
    if (MH_CreateHook(reinterpret_cast<void*>(&Target),reinterpret_cast<void*>(&Hook),reinterpret_cast<void**>(&original))!=MH_OK) return 2;
    if (MH_EnableHook(reinterpret_cast<void*>(&Target))!=MH_OK) return 3;
    auto exercise=[] {
        Function volatile function=&Target;
        for (unsigned i=0;i<1000;++i) {
            Input input{0x123456789abcdef0ULL+i,0xfedcba9876543210ULL-i};
            unsigned long long output=0;
            function(&input,&output);
            if (output!=(input.a^input.b)) failures.fetch_add(1);
        }
    };
    std::thread workers[4];
    for(auto& worker:workers) worker=std::thread(exercise);
    for(auto& worker:workers) worker.join();
    if (hits!=4000 || failures!=0) return 4;
    if (MH_DisableHook(reinterpret_cast<void*>(&Target))!=MH_OK) return 5;
    exercise();
    if (hits!=4000 || failures!=0) return 6;
    MH_RemoveHook(reinterpret_cast<void*>(&Target));
    MH_Uninitialize();
    puts("PASS: 4000 concurrent hooked calls preserve both arguments/output; disable restores normal execution.");
    return 0;
}
