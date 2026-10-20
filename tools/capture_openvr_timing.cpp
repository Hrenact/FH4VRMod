#define NOMINMAX
#include <windows.h>
#include <openvr.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <share.h>
#include <string>

// Read-only background client: never calls WaitGetPoses, Submit, or focus APIs.
// Focus/renderer IDs describe the query, not a guaranteed owner for each historic
// frame. Drop backlog on a process change; consumers must still exclude transitions.
static std::atomic<bool> running{true};
static BOOL WINAPI Interrupt(DWORD kind) {
    if(kind==CTRL_C_EVENT || kind==CTRL_BREAK_EVENT){running.store(false);return TRUE;}
    return FALSE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5){puts("Usage: capture_openvr_timing absolute-openvr-api-path output.csv seconds expected-game-pid");return 2;}
    wchar_t* end=nullptr;unsigned long seconds=wcstoul(argv[3],&end,10);
    if(!end || *end || seconds<1 || seconds>3600)return 2;
    unsigned long expected=wcstoul(argv[4],&end,10);if(!end || *end || !expected)return 2;
    FILE* output=_wfsopen(argv[2],L"wx",_SH_DENYWR);if(!output)return 3;
    const std::wstring stopPath=std::wstring(argv[2])+L".stop";
    auto library=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!library){printf("load_error=%lu\n",GetLastError());fclose(output);return 4;}
    using Init=uint32_t(VR_CALLTYPE*)(vr::EVRInitError*,vr::EVRApplicationType,const char*);
    using Shutdown=void(VR_CALLTYPE*)();
    using Interface=void*(VR_CALLTYPE*)(const char*,vr::EVRInitError*);
    auto init=reinterpret_cast<Init>(GetProcAddress(library,"VR_InitInternal2"));
    auto stop=reinterpret_cast<Shutdown>(GetProcAddress(library,"VR_ShutdownInternal"));
    auto get=reinterpret_cast<Interface>(GetProcAddress(library,"VR_GetGenericInterface"));
    if(!init || !stop || !get){fclose(output);FreeLibrary(library);return 5;}
    vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Background,nullptr);
    if(error!=vr::VRInitError_None){printf("init_error=%d\n",error);fclose(output);FreeLibrary(library);return 6;}
    auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
    auto compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
    if(!system || !compositor || error!=vr::VRInitError_None){stop();fclose(output);FreeLibrary(library);return 7;}
    vr::ETrackedPropertyError propertyError=vr::TrackedProp_Success;
    const float hz=system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,vr::Prop_DisplayFrequency_Float,&propertyError);
    printf("background_readonly=1 expected_game_pid=%lu display_hz=%.3f property_error=%d seconds=%lu\n",expected,hz,propertyError,seconds);fflush(stdout);
    fputs("sample_tick,expected_pid,focus_before,renderer_before,focus_after,renderer_after,transition,frame_index,system_seconds,presents,mis_presented,dropped,reprojection_flags,vsyncs_ready,vsyncs_first_view,pre_submit_gpu_ms,post_submit_gpu_ms,total_render_gpu_ms,compositor_gpu_ms,compositor_cpu_ms,compositor_idle_cpu_ms,client_interval_ms,present_cpu_ms,wait_present_cpu_ms,submit_ms,wait_poses_called_ms,new_poses_ready_ms,new_frame_ready_ms,compositor_update_start_ms,compositor_update_end_ms,compositor_render_start_ms,transfer_latency_ms\n",output);
    fflush(output);SetConsoleCtrlHandler(Interrupt,TRUE);
    uint64_t rows=0,queries=0,empty=0;double lastTime=-1;uint32_t lastIndex=0,previousFocus=UINT32_MAX,previousRenderer=UINT32_MAX;
    const auto start=GetTickCount64();auto lastFlush=start;
    while(running.load() && GetTickCount64()-start<static_cast<uint64_t>(seconds)*1000 && GetFileAttributesW(stopPath.c_str())==INVALID_FILE_ATTRIBUTES) {
        const auto focusBefore=compositor->GetCurrentSceneFocusProcess();
        const auto rendererBefore=compositor->GetLastFrameRenderer();
        std::array<vr::Compositor_FrameTiming,128> frames{};frames[0].m_nSize=sizeof(frames[0]);
        auto count=compositor->GetFrameTimings(frames.data(),static_cast<uint32_t>(frames.size()));++queries;
        const auto focusAfter=compositor->GetCurrentSceneFocusProcess();
        const auto rendererAfter=compositor->GetLastFrameRenderer();
        const bool transition=focusBefore!=focusAfter || rendererBefore!=rendererAfter || focusBefore!=previousFocus || rendererBefore!=previousRenderer;
        previousFocus=focusAfter;previousRenderer=rendererAfter;const auto tick=GetTickCount64();
        if(count>frames.size()){puts("invalid_frame_count");break;}
        if(!count)++empty;
        for(uint32_t i=transition && count ? count-1:0;i<count;++i){const auto& t=frames[i];
            if(t.m_flSystemTimeInSeconds<lastTime || (t.m_flSystemTimeInSeconds==lastTime && t.m_nFrameIndex==lastIndex))continue;
            lastTime=t.m_flSystemTimeInSeconds;lastIndex=t.m_nFrameIndex;
            fprintf(output,"%llu,%lu,%u,%u,%u,%u,%u,%u,%.9f,%u,%u,%u,%u,%u,%u",static_cast<unsigned long long>(tick),expected,
                focusBefore,rendererBefore,focusAfter,rendererAfter,transition?1u:0u,t.m_nFrameIndex,t.m_flSystemTimeInSeconds,
                t.m_nNumFramePresents,t.m_nNumMisPresented,t.m_nNumDroppedFrames,t.m_nReprojectionFlags,t.m_nNumVSyncsReadyForUse,t.m_nNumVSyncsToFirstView);
            const float values[]={t.m_flPreSubmitGpuMs,t.m_flPostSubmitGpuMs,t.m_flTotalRenderGpuMs,t.m_flCompositorRenderGpuMs,
                t.m_flCompositorRenderCpuMs,t.m_flCompositorIdleCpuMs,t.m_flClientFrameIntervalMs,t.m_flPresentCallCpuMs,t.m_flWaitForPresentCpuMs,
                t.m_flSubmitFrameMs,t.m_flWaitGetPosesCalledMs,t.m_flNewPosesReadyMs,t.m_flNewFrameReadyMs,t.m_flCompositorUpdateStartMs,
                t.m_flCompositorUpdateEndMs,t.m_flCompositorRenderStartMs,t.m_flTransferLatencyMs};
            static_assert(sizeof(values)/sizeof(values[0])==17);
            for(float value:values)fprintf(output,",%.6f",value);fputc('\n',output);++rows;
        }
        if(tick-lastFlush>=1000){fflush(output);lastFlush=tick;}
        Sleep(200);
    }
    const bool writeError=ferror(output)!=0;const int closeStatus=fclose(output);
    stop();FreeLibrary(library);
    printf("rows=%llu queries=%llu empty=%llu elapsed_ms=%llu\n",static_cast<unsigned long long>(rows),static_cast<unsigned long long>(queries),
        static_cast<unsigned long long>(empty),static_cast<unsigned long long>(GetTickCount64()-start));
    return writeError || closeStatus ? 8:0;
}
