#include <windows.h>
static bool front=true;
static ULONGLONG testTick=100;
static HWND Window(){return nullptr;}
static DWORD Process(HWND,DWORD* pid){*pid=front?GetCurrentProcessId():0;return 0;}
inline SHORT Key(int){return static_cast<SHORT>(0x8000);}
static ULONGLONG Tick(){return testTick;}
#define GetForegroundWindow Window
#define GetWindowThreadProcessId Process
#define GetAsyncKeyState Key
#define GetTickCount64 Tick
#define FH4VR_FOV_PROBE
#define FH4VR_FOV_TEST
#define FH4VR_WIDE_FOV_TEST
#define FH4VR_NEAR_CLIP_TEST
#define FH4VR_VR_PROJECTION_TEST
#define FH4VR_LIVE_SCENE
#define FH4VR_STEREO_HEAD
#define FH4VR_STEREO_TRIAL
#define FH4VR_EYE_OFFSET
#define FH4VR_EYE_POSITION
#include "vtable_probe.cpp"
static float fov=.83775806427f,nearValue=.1f;
static float __fastcall Fov(void*){SetLastError(4321);return fov;}
static float __fastcall Near(void*){SetLastError(1234);return nearValue;}
int main(){
    ProbeTelemetry state{};telemetry=&state;originalFov=Fov;originalNear=Near;
    runtime_projection::verticalRadians.store(1.7f);
    auto check=[&](bool active){
        float value=ObserveFov(nullptr);
        if(GetLastError()!=4321)return false;
        float expected=active?runtime_projection::verticalRadians.load()-.0872664626f:fov;
        if(std::abs(value-expected)>.000001f)return false;
        value=ObserveNear(nullptr);
        return GetLastError()==1234 && value==(active?.01f:nearValue);
    };
    // Held manual diagnostic keys must not activate overrides outside VR.
    if(!check(false))return 1;
    stereo_trial::deadline.store(testTick+60000);
    for(unsigned n=1;n<=40000;++n){
        stereo_trial::command.store(n);
        if(!check(true))return 2;
    }
    front=false;if(!check(false))return 3;
    front=true;testTick=60100;if(!check(false))return 4;
    testTick=100;runtime_projection::verticalRadians.store(1.9f);if(!check(true))return 7;
    testTick=100;stereo_trial::command.store(0);if(!check(false))return 5;
    stereo_trial::command.store(1);
    fov=1.2f;nearValue=.2f;
    if(ObserveFov(nullptr)!=fov || ObserveNear(nullptr)!=nearValue)return 6;
    puts("PASS VR-only FOV/near override, 40000 eye phases, focus/deadline/stop restoration, baseline rejection and LastError");
}

