#define FH4VR_HEADTRACK
#include "vtable_probe.cpp"
static void* __fastcall MatrixFixture(void*,void* output) {
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    memcpy(output,identity,sizeof(identity)); SetLastError(1234); return output;
}
int main() {
    ProbeTelemetry counters{}; telemetry=&counters; original=&MatrixFixture;
    HeadPose pose{}; pose.magic=PoseMagic;pose.version=1;pose.valid=1;pose.enabled=1;
    pose.tick=GetTickCount64();
    float a=0.1f,c=std::cos(a),s=std::sin(a);
    float rotation[9]={c,0,s,0,1,0,-s,0,c};memcpy(pose.rotation,rotation,sizeof(rotation));
    sharedPose=&pose;
    struct { unsigned before=0x12345678; float matrix[16]{}; unsigned after=0x87654321; } output;
    if(ObserveMatrix(&output,output.matrix)!=output.matrix || GetLastError()!=1234) return 1;
    if(std::abs(output.matrix[2]-s)>1e-6 || counters.matched!=1 || counters.readFailures) return 2;
    if(output.before!=0x12345678 || output.after!=0x87654321) return 3;
    pose.enabled=0; ObserveMatrix(&output,output.matrix);
    if(output.matrix[2]!=0 || output.matrix[0]!=1 || counters.matched!=1) return 4;
    pose.enabled=1;pose.tick=GetTickCount64()-1000;ObserveMatrix(&output,output.matrix);
    if(output.matrix[2]!=0 || counters.matched!=1) return 5;
    pose.tick=GetTickCount64();pose.valid=0;ObserveMatrix(&output,output.matrix);
    if(output.matrix[2]!=0 || counters.matched!=1) return 6;
    sharedPose=nullptr;ObserveMatrix(&output,output.matrix);
    if(output.matrix[2]!=0 || counters.entered!=5 || counters.returned!=5) return 7;
    puts("PASS: real callback transforms only fresh enabled pose; disabled/stale/lost/absent pose restores original output; return, last error and bounds preserved.");return 0;
}
