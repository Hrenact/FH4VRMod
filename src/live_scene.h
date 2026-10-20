#pragma once
#include <openvr.h>
#include "projection_crop.h"
#include "scene_pose.h"
#include "runtime_projection.h"
#include "ui_smoothing.h"
#include "vr_timing_probe.h"
#include "frame_order_trace.h"
#include "stereo_trial.h"
#include "pose_wait_policy.h"
#ifdef FH4VR_SOURCE_DRIVEN_STEREO
#include "render_cycle_state.h"
#endif
#ifdef FH4VR_RIGHT_EYE_MONO_PROBE
#include "right_eye_probe.h"
#endif
#include <cstdlib>
// Sample once before each eye pair; submit that exact absolute pose with the pair.
class LiveScene {
    HMODULE library=nullptr;
    using Init=uint32_t (VR_CALLTYPE *)(vr::EVRInitError*,vr::EVRApplicationType,const char*);
    using Shutdown=void (VR_CALLTYPE *)();
    using Interface=void* (VR_CALLTYPE *)(const char*,vr::EVRInitError*);
    Shutdown shutdown=nullptr;
    vr::IVRSystem* system=nullptr;
    vr::IVRCompositor* compositor=nullptr;
    bool initialized=false;
    ScenePoseState pair{},previousPair{};
#ifdef FH4VR_RIGHT_EYE_MONO_PROBE
    right_eye_probe::Gate rightEyeProbe;
    bool rightEyeProbeActive=false;
#endif
    bool recenterHeld=true;
    PoseWaitPolicy poseWaitPolicy;
    bool pairWaited=false;
    uint64_t recoveryPoseSamples=0;
    ui_smoothing::Filter uiFilter;
    runtime_projection::Frustum frusta[2]{};
    runtime_projection::Plan projection{};
    uint32_t recommendedWidth=0,recommendedHeight=0,sourceWidth=0,sourceHeight=0;
    float renderScale=1;
#ifdef FH4VR_TIMING_PROBE
    double poseReady=0,previousPoseReady=0,lastPairSubmit=0;
#endif
public:
    unsigned submitted=0;
    bool PairWaited()const{return pairWaited;}
    vr::IVRSystem* System() const {return system;}
    ~LiveScene(){Stop();}
    HRESULT Start(bool=true) {
        if(GetModuleHandleW(L"openvr_api.dll") && !library)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        HMODULE owner=nullptr;wchar_t directory[MAX_PATH]{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&runtime_projection::verticalRadians),&owner))return E_FAIL;
        const DWORD length=GetModuleFileNameW(owner,directory,MAX_PATH);
        if(!length || length>=MAX_PATH)return E_FAIL;
        auto tail=wcsrchr(directory,L'\\');if(!tail)return E_FAIL;tail[1]=0;
        wchar_t apiPath[MAX_PATH]{},iniPath[MAX_PATH]{};
        if(swprintf_s(apiPath,L"%lsfh4vr\\openvr_api.dll",directory)<0 ||
           swprintf_s(iniPath,L"%lsfh4vr.ini",directory)<0)return E_FAIL;
        wchar_t scaleWide[64]{};GetPrivateProfileStringW(L"VR",L"RenderScale",L"1.0",scaleWide,64,iniPath);
        wchar_t* end=nullptr;renderScale=wcstof(scaleWide,&end);
        if(end==scaleWide || *end || !std::isfinite(renderScale) || renderScale<.25f || renderScale>2.f)return E_INVALIDARG;
        if(!library)library=LoadLibraryExW(apiPath,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!library)return HRESULT_FROM_WIN32(GetLastError());
        auto init=reinterpret_cast<Init>(GetProcAddress(library,"VR_InitInternal2"));
        shutdown=reinterpret_cast<Shutdown>(GetProcAddress(library,"VR_ShutdownInternal"));
        auto get=reinterpret_cast<Interface>(GetProcAddress(library,"VR_GetGenericInterface"));
        if(!init || !shutdown || !get)return E_NOINTERFACE;
        vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Scene,nullptr);
        if(error!=vr::VRInitError_None){Log("scene_init_error code=%d",error);return E_FAIL;}
        initialized=true;
        system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
        if(!system || error!=vr::VRInitError_None){Stop();return E_NOINTERFACE;}
        compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
        if(!compositor || error!=vr::VRInitError_None){Stop();return E_NOINTERFACE;}
        system->GetRecommendedRenderTargetSize(&recommendedWidth,&recommendedHeight);
        for(int i=0;i<2;++i){auto& f=frusta[i];system->GetProjectionRaw(static_cast<vr::EVREye>(i),&f.left,&f.right,&f.top,&f.bottom);}
        runtime_projection::Plan square{};
        if(!runtime_projection::Compute(frusta,recommendedWidth,recommendedHeight,1.f,renderScale,square)){Stop();return E_INVALIDARG;}
        Log("scene_resolution recommended_eye=%ux%u scale=%f suggested_square=%ux%u",recommendedWidth,recommendedHeight,renderScale,square.width,square.height);
        poseWaitPolicy={};pairWaited=false;recoveryPoseSamples=0;
        submitted=0;pair={};previousPair={};recenterHeld=true;
