#include <windows.h>
static bool fixtureForeground=true;
static HWND FixtureWindow() {return nullptr;}
static DWORD FixtureWindowProcess(HWND,DWORD* pid) {*pid=fixtureForeground?GetCurrentProcessId():0;return 0;}
#define GetForegroundWindow FixtureWindow
#define GetWindowThreadProcessId FixtureWindowProcess
#define FH4VR_STEREO_HEAD
#define FH4VR_STEREO_TRIAL
#define FH4VR_EYE_OFFSET
#define FH4VR_EYE_POSITION
#include "vtable_probe.cpp"
#include <thread>
#include <vector>
static void* __fastcall Orientation(void*,void* output) {
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    memcpy(output,identity,sizeof(identity));SetLastError(1234);return output;
}
static void* __fastcall Position(void*,void* output) {
    double p[4]={0,0,0,17};memcpy(output,p,sizeof(p));SetLastError(5678);return output;
}
int main() {
    ProbeTelemetry state{};telemetry=&state;original=Position;orientationFunction=Orientation;
    HeadPose pose{};pose.magic=PoseMagic;pose.version=1;pose.enabled=pose.valid=1;pose.tick=GetTickCount64();
    float c=std::cos(.2f),s=std::sin(.2f),r[9]={c,0,s,0,1,0,-s,0,c};memcpy(pose.rotation,r,sizeof(r));
    stereo_head::input.store(&pose);stereo_head::Latch();
    stereo_trial::deadline.store(GetTickCount64()+60000);stereo_trial::command.store(1);
    struct {unsigned before=123;float m[16]{};unsigned after=456;} view;
    struct {unsigned long long before=789;double p[4]{};unsigned long long after=987;} pos;
    if(ObserveOrientation(nullptr,view.m)!=view.m || GetLastError()!=1234 || std::abs(view.m[2]-s)>1e-6) return 1;
    if(ObserveMatrix(nullptr,pos.p)!=pos.p || GetLastError()!=5678 || std::abs(pos.p[0]+.032*c)>1e-6 || std::abs(pos.p[2]-.032*s)>1e-6 || pos.p[3]!=17) return 2;
    if(view.before!=123 || view.after!=456 || pos.before!=789 || pos.after!=987) return 3;
    // Producer changes are invisible until next pair latch.
    pose.enabled=0;
    HeadPose read{};
    if(!stereo_head::Read(read,true,GetTickCount64())) return 4;
    stereo_head::Latch();ObserveOrientation(nullptr,view.m);
    if(view.m[0]!=1 || view.m[2]!=0) return 5;
    pose.enabled=1;pose.tick=GetTickCount64()-1000;stereo_head::Latch();
    if(stereo_head::Read(read,true,GetTickCount64())) return 6;
    pose.tick=GetTickCount64();stereo_head::Latch();fixtureForeground=false;
    ObserveOrientation(nullptr,view.m);ObserveMatrix(nullptr,pos.p);
    if(view.m[2]!=0 || pos.p[0]!=0) return 7;
    fixtureForeground=true;stereo_trial::command.store(0);
    ObserveOrientation(nullptr,view.m);if(view.m[2]!=0) return 8;
    // Inactive concurrent calls preserve the two different ABI output sizes.
    std::atomic<unsigned> errors{0};std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t) workers.emplace_back([&] {
        for(unsigned n=0;n<10000;++n) {
            float m[16];double p[4];
            if(ObserveOrientation(nullptr,m)!=m || GetLastError()!=1234 || m[0]!=1) ++errors;
            if(ObserveMatrix(nullptr,p)!=p || GetLastError()!=5678 || p[3]!=17 || p[0]!=0) ++errors;
        }
    });
    for(auto& worker:workers) worker.join();
    if(errors || stereo_head::rejected.load()) return 9;
    stereo_head::input.store(nullptr);stereo_head::Latch();
    stereo_trial::command.store(1);
    if(ObserveOrientation(nullptr,view.m)!=view.m || view.m[0]!=1 || view.m[2]!=0 || GetLastError()!=1234) return 10;
    if(ObserveMatrix(nullptr,pos.p)!=pos.p || std::abs(pos.p[0]+.032)>1e-6 || pos.p[2]!=0 || GetLastError()!=5678) return 11;
    stereo_trial::command.store(0);
    puts("PASS combined rotation/position axes, output guards, pair latch, stale/disabled/focus/stop gates and 40000 concurrent dual callbacks");
}
