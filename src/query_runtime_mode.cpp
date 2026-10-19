#define NOMINMAX
#include <windows.h>
#include <openvr.h>
#include <cstdio>
#include <cstdlib>
#include "runtime_projection.h"
int wmain(int argc,wchar_t** argv) {
    if(argc!=3){puts("Usage: query_runtime_mode absolute-openvr-api-path render-scale");return 2;}
    wchar_t* end=nullptr;float scale=wcstof(argv[2],&end);
    if(end==argv[2]||*end||!std::isfinite(scale)||scale<.25f||scale>2)return 2;
    auto lib=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!lib)return 3;
    using Init=uint32_t(VR_CALLTYPE*)(vr::EVRInitError*,vr::EVRApplicationType,const char*);
    using Shutdown=void(VR_CALLTYPE*)();
    using Interface=void*(VR_CALLTYPE*)(const char*,vr::EVRInitError*);
    auto init=reinterpret_cast<Init>(GetProcAddress(lib,"VR_InitInternal2"));
    auto stop=reinterpret_cast<Shutdown>(GetProcAddress(lib,"VR_ShutdownInternal"));
    auto get=reinterpret_cast<Interface>(GetProcAddress(lib,"VR_GetGenericInterface"));
    if(!init||!stop||!get)return 4;
    vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Background,nullptr);
    if(error!=vr::VRInitError_None){printf("VR init error=%d\n",error);return 5;}
    auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
    if(!system||error!=vr::VRInitError_None){stop();return 6;}
    uint32_t w=0,h=0;system->GetRecommendedRenderTargetSize(&w,&h);
    runtime_projection::Frustum eyes[2]{};
    for(int i=0;i<2;++i){auto& f=eyes[i];system->GetProjectionRaw(static_cast<vr::EVREye>(i),&f.left,&f.right,&f.top,&f.bottom);}
    runtime_projection::Plan plan{};bool ok=runtime_projection::Compute(eyes,w,h,1.f,scale,plan);
    stop();FreeLibrary(lib);
    if(!ok)return 7;
    printf("{\"recommended_width\":%u,\"recommended_height\":%u,\"scale\":%.6f,\"width\":%u,\"height\":%u}\n",w,h,scale,plan.width,plan.height);
    return 0;
}
