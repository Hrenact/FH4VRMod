#define NOMINMAX
#include <windows.h>
#include <openvr.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc,char** argv) {
    if(argc!=3) { puts("Usage: pose_capture seconds output.csv"); return 2; }
    const int seconds=atoi(argv[1]);
    if(seconds<1 || seconds>600) return 2;
    vr::EVRInitError error=vr::VRInitError_None;
    vr::IVRSystem* system=vr::VR_Init(&error,vr::VRApplication_Background);
    if(error!=vr::VRInitError_None || !system) {
        printf("VR_Init failed: %d %s\n",error,vr::VR_GetVRInitErrorAsEnglishDescription(error)); return 3;
    }
    char model[256]{};
    system->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,vr::Prop_ModelNumber_String,model,sizeof(model));
    printf("HMD model: %s\n",model);
    FILE* file=nullptr;
    if(fopen_s(&file,argv[2],"w") || !file) { vr::VR_Shutdown(); return 4; }
    fputs("elapsed_ms,connected,pose_valid,tracking_result,accepted,orthogonality_error,determinant,yaw_deg,pitch_deg,roll_deg,m00,m01,m02,m10,m11,m12,m20,m21,m22\n",file);
    const ULONGLONG start=GetTickCount64();
    ULONGLONG nextLog=0;
    unsigned samples=0,accepted=0;
    double maxError=0;
    while(GetTickCount64()-start<static_cast<ULONGLONG>(seconds)*1000) {
        vr::TrackedDevicePose_t pose{};
        system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,&pose,1);
        const auto& m=pose.mDeviceToAbsoluteTracking.m;
        bool finite=true;
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) finite=finite && std::isfinite(m[i][j]);
        double ortho=0;
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
            double dot=0;
            for(unsigned k=0;k<3;++k) dot+=static_cast<double>(m[k][i])*m[k][j];
            ortho=std::max(ortho,std::abs(dot-(i==j?1.0:0.0)));
        }
        const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                        -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                        +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
        const bool valid=pose.bDeviceIsConnected && pose.bPoseIsValid &&
            pose.eTrackingResult==vr::TrackingResult_Running_OK && finite && ortho<0.01 && std::abs(det-1)<0.01;
        const double deg=57.295779513;
        const double yaw=valid?std::atan2(-m[0][2],m[2][2])*deg:0;
        const double pitch=valid?-std::asin(std::clamp(static_cast<double>(m[1][2]),-1.0,1.0))*deg:0;
        const double roll=valid?std::atan2(m[1][0],m[1][1])*deg:0;
        const ULONGLONG elapsed=GetTickCount64()-start;
        fprintf(file,"%llu,%d,%d,%d,%d,%.9g,%.9g,%.6f,%.6f,%.6f",elapsed,
            pose.bDeviceIsConnected,pose.bPoseIsValid,pose.eTrackingResult,valid,ortho,det,yaw,pitch,roll);
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) fprintf(file,",%.9g",m[i][j]);
        fputc('\n',file);
        ++samples;
        if(valid) { ++accepted; maxError=std::max(maxError,ortho); }
        if(elapsed>=nextLog) {
            printf("t=%llums connected=%d valid=%d tracking=%d yaw=%.1f pitch=%.1f roll=%.1f\n",elapsed,
                pose.bDeviceIsConnected,valid,pose.eTrackingResult,yaw,pitch,roll);
            fflush(stdout); fflush(file); nextLog=elapsed+2000;
        }
        Sleep(10);
    }
    fclose(file);
    vr::VR_Shutdown();
    printf("samples=%u accepted=%u max_orthogonality_error=%.9g\n",samples,accepted,maxError);
    return accepted?0:5;
}
