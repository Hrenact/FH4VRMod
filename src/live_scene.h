#pragma once
#include <openvr.h>
#include "projection_crop.h"
#include "scene_pose.h"
#include "runtime_projection.h"
#include "ui_smoothing.h"
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
    ScenePoseState pair{};
    bool recenterHeld=true;
    ui_smoothing::Filter uiFilter;
    runtime_projection::Frustum frusta[2]{};
    runtime_projection::Plan projection{};
    uint32_t recommendedWidth=0,recommendedHeight=0,sourceWidth=0,sourceHeight=0;
    float renderScale=1;
public:
    unsigned submitted=0;
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
        submitted=0;pair={};recenterHeld=true;
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
        vr::TrackedDevicePose_t pose{};
        auto waited=compositor->WaitGetPoses(&pose,1,nullptr,0);
        bool pressed=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
        bool recenter=pressed && !recenterHeld;recenterHeld=pressed;
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
    HRESULT Submit(ID3D12Resource* resource,ID3D12CommandQueue* queue) {
        if(!compositor || !resource || !queue)return E_UNEXPECTED;
        const auto now=GetTickCount64();
        const bool wasValid=pair.valid;
        if(pair.DropIfStale(now)){
            Log("scene_drop_stale_pair generation=%llu valid=%d age_ms=%llu",pair.generation,wasValid,
                now>=pair.game.tick?now-pair.game.tick:0);
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
        vr::VRTextureWithPose_t image{};
        image.handle=&data;image.eType=vr::TextureType_DirectX12;image.eColorSpace=vr::ColorSpace_Gamma;
        image.mDeviceToAbsoluteTracking=pair.absolute;
        auto left=compositor->Submit(vr::Eye_Left,&image,&bounds[0],vr::Submit_TextureWithPose);
        auto right=compositor->Submit(vr::Eye_Right,&image,&bounds[1],vr::Submit_TextureWithPose);
        if(left!=vr::VRCompositorError_None || right!=vr::VRCompositorError_None){Log("scene_submit_error left=%d right=%d",left,right);compositor->ClearLastSubmittedFrame();return E_FAIL;}
        ++submitted;
        if(submitted==1 || submitted%300==0)Log("scene_pair_submit generation=%llu age_ms=%llu",pair.generation,GetTickCount64()-pair.game.tick);
        pair.valid=false; // Never relabel/re-submit a consumed pair.
        compositor->PostPresentHandoff();return S_OK;
    }
    void Stop(){
        uiFilter={};ui_smoothing::Publish({});
        if(compositor)compositor->ClearLastSubmittedFrame();
        compositor=nullptr;system=nullptr;
        runtime_projection::verticalRadians.store(0);sourceWidth=sourceHeight=0;
        pair.valid=false;
        if(initialized){shutdown();initialized=false;Log("scene_stop submitted=%u",submitted);}
    }
};
