#define NOMINMAX
#include "head_pose.h"
#include <openvr.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

static bool GameForeground() {
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return false;
    wchar_t path[MAX_PATH]{}; DWORD length=MAX_PATH;
    bool ok=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;
    CloseHandle(process);
    const wchar_t* name=wcsrchr(path,L'\\');
    return ok && name && !_wcsicmp(name+1,L"ForzaHorizon4.exe");
}
int main(int argc,char** argv) {
    if(argc!=3) { puts("Usage: pose_bridge seconds output.csv (F7 toggle, F6 recenter in FH4)"); return 2; }
    int seconds=atoi(argv[1]); if(seconds<1 || seconds>3600) return 2;
    vr::EVRInitError error=vr::VRInitError_None;
    auto system=vr::VR_Init(&error,vr::VRApplication_Background);
    if(error!=vr::VRInitError_None || !system) { printf("VR init error %d\n",error); return 3; }
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(HeadPose),PoseMappingName);
    if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS) {
        puts("Pose mapping unavailable or already exists; close existing bridge and game first.");
        if(mapping) CloseHandle(mapping); vr::VR_Shutdown(); return 4;
    }
    auto shared=static_cast<HeadPose*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(HeadPose)));
    if(!shared) { CloseHandle(mapping); vr::VR_Shutdown(); return 5; }
    FILE* log=nullptr;
    if(fopen_s(&log,argv[2],"w") || !log) { UnmapViewOfFile(shared); CloseHandle(mapping); vr::VR_Shutdown(); return 6; }
    fputs("elapsed_ms,valid,foreground,enabled,toggle_key,recenter_key,yaw_deg,pitch_deg,roll_deg\n",log);
    bool enabled=false,oldToggle=true,oldRecenter=true,wasForeground=false;
    float center[9]={1,0,0,0,1,0,0,0,1};
    auto start=GetTickCount64(),lastTick=start,nextLog=start;
    unsigned validCount=0;
    while(GetTickCount64()-start<static_cast<ULONGLONG>(seconds)*1000) {
        vr::TrackedDevicePose_t pose{};
        system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,&pose,1);
        float current[9];
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) current[i*3+j]=pose.mDeviceToAbsoluteTracking.m[i][j];
        bool valid=pose.bDeviceIsConnected && pose.bPoseIsValid && pose.eTrackingResult==vr::TrackingResult_Running_OK && RotationValid(current);
        bool foreground=GameForeground();
        bool toggle=(GetAsyncKeyState(VK_F7)&0x8000)!=0,recenter=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
        auto now=GetTickCount64();
        if(!valid || !foreground || now-lastTick>250) enabled=false;
        // Require a fresh key edge after returning to the game or losing tracking.
        if(valid && foreground && wasForeground && now-lastTick<=250) {
            if(toggle && !oldToggle) { enabled=!enabled; if(enabled) memcpy(center,current,sizeof(center)); }
            if(recenter && !oldRecenter) memcpy(center,current,sizeof(center));
        }
        oldToggle=toggle; oldRecenter=recenter; wasForeground=foreground && valid; lastTick=now;
        HeadPose packet{}; packet.magic=PoseMagic; packet.version=1;
        packet.producerPid=GetCurrentProcessId(); packet.tick=now; packet.valid=valid; packet.enabled=enabled;
        if(valid) { RelativeViewRotation(center,current,packet.rotation); ++validCount; }
        else { packet.rotation[0]=packet.rotation[4]=packet.rotation[8]=1; }
        packet.yaw=std::atan2(packet.rotation[2],packet.rotation[8]);
        packet.pitch=std::asin(std::clamp(packet.rotation[5],-1.f,1.f));
        packet.roll=std::atan2(packet.rotation[3],packet.rotation[4]);
        InterlockedIncrement(&shared->sequence);
        // Never copy over the seqlock itself.
        shared->magic=packet.magic; shared->version=packet.version;
        memcpy(reinterpret_cast<char*>(shared)+12,reinterpret_cast<char*>(&packet)+12,sizeof(packet)-12);
        InterlockedIncrement(&shared->sequence);
        fprintf(log,"%llu,%d,%d,%d,%d,%d,%.5f,%.5f,%.5f\n",now-start,valid,foreground,enabled,toggle,recenter,
            packet.yaw*57.2957795,packet.pitch*57.2957795,packet.roll*57.2957795);
        if(now>=nextLog) {
            printf("t=%llums valid=%d foreground=%d enabled=%d\n",now-start,valid,foreground,enabled);
            fflush(stdout); fflush(log); nextLog=now+2000;
        }
        Sleep(8);
    }
    InterlockedIncrement(&shared->sequence); shared->enabled=0; shared->valid=0; shared->tick=GetTickCount64(); InterlockedIncrement(&shared->sequence);
    fclose(log); UnmapViewOfFile(shared); CloseHandle(mapping); vr::VR_Shutdown();
    printf("Finished valid_samples=%u\n",validCount); return 0;
}