#ifdef FH4VR_RIGHT_EYE_MONO_PROBE
        rightEyeProbe={};rightEyeProbeActive=false;
        Log("scene_right_eye_probe ready=1 key=F11 max_ms=5000 normal_pairing_unchanged=1");
#endif
#ifdef FH4VR_TIMING_PROBE
        poseReady=previousPoseReady=lastPairSubmit=0;
#endif
#ifdef FH4VR_SCENE_IMPLICIT_HANDOFF
        Log("scene_handoff mode=wait_poses_after_present deferred_texture_seal=1");
#endif
        Log("scene_ready paired_pose=1 automatic_tracking=1");return S_OK;
    }
    HRESULT ConfigureSource(uint32_t width,uint32_t height) {
        if(!system || !width || !height || !runtime_projection::Compute(frusta,recommendedWidth,recommendedHeight,
            static_cast<float>(width)/height,renderScale,projection))return E_INVALIDARG;
        // Existing camera hook accepts this validated angular range only.
        if(projection.vertical<=.2f || projection.vertical>=2.4f)return E_INVALIDARG;
        sourceWidth=width;sourceHeight=height;
        runtime_projection::verticalRadians.store(projection.vertical);
        Log("scene_source actual=%ux%u suggested_same_aspect=%ux%u vertical_radians=%f",width,height,projection.width,projection.height,projection.vertical);
        return S_OK;
    }
    HRESULT BeginPair(HeadPose& gamePose) {
        gamePose={};if(!compositor)return E_UNEXPECTED;
        vr::VREvent_t event{};
        while(system->PollNextEvent(&event,sizeof(event)))if(event.eventType==vr::VREvent_Quit)return E_ABORT;
        // The previous pair can still be in the game's render pipeline.
        previousPair=pair;
#ifdef FH4VR_TIMING_PROBE
        previousPoseReady=poseReady;
#endif
        vr::TrackedDevicePose_t pose{};
        vr::EVRCompositorError waited=vr::VRCompositorError_None;
        pairWaited=true;
#ifdef FH4VR_RECOVERY_POSE_SAMPLE
        pairWaited=poseWaitPolicy.NeedWait(submitted);
#endif
        if(pairWaited){
            frame_order_trace::Record(frame_order_trace::PoseWaitEnter,stereo_trial::command.load());
            waited=vr_timing::Measure(vr_timing::Stage::WaitPoses,[&]{return compositor->WaitGetPoses(&pose,1,nullptr,0);});
            frame_order_trace::Record(frame_order_trace::PoseWaitExit,stereo_trial::command.load());
            if(waited==vr::VRCompositorError_None)poseWaitPolicy.WaitSucceeded(submitted);
        }else{
            // Fresh current pose, same tracking universe; never relabel a prior image.
            // This call does not advance the compositor's submission frame.
            system->GetDeviceToAbsoluteTrackingPose(compositor->GetTrackingSpace(),0.f,&pose,1);
            ++recoveryPoseSamples;
        }
#ifdef FH4VR_TIMING_PROBE
        poseReady=vr_timing::Now();
#endif
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        bool focused=foregroundPid==GetCurrentProcessId();
        bool pressed=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
        bool recenter=focused && pressed && !recenterHeld;recenterHeld=!focused || pressed;
        if(waited!=vr::VRCompositorError_None || !pair.Begin(pose,recenter,GetTickCount64())) {
            pair.valid=false;Log("scene_pair_pose_error code=%d",waited);return E_FAIL;
        }
        gamePose=pair.game;
        float uiRotation[9];
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)uiRotation[i*3+j]=pair.absolute.m[i][j];
        ui_smoothing::Publish(uiFilter.Update(uiRotation,GetTickCount64(),recenter||pair.generation==1));
        if(recenter || pair.generation==1)Log("scene_recenter generation=%llu",pair.generation);
        return S_OK;
    }
    const ScenePoseState& CurrentPose() const {return pair;}
    HRESULT Submit(ID3D12Resource* resource,ID3D12CommandQueue* queue,uint64_t generation=0
#ifdef FH4VR_SOURCE_DRIVEN_STEREO
                   ,const render_cycle::Snapshot* source=nullptr
#endif
                   ) {
        if(!compositor || !resource || !queue)return E_UNEXPECTED;
        ScenePoseState* selected=SelectScenePose(pair,previousPair,generation);
#ifdef FH4VR_SOURCE_DRIVEN_STEREO
        ScenePoseState sourcePair{};
        if(!source || !CopySourceScenePose(source->pose,source->absolute,source->poseGeneration,
                                          generation,GetTickCount64(),sourcePair))return S_FALSE;
        selected=&sourcePair;
#endif
        if(!selected){Log("scene_drop_unknown_generation requested=%llu current=%llu previous=%llu",generation,pair.generation,previousPair.generation);return S_FALSE;}
#if defined(FH4VR_TIMING_PROBE) && !defined(FH4VR_SOURCE_DRIVEN_STEREO)
        const double selectedPoseReady=selected==&pair?poseReady:previousPoseReady;
#endif
        auto& submittedPair=*selected;
        const auto now=GetTickCount64();
        const bool wasValid=submittedPair.valid;
        if(submittedPair.DropIfStale(now)){
            Log("scene_drop_stale_pair generation=%llu valid=%d age_ms=%llu",submittedPair.generation,wasValid,
                now>=submittedPair.game.tick?now-submittedPair.game.tick:0);
            return S_FALSE; // Skip this pair; next BeginPair obtains a fresh pose. Never relabel old pixels.
        }
        vr::VREvent_t event{};
        while(system->PollNextEvent(&event,sizeof(event)))if(event.eventType==vr::VREvent_Quit)return E_ABORT;
        const auto desc=resource->GetDesc();
        if(desc.Width!=uint64_t(sourceWidth)*2 || desc.Height!=sourceHeight)return E_INVALIDARG;
        vr::VRTextureBounds_t bounds[2]{};
        for(int eye=0;eye<2;++eye){
            auto crop=projection.crops[eye];
            bounds[eye]={(eye+crop.u0)*.5f,crop.v0,(eye+crop.u1)*.5f,crop.v1};
            if(!submitted)Log("scene_bounds eye=%d uv=%f,%f,%f,%f",eye,bounds[eye].uMin,bounds[eye].vMin,bounds[eye].uMax,bounds[eye].vMax);
        }
        vr::D3D12TextureData_t data{resource,queue,0};
#ifdef FH4VR_RIGHT_EYE_MONO_PROBE
        const bool monoRight=rightEyeProbe.Active((GetAsyncKeyState(VK_F11)&0x8000)!=0,now);
        right_eye_probe::Apply(monoRight,bounds);
        if(monoRight!=rightEyeProbeActive){
            Log("scene_right_eye_probe active=%d generation=%llu source_eye=right",monoRight,submittedPair.generation);
            rightEyeProbeActive=monoRight;
        }
#endif
        vr::VRTextureWithPose_t image{};
        image.handle=&data;image.eType=vr::TextureType_DirectX12;image.eColorSpace=vr::ColorSpace_Gamma;
        image.mDeviceToAbsoluteTracking=submittedPair.absolute;
#ifdef FH4VR_TIMING_PROBE
        const double submitAt=vr_timing::Now();
#ifndef FH4VR_SOURCE_DRIVEN_STEREO
        if(selectedPoseReady)vr_timing::Record(vr_timing::Stage::PoseAge,submitAt-selectedPoseReady);
#endif
        if(lastPairSubmit)vr_timing::Record(vr_timing::Stage::PairInterval,submitAt-lastPairSubmit);
        lastPairSubmit=submitAt;
#endif
        auto left=vr_timing::Measure(vr_timing::Stage::SubmitLeft,[&]{return compositor->Submit(vr::Eye_Left,&image,&bounds[0],vr::Submit_TextureWithPose);});
        auto right=vr_timing::Measure(vr_timing::Stage::SubmitRight,[&]{return compositor->Submit(vr::Eye_Right,&image,&bounds[1],vr::Submit_TextureWithPose);});
        if(left!=vr::VRCompositorError_None || right!=vr::VRCompositorError_None){Log("scene_submit_error left=%d right=%d",left,right);compositor->ClearLastSubmittedFrame();return E_FAIL;}
        ++submitted;
        if(submitted==1 || submitted%300==0)Log("scene_pair_submit generation=%llu age_ms=%llu",submittedPair.generation,GetTickCount64()-submittedPair.game.tick);
        submittedPair.valid=false; // Never relabel/re-submit a consumed submittedPair.
#ifndef FH4VR_SCENE_IMPLICIT_HANDOFF
        Handoff();
#endif
        return S_OK;
    }
    void Handoff(){if(compositor){
#ifdef FH4VR_FRAME_ORDER_TRACE
        frame_order_trace::Record(frame_order_trace::HandoffEnter,stereo_trial::command.load());
#endif
        vr_timing::Measure(vr_timing::Stage::Handoff,[&]{compositor->PostPresentHandoff();});
#ifdef FH4VR_FRAME_ORDER_TRACE
        frame_order_trace::Record(frame_order_trace::HandoffExit,stereo_trial::command.load());
#endif
    }}
    void Stop(){
        uiFilter={};ui_smoothing::Publish({});
        if(compositor)compositor->ClearLastSubmittedFrame();
        compositor=nullptr;system=nullptr;
        runtime_projection::verticalRadians.store(0);sourceWidth=sourceHeight=0;
        pair.valid=previousPair.valid=false;
        if(initialized){shutdown();initialized=false;Log("scene_stop submitted=%u recovery_pose_samples=%llu",submitted,recoveryPoseSamples);}
    }
};
