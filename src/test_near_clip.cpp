#include <windows.h>
static bool testFront=true,testF1=false,testF8=false;
static ULONGLONG testNow=100;
static HWND Window(){return nullptr;}
static DWORD Process(HWND,DWORD* pid){*pid=testFront?GetCurrentProcessId():0;return 0;}
static SHORT Key(int key){return ((key==VK_F1 && testF1)||(key==VK_F8 && testF8))?static_cast<SHORT>(0x8000):0;}
static ULONGLONG Tick(){return testNow;}
#define GetForegroundWindow Window
#define GetWindowThreadProcessId Process
#define GetAsyncKeyState Key
#define GetTickCount64 Tick
#define FH4VR_FOV_PROBE
#define FH4VR_FOV_TEST
#define FH4VR_WIDE_FOV_TEST
#define FH4VR_NEAR_CLIP_TEST
#define FH4VR_STEREO_HEAD
#define FH4VR_STEREO_TRIAL
#define FH4VR_EYE_OFFSET
#define FH4VR_EYE_POSITION
#include "vtable_probe.cpp"
#include <limits>
static float __fastcall Near(void* value){SetLastError(5678);return *static_cast<float*>(value);}
static float __fastcall Fov(void*){return .83775806427f;}
int main(){
    ProbeTelemetry state{};telemetry=&state;originalNear=Near;originalFov=Fov;
    float raw=.1f;
    auto sample=[&](){float result=ObserveNear(&raw);if(GetLastError()!=5678)ExitProcess(20);return result;};
    if(sample()!=raw)return 1;
    testF8=true;if(sample()!=raw)return 2;testF8=false;
    testF1=true;ObserveFov(nullptr);if(sample()!=raw)return 3;
    testF8=true;if(sample()!=.01f || raw!=.1f || state.position[3]!=.01f)return 4;
    if(sample()!=.01f)return 5;
    testF8=false;if(sample()!=raw)return 6;
    testF8=true;if(sample()!=.01f)return 7;
    testNow+=5000;if(sample()!=raw)return 8;
    testF1=testF8=false;sample();testF1=true;sample();testF8=true;
    if(sample()!=.01f)return 9;
    testFront=false;if(sample()!=raw)return 10;
    testFront=true;if(sample()!=raw)return 11;
    testF1=testF8=false;sample();testF1=true;sample();testF8=true;
    stereo_trial::command.store(1);if(sample()!=raw)return 12;
    stereo_trial::command.store(0);if(sample()!=raw)return 13;
    testF1=testF8=false;sample();testF1=true;sample();testF8=true;
    raw=.2f;if(sample()!=raw)return 14;
    raw=std::numeric_limits<float>::infinity();if(sample()!=raw)return 15;
    raw=.1f;testF1=false;if(sample()!=raw)return 16;
    puts("PASS near clip bounded return-only override, wide-only/release/focus/timeout/stereo gates, baseline rejection and LastError");
}
