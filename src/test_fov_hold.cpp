#include <windows.h>
static bool testForeground=true,testPressed=false;
static ULONGLONG testTick=100;
static HWND TestForegroundWindow() {return nullptr;}
static DWORD TestForegroundProcess(HWND,DWORD* pid) {*pid=testForeground?GetCurrentProcessId():0;return 0;}
static SHORT TestKey(int) {return testPressed?static_cast<SHORT>(0x8000):0;}
static ULONGLONG TestTick() {return testTick;}
#define GetForegroundWindow TestForegroundWindow
#define GetWindowThreadProcessId TestForegroundProcess
#define GetAsyncKeyState TestKey
#define GetTickCount64 TestTick
#define FH4VR_FOV_PROBE
#define FH4VR_FOV_TEST
#define FH4VR_STEREO_TRIAL
#define FH4VR_EYE_OFFSET
#define FH4VR_EYE_POSITION
#include "vtable_probe.cpp"
#include <limits>
static float __fastcall Fixture(void* value) {SetLastError(2468);return *static_cast<float*>(value);}
int main() {
    ProbeTelemetry counters{};telemetry=&counters;originalFov=Fixture;
    float raw=.83775806427f;
    if(ObserveFov(&raw)!=raw || GetLastError()!=2468) return 1;
    testPressed=true;
    float wide=ObserveFov(&raw);
#ifdef FH4VR_WIDE_FOV_TEST
    const float expected=2.f*std::atan(1.279527f)-.0872664626f;
#else
    const float expected=raw+.0872664626f;
#endif
    if(std::abs(wide-expected)>1e-7 || GetLastError()!=2468) return 2;
    if(ObserveFov(&raw)!=wide || telemetry->position[1]!=raw || telemetry->fov!=wide) return 3;
    testTick+=5000;
    if(ObserveFov(&raw)!=raw) return 4;
    testPressed=false;ObserveFov(&raw);testPressed=true;
    if(ObserveFov(&raw)!=wide) return 5;
    testForeground=false;if(ObserveFov(&raw)!=raw) return 6;
    testForeground=true;if(ObserveFov(&raw)!=raw) return 7;
    testPressed=false;ObserveFov(&raw);testPressed=true;
    stereo_trial::command.store(1);if(ObserveFov(&raw)!=raw) return 8;
    stereo_trial::command.store(0);if(ObserveFov(&raw)!=raw) return 9;
    testPressed=false;ObserveFov(&raw);testPressed=true;
    float invalidValues[]={.1f,2.5f,std::numeric_limits<float>::infinity()};
    for(float invalid:invalidValues) {
        if(ObserveFov(&invalid)!=invalid) return 10;
    }
#ifdef FH4VR_WIDE_FOV_TEST
    float otherCamera=1.f;
    if(ObserveFov(&otherCamera)!=otherCamera)return 11;
    if(std::abs((wide+.0872664626f)*180.f/3.14159265359f-103.9819f)>.02f)return 12;
#endif
    puts("PASS FOV temporary delta, no accumulation, release/timeout/focus/stereo gates and range rejection");
}
