#pragma once
#include <openvr.h>
// Loaded only on explicit activation, outside DllMain. GPU resources must outlive Stop().
class LiveOverlay {
    HMODULE library=nullptr;
    using Init=uint32_t (VR_CALLTYPE *)(vr::EVRInitError*,vr::EVRApplicationType,const char*);
    using Shutdown=void (VR_CALLTYPE *)();
    using Interface=void* (VR_CALLTYPE *)(const char*,vr::EVRInitError*);
    Shutdown shutdown=nullptr;
    vr::IVROverlay* overlay=nullptr;
    vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
    bool initialized=false;
public:
    unsigned submitted=0;
    ~LiveOverlay() { Stop(); }
    HRESULT Start(bool stereo=false) {
        // Do not take ownership of an OpenVR session initialized by another module.
        if(GetModuleHandleW(L"openvr_api.dll") && !library) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if(!library) library=LoadLibraryExW(L"F:\\FH4VRMod\\third_party\\openvr\\bin\\win64\\openvr_api.dll",nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!library) return HRESULT_FROM_WIN32(GetLastError());
        auto init=reinterpret_cast<Init>(GetProcAddress(library,"VR_InitInternal2"));
        shutdown=reinterpret_cast<Shutdown>(GetProcAddress(library,"VR_ShutdownInternal"));
        auto get=reinterpret_cast<Interface>(GetProcAddress(library,"VR_GetGenericInterface"));
        if(!init || !shutdown || !get) return E_NOINTERFACE;
        vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Overlay,nullptr);
        if(error!=vr::VRInitError_None) {Log("overlay_init_error code=%d",error);return E_FAIL;}
        initialized=true;overlay=static_cast<vr::IVROverlay*>(get(vr::IVROverlay_Version,&error));
        if(!overlay || error!=vr::VRInitError_None) {Stop();return E_NOINTERFACE;}
        auto result=overlay->CreateOverlay("fh4vr.live.d3d12","FH4VR Live Preview",&handle);
        // Game backbuffer alpha is not a transparency mask for the VR panel.
        if(result==vr::VROverlayError_None) result=overlay->SetOverlayFlag(handle,vr::VROverlayFlags_IgnoreTextureAlpha,true);
        if(result==vr::VROverlayError_None && stereo) result=overlay->SetOverlayFlag(handle,vr::VROverlayFlags_SideBySide_Parallel,true);
        if(result==vr::VROverlayError_None) result=overlay->SetOverlayWidthInMeters(handle,1.6f);
        vr::HmdMatrix34_t transform{};transform.m[0][0]=transform.m[1][1]=transform.m[2][2]=1;transform.m[2][3]=-2;
        if(result==vr::VROverlayError_None) result=overlay->SetOverlayTransformTrackedDeviceRelative(handle,vr::k_unTrackedDeviceIndex_Hmd,&transform);
        if(result!=vr::VROverlayError_None) {Log("overlay_start_error code=%d",result);Stop();return E_FAIL;}
        submitted=0;Log("overlay_ready");return S_OK;
    }
    HRESULT Submit(ID3D12Resource* resource,ID3D12CommandQueue* queue) {
        if(!overlay || !resource || !queue) return E_UNEXPECTED;
        vr::D3D12TextureData_t data{resource,queue,0};vr::Texture_t texture{&data,vr::TextureType_DirectX12,vr::ColorSpace_Gamma};
        auto result=overlay->SetOverlayTexture(handle,&texture);
        if(result==vr::VROverlayError_None && !submitted) result=overlay->ShowOverlay(handle);
        if(result!=vr::VROverlayError_None) {Log("overlay_submit_error code=%d",result);return E_FAIL;}
        if(!submitted) {
            uint32_t width=0,height=0,flags=0;float aspect=0,meters=0;vr::VRTextureBounds_t bounds{};
            auto sizeError=overlay->GetOverlayTextureSize(handle,&width,&height);
            auto flagsError=overlay->GetOverlayFlags(handle,&flags);
            auto aspectError=overlay->GetOverlayTexelAspect(handle,&aspect);
            auto widthError=overlay->GetOverlayWidthInMeters(handle,&meters);
            auto boundsError=overlay->GetOverlayTextureBounds(handle,&bounds);
            auto desc=resource->GetDesc();
            Log("overlay_layout gpu=%llux%u runtime=%ux%u flags=%u aspect=%f meters=%f bounds=%f,%f,%f,%f errors=%d,%d,%d,%d,%d",
                desc.Width,desc.Height,width,height,flags,aspect,meters,bounds.uMin,bounds.vMin,bounds.uMax,bounds.vMax,
                sizeError,flagsError,aspectError,widthError,boundsError);
        }
        ++submitted;return S_OK;
    }
    void Stop() {
        if(overlay && handle!=vr::k_ulOverlayHandleInvalid) {
            overlay->HideOverlay(handle);overlay->ClearOverlayTexture(handle);overlay->DestroyOverlay(handle);
        }
        handle=vr::k_ulOverlayHandleInvalid;overlay=nullptr;
        if(initialized) {shutdown();initialized=false;Log("overlay_stop submitted=%u",submitted);}
        // Keep API module loaded for the lifetime of this pinned diagnostic DLL.
    }
};
