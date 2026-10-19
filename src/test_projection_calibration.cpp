#include <windows.h>
static bool front=true,calibrationPressed=false;
static ULONGLONG calibrationTick=100;
static HWND Window(){return nullptr;}
static DWORD Process(HWND,DWORD* pid){*pid=front?GetCurrentProcessId():0;return 0;}
static SHORT Key(int key){return key==VK_F8 && calibrationPressed?static_cast<SHORT>(0x8000):0;}
static ULONGLONG Tick(){return calibrationTick;}
#define GetForegroundWindow Window
#define GetWindowThreadProcessId Process
#define GetAsyncKeyState Key
#define GetTickCount64 Tick
#define FH4VR_PROJECTION_CALIBRATION
#define FH4VR_STEREO_HEAD
#define FH4VR_STEREO_TRIAL
#define FH4VR_EYE_OFFSET
#define FH4VR_EYE_POSITION
#include "vtable_probe.cpp"
static bool invalid=false;
static void* __fastcall Orientation(void*,void* output){
    float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    if(invalid)m[0]=2;
    memcpy(output,m,sizeof(m));SetLastError(1234);return output;
}
int main(){
    ProbeTelemetry state{};telemetry=&state;orientationFunction=Orientation;
    struct {unsigned before=42;float m[16];unsigned after=43;} view{};
    auto run=[&](){return ObserveOrientation(nullptr,view.m)==view.m && GetLastError()==1234 && view.before==42 && view.after==43;};
    if(!run() || view.m[2]!=0)return 1;
    calibrationPressed=true;
    if(!run() || std::abs(view.m[2]-std::sin(.0872664626f))>1e-6 || state.position[3]==0)return 2;
    if(!run() || std::abs(view.m[2]-std::sin(.0872664626f))>1e-6)return 3;
    calibrationTick+=5000;if(!run() || view.m[2]!=0 || state.position[3]!=0)return 4;
    calibrationPressed=false;run();calibrationPressed=true;front=false;
    if(!run() || view.m[2]!=0)return 5;
    front=true;if(!run() || view.m[2]!=0)return 6;
    calibrationPressed=false;run();calibrationPressed=true;stereo_trial::command.store(1);
    if(!run() || view.m[2]!=0)return 7;
    stereo_trial::command.store(0);if(!run() || view.m[2]!=0)return 8;
    calibrationPressed=false;run();calibrationPressed=true;invalid=true;
    if(!run() || view.m[0]!=2 || view.m[2]!=0 || state.position[3]!=0)return 9;
    puts("PASS projection calibration angle, output guards, LastError, no accumulation, timeout/focus/stereo gates and invalid-matrix rejection");
}

