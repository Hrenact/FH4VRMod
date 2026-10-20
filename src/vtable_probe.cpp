#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "probe_telemetry.h"
#ifdef FH4VR_LIVE_SCENE
#include "runtime_projection.h"
#endif
#ifdef FH4VR_FOV_TEST
#include "camera_yaw_test.h"
static HoldYawGate fovGate;
#endif
#ifdef FH4VR_PROJECTION_CALIBRATION
#include "camera_yaw_test.h"
static HoldYawGate calibrationGate;
#endif
#ifdef FH4VR_STEREO_HEAD
#include "stereo_head.h"
#endif
#ifdef FH4VR_STEREO_TRIAL
#include "stereo_trial.h"
#endif
#ifdef FH4VR_AER_PROBE
#include "aer_probe.h"
#endif
#ifdef FH4VR_HEADTRACK
#include "head_pose.h"
static HANDLE poseMapping;
static const HeadPose* sharedPose;
static std::atomic_flag posePublishing=ATOMIC_FLAG_INIT;
#endif
#ifdef FH4VR_YAW_TEST
#include "camera_yaw_test.h"
static HoldYawGate yawGate;
static std::atomic_flag publishing=ATOMIC_FLAG_INIT;
#endif

#ifdef FH4VR_EYE_OFFSET
#include "eye_offset.h"
static EyeOffsetGate eyeGate;
static std::atomic_flag eyePublishing=ATOMIC_FLAG_INIT;
#endif

// Slot 12: RCX=this, RDX=matrix output, RAX=matrix output pointer.
using MatrixFunction = void* (__fastcall*)(void*, void*);
static MatrixFunction original;
#ifdef FH4VR_EYE_POSITION
static MatrixFunction orientationFunction;
#endif
static ProbeTelemetry* telemetry;
static HMODULE self;
static void* volatile* slot;
static HANDLE mapping;
#ifdef FH4VR_FOV_PROBE
using FovFunction=float (__fastcall*)(void*);
static FovFunction originalFov;
static void* volatile* fovSlot;
static std::atomic<unsigned long long> fovCalls{0};
#ifdef FH4VR_NEAR_CLIP_TEST
static FovFunction originalNear;
static void* volatile* nearSlot;
static HoldYawGate nearGate;
static float __fastcall ObserveNear(void* camera) {
    const float raw=originalNear(camera);const DWORD saved=GetLastError();
#ifndef FH4VR_MULTICAM_VR
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const auto now=GetTickCount64();
#endif
#ifdef FH4VR_VR_PROJECTION_TEST
    const bool reduce=
#ifdef FH4VR_MULTICAM_VR
        false;
#else
        stereo_trial::Offset(foreground==GetCurrentProcessId(),now)!=0;
#endif
#else
    const bool active=foreground==GetCurrentProcessId() && stereo_trial::command.load()==0;
    const bool wide=fovGate.angle(active,(GetAsyncKeyState(VK_F1)&0x8000)!=0,now)!=0;
    const bool reduce=nearGate.angle(active && wide,(GetAsyncKeyState(VK_F8)&0x8000)!=0,now)!=0;
#endif
    const float result=reduce && std::isfinite(raw) && std::abs(raw-.1f)<.001f?.01f:raw;
    if(!eyePublishing.test_and_set(std::memory_order_acquire)) {
        InterlockedIncrement(&telemetry->sequence);telemetry->position[3]=result;
        InterlockedIncrement(&telemetry->sequence);eyePublishing.clear(std::memory_order_release);
    }
    SetLastError(saved);return result;
}
#endif
static float __fastcall ObserveFov(void* camera) {
    float result=originalFov(camera);const DWORD saved=GetLastError();
#ifdef FH4VR_FOV_TEST
    const float raw=result;
#ifndef FH4VR_MULTICAM_VR
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
#ifdef FH4VR_VR_PROJECTION_TEST
    const float requested=
#ifdef FH4VR_MULTICAM_VR
        0.f;
#else
        stereo_trial::Offset(foreground==GetCurrentProcessId(),GetTickCount64())!=0?1.f:0.f;
#endif
#else
    const float requested=fovGate.angle(foreground==GetCurrentProcessId() && stereo_trial::command.load()==0,
        (GetAsyncKeyState(VK_F1)&0x8000)!=0,GetTickCount64());
#endif
    if(requested!=0 && std::isfinite(raw) && raw>=.2f && raw<=2.4f) {
#if defined(FH4VR_LIVE_SCENE)
        // Five degrees is this verified FH4 camera's downstream adjustment,
        // not a headset FOV. Runtime source projection is published before F2.
        const float target=runtime_projection::verticalRadians.load();
        if(std::abs(raw-.83775806427f)<.001f && target>.2f && target<2.4f)
            result=target-.0872664626f;
#elif defined(FH4VR_WIDE_FOV_TEST)
        // Diagnostic for the calibrated 48deg getter + 5deg downstream adjustment.
        // PICO runtime raw vertical tangents measured at +/-1.279527.
        // Reject other camera baselines; this is not a general VR FOV override.
        if(std::abs(raw-.83775806427f)<.001f)
            result=2.f*std::atan(1.279527f)-.0872664626f;
#else
        result=raw+requested;
#endif
    }
#endif
#endif
    ++fovCalls;
    if(!eyePublishing.test_and_set(std::memory_order_acquire)) {
        InterlockedIncrement(&telemetry->sequence);
        telemetry->fov=result;
#ifdef FH4VR_FOV_TEST
        telemetry->position[1]=raw;
        telemetry->position[2]=result-raw;
#endif
        InterlockedIncrement(&telemetry->sequence);
        eyePublishing.clear(std::memory_order_release);
    }
    SetLastError(saved);return result;
}
#endif
#ifdef FH4VR_STEREO_HEAD
static void* volatile* headSlot;
static void* __fastcall ObserveOrientation(void* camera,void* output) {
    void* result=orientationFunction(camera,output);
    const DWORD saved=GetLastError();
#ifndef FH4VR_MULTICAM_VR
    if(result==output && result) stereo_head::Apply(static_cast<float*>(result));
#endif
#ifdef FH4VR_PROJECTION_CALIBRATION
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const float angle=calibrationGate.angle(foreground==GetCurrentProcessId() && stereo_trial::command.load()==0,
        (GetAsyncKeyState(VK_F8)&0x8000)!=0,GetTickCount64());
    bool applied=false;
    if(angle!=0 && result==output && result) applied=ApplyViewYaw(static_cast<float*>(result),angle);
    if(!eyePublishing.test_and_set(std::memory_order_acquire)) {
        InterlockedIncrement(&telemetry->sequence);
        telemetry->position[3]=applied?angle:0;
        InterlockedIncrement(&telemetry->sequence);
        eyePublishing.clear(std::memory_order_release);
    }
#endif
    SetLastError(saved);return result;
}
#endif

static void* __fastcall ObserveMatrix(void* camera, void* output) {
    const DWORD incomingError=GetLastError();
    InterlockedIncrement64(&telemetry->entered);
    InterlockedExchange64(&telemetry->lastEntry,reinterpret_cast<LONG64>(camera));
    SetLastError(incomingError);
    void* result=original(camera,output);
    const DWORD outgoingError=GetLastError();
    InterlockedIncrement64(&telemetry->returned);
#ifdef FH4VR_HEADTRACK
    HeadPose pose{};
    bool applied=false;
    if(sharedPose && CopyHeadPose(sharedPose,pose) && PoseUsable(pose,GetTickCount64())) {
        if(result==output && result) applied=ApplyHeadRotation(static_cast<float*>(result),pose.rotation);
        if(applied) InterlockedIncrement64(&telemetry->matched);
        else InterlockedIncrement64(&telemetry->readFailures);
    }
    if(!posePublishing.test_and_set(std::memory_order_acquire)) {
        InterlockedIncrement(&telemetry->sequence);
        telemetry->calls=static_cast<unsigned long long>(InterlockedCompareExchange64(&telemetry->returned,0,0));
        telemetry->camera=reinterpret_cast<unsigned long long>(camera);
        telemetry->tick=GetTickCount64(); telemetry->yaw=applied?pose.yaw:0;
        InterlockedIncrement(&telemetry->sequence);
        posePublishing.clear(std::memory_order_release);
    }
#endif
#ifdef FH4VR_YAW_TEST
    DWORD foregroundPid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    const auto tick=GetTickCount64();
    const float angle=yawGate.angle(foregroundPid==GetCurrentProcessId(),
                                    (GetAsyncKeyState(VK_F8)&0x8000)!=0,tick);
    bool applied=false;
    if (angle!=0) {
        if (result==output && result) applied=ApplyViewYaw(static_cast<float*>(result),angle);
        if (applied) InterlockedIncrement64(&telemetry->matched);
        else InterlockedIncrement64(&telemetry->readFailures);
    }
    if (!publishing.test_and_set(std::memory_order_acquire)) {
        InterlockedIncrement(&telemetry->sequence);
        telemetry->calls=static_cast<unsigned long long>(InterlockedCompareExchange64(&telemetry->returned,0,0));
        telemetry->camera=reinterpret_cast<unsigned long long>(camera);
        telemetry->tick=tick;
        telemetry->yaw=applied ? angle : 0;
        InterlockedIncrement(&telemetry->sequence);
        publishing.clear(std::memory_order_release);
    }
#endif
#ifdef FH4VR_EYE_OFFSET
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    const auto tick=GetTickCount64();
#ifdef FH4VR_STEREO_TRIAL
    float eye=
#ifdef FH4VR_MULTICAM_VR
        0.f;
#else
        stereo_trial::Offset(foregroundPid==GetCurrentProcessId(),tick);
#endif
#elif defined(FH4VR_AER_PROBE)
    unsigned eyePhase=aer_probe::state.Read(foregroundPid==GetCurrentProcessId(),tick);
    float eye=aer_probe::Eye(eyePhase);
    if(eyePhase) ++aer_probe::state.calls[eyePhase];
#else
    float eye=eyeGate.offset(foregroundPid==GetCurrentProcessId(),
        (GetAsyncKeyState(VK_F4)&0x8000)!=0,(GetAsyncKeyState(VK_F5)&0x8000)!=0,tick);
#endif
    bool applied=false;
    if(eye!=0) {
        if(result==output && result) {
#ifdef FH4VR_EYE_POSITION
            alignas(16) float view[16]{};
            if(orientationFunction(camera,view)==view) {
#ifdef FH4VR_STEREO_HEAD
                if(stereo_head::Apply(view)) applied=ApplyEyePosition(static_cast<double*>(result),view,eye);
#else
                applied=ApplyEyePosition(static_cast<double*>(result),view,eye);
#endif
            }
#else
            applied=ApplyEyeOffset(static_cast<float*>(result),eye);
#endif
        }
        if(applied) InterlockedIncrement64(&telemetry->matched);
        else InterlockedIncrement64(&telemetry->readFailures);
    }
#ifdef FH4VR_AER_PROBE
    if(eyePhase && eye!=0) {
        if(applied) ++aer_probe::state.applied[eyePhase];else ++aer_probe::state.rejected[eyePhase];
    }
#endif
#ifdef FH4VR_STEREO_TRIAL
    if(eye!=0) {if(applied) ++stereo_trial::applied;else ++stereo_trial::rejected;}
#endif
    if(!eyePublishing.test_and_set(std::memory_order_acquire)) {
        InterlockedIncrement(&telemetry->sequence);
        telemetry->calls=static_cast<unsigned long long>(InterlockedCompareExchange64(&telemetry->returned,0,0));
        telemetry->camera=reinterpret_cast<unsigned long long>(camera);telemetry->tick=tick;
        telemetry->position[0]=applied?eye:0;telemetry->yaw=0;
        InterlockedIncrement(&telemetry->sequence);eyePublishing.clear(std::memory_order_release);
    }
#endif
    SetLastError(outgoingError);
    return result;
}

// Only the validated, naturally aligned vtable pointer. Runtime FH4 maps this
// .rdata region as executable/read/write; preserve those permissions unchanged.
// Keep original and the DLL alive until process exit for concurrent callers.
static DWORD ReplaceSlot(void* volatile* address, void* expected, void* replacement) {
    MEMORY_BASIC_INFORMATION info{};
    if (reinterpret_cast<uintptr_t>(address)%sizeof(void*) ||
        !VirtualQuery(const_cast<void**>(address),&info,sizeof(info)) ||
        info.State!=MEM_COMMIT || info.Protect&PAGE_GUARD ||
        (info.Protect!=PAGE_READONLY && info.Protect!=PAGE_READWRITE &&
         info.Protect!=PAGE_EXECUTE_READWRITE)) return ERROR_INVALID_ADDRESS;
    if (info.Protect==PAGE_READWRITE || info.Protect==PAGE_EXECUTE_READWRITE) {
        return InterlockedCompareExchangePointer(address,replacement,expected)==expected
            ? ERROR_SUCCESS : ERROR_REVISION_MISMATCH;
    }
    DWORD oldProtection=0;
    if (!VirtualProtect(const_cast<void**>(address),sizeof(void*),PAGE_READWRITE,&oldProtection)) return GetLastError();
    void* previous=InterlockedCompareExchangePointer(address,replacement,expected);
    DWORD ignored=0;
    if (!VirtualProtect(const_cast<void**>(address),sizeof(void*),oldProtection,&ignored)) return GetLastError();
    return previous==expected ? ERROR_SUCCESS : ERROR_REVISION_MISMATCH;
}

static void Log(DWORD status) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(self,path,MAX_PATH)) return;
    wchar_t* tail=wcsrchr(path,L'\\');
    if (!tail) return;
    wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"vtable-probe.log");
    FILE* file=nullptr;
    if (!_wfopen_s(&file,path,L"a") && file) {
        fprintf(file,"pid=%lu status=%lu slot=%p original=%p observer=%p\n",
                GetCurrentProcessId(),status,static_cast<const void*>(const_cast<void**>(slot)),
                reinterpret_cast<void*>(original),reinterpret_cast<void*>(&ObserveMatrix));
        fclose(file);
    }
}

#ifdef FH4VR_UI_PROJECTION_PROBE
#include "ui_projection_probe.h"
#ifdef FH4VR_UI_MATERIAL_PROBE
#include "ui_material_probe.h"
#endif
#ifdef FH4VR_UI_MAP_PROBE
#include "ui_map_probe.h"
#endif
#ifdef FH4VR_UI_MAP_CLIP
#include "ui_map_mode_probe.h"
#endif
#include "menu_camera_probe.h"
#include "presentation_camera_probe.h"
#include "multicam_probe.h"
#ifdef FH4VR_RENDER_MATRIX_PROBE
#include "render_matrix_probe.h"
#endif
#endif
extern "C" void SetProbeModule(HMODULE module) { self=module; }
extern "C" DWORD WINAPI StartProbe(void*) {
    if (telemetry) return ERROR_ALREADY_INITIALIZED;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
#ifdef FH4VR_EYE_POSITION
    slot=reinterpret_cast<void* volatile*>(base+0x55397d0+52*sizeof(void*));
    original=reinterpret_cast<MatrixFunction>(base+0x32817b0);
    orientationFunction=reinterpret_cast<MatrixFunction>(base+0x3281120);
    void* orientationSlot=nullptr;SIZE_T orientationRead=0;
    unsigned char orientationBytes[10]{};
    constexpr unsigned char orientationExpected[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x70};
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+0x55397d0+12*sizeof(void*)),&orientationSlot,sizeof(orientationSlot),&orientationRead) ||
       orientationRead!=sizeof(orientationSlot) || orientationSlot!=reinterpret_cast<void*>(orientationFunction) ||
       !ReadProcessMemory(GetCurrentProcess(),orientationSlot,orientationBytes,sizeof(orientationBytes),&orientationRead) ||
       orientationRead!=sizeof(orientationBytes) || memcmp(orientationBytes,orientationExpected,sizeof(orientationExpected))) {
        Log(ERROR_REVISION_MISMATCH);return ERROR_REVISION_MISMATCH;
    }
#else
    slot=reinterpret_cast<void* volatile*>(base+0x55397d0+12*sizeof(void*));
    original=reinterpret_cast<MatrixFunction>(base+0x3281120);
#endif
    void* actual=nullptr; SIZE_T copied=0;
    unsigned char entry[32]{};
#ifdef FH4VR_EYE_POSITION
    constexpr unsigned char expected[]={0x48,0x8b,0x81,0x70,0x02,0x00,0x00,0x48,0x89,0x02,0x48,0x8b,0x81,0x78,0x02,0x00,0x00};
#else
    constexpr unsigned char expected[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x70};
#endif
    if (!ReadProcessMemory(GetCurrentProcess(),const_cast<void**>(slot),&actual,sizeof(actual),&copied) ||
        copied!=sizeof(actual) || actual!=reinterpret_cast<void*>(original) ||
        !ReadProcessMemory(GetCurrentProcess(),actual,entry,sizeof(entry),&copied) ||
        copied!=sizeof(entry) || memcmp(entry,expected,sizeof(expected))) {
        Log(ERROR_REVISION_MISMATCH); return ERROR_REVISION_MISMATCH;
    }
    wchar_t name[80]; swprintf_s(name,L"Local\\FH4VR_CameraProbe_%lu",GetCurrentProcessId());
#ifdef FH4VR_FOV_PROBE
    fovSlot=reinterpret_cast<void* volatile*>(base+0x55397d0+19*sizeof(void*));
    originalFov=reinterpret_cast<FovFunction>(base+0x3280830);
    void* fovPointer=nullptr;unsigned char fovBytes[9]{};
    constexpr unsigned char fovExpected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
    if(!ReadProcessMemory(GetCurrentProcess(),const_cast<void**>(fovSlot),&fovPointer,sizeof(fovPointer),&copied) ||
       copied!=sizeof(fovPointer) || fovPointer!=reinterpret_cast<void*>(originalFov) ||
       !ReadProcessMemory(GetCurrentProcess(),fovPointer,fovBytes,sizeof(fovBytes),&copied) ||
       copied!=sizeof(fovBytes) || memcmp(fovBytes,fovExpected,sizeof(fovExpected))) {
        Log(ERROR_REVISION_MISMATCH);return ERROR_REVISION_MISMATCH;
    }
#endif
#ifdef FH4VR_NEAR_CLIP_TEST
    nearSlot=reinterpret_cast<void* volatile*>(base+0x55397d0+49*sizeof(void*));
    originalNear=reinterpret_cast<FovFunction>(base+0x3281660);
    void* nearPointer=nullptr;unsigned char nearBytes[9]{};
    constexpr unsigned char nearExpected[]={0xf3,0x0f,0x10,0x05,0x58,0x0a,0x1b,0x04,0xc3};
    if(!ReadProcessMemory(GetCurrentProcess(),const_cast<void**>(nearSlot),&nearPointer,sizeof(nearPointer),&copied) ||
       copied!=sizeof(nearPointer) || nearPointer!=reinterpret_cast<void*>(originalNear) ||
       !ReadProcessMemory(GetCurrentProcess(),nearPointer,nearBytes,sizeof(nearBytes),&copied) ||
       copied!=sizeof(nearBytes) || memcmp(nearBytes,nearExpected,sizeof(nearExpected))) {
        Log(ERROR_REVISION_MISMATCH);return ERROR_REVISION_MISMATCH;
    }
#endif
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ProbeTelemetry),name);
    if (!mapping) return GetLastError();
    if (GetLastError()==ERROR_ALREADY_EXISTS) { CloseHandle(mapping); mapping=nullptr; return ERROR_ALREADY_EXISTS; }
    telemetry=static_cast<ProbeTelemetry*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ProbeTelemetry)));
    if (!telemetry) { DWORD error=GetLastError(); CloseHandle(mapping); mapping=nullptr; return error; }
    ZeroMemory(telemetry,sizeof(*telemetry));
    telemetry->pid=GetCurrentProcessId(); telemetry->phase=1;
    telemetry->target=reinterpret_cast<unsigned long long>(original);
    telemetry->detour=reinterpret_cast<unsigned long long>(&ObserveMatrix);
    MEMORY_BASIC_INFORMATION slotInfo{};
    if (VirtualQuery(const_cast<void**>(slot),&slotInfo,sizeof(slotInfo))) telemetry->targetProtection=slotInfo.Protect;
    memcpy(telemetry->originalBytes,entry,sizeof(entry));
#ifdef FH4VR_HEADTRACK
    poseMapping=OpenFileMappingW(FILE_MAP_READ,FALSE,PoseMappingName);
    if(poseMapping) sharedPose=static_cast<const HeadPose*>(MapViewOfFile(poseMapping,FILE_MAP_READ,0,0,sizeof(HeadPose)));
    if(!sharedPose) { telemetry->status=ERROR_NOT_READY; telemetry->phase=4; Log(ERROR_NOT_READY); return ERROR_NOT_READY; }
#endif
    // No trampoline: target byte snapshots refer to the unmodified original method.
#ifdef FH4VR_STEREO_HEAD
#if (!defined(FH4VR_WIDE_FOV_TEST) || defined(FH4VR_VR_PROJECTION_TEST)) && !defined(FH4VR_LIVE_SCENE)
    if(!stereo_head::Open()) {telemetry->status=ERROR_NOT_READY;Log(ERROR_NOT_READY);return ERROR_NOT_READY;}
#endif
    // Monitor-only and initial stationary Scene diagnostics leave pose input null.
    // Camera/FOV hooks must not depend on an expiring external pose bridge.
    headSlot=reinterpret_cast<void* volatile*>(base+0x55397d0+12*sizeof(void*));
#endif
    DWORD status=ReplaceSlot(slot,reinterpret_cast<void*>(original),reinterpret_cast<void*>(&ObserveMatrix));
#ifdef FH4VR_STEREO_HEAD
    if(status==ERROR_SUCCESS) {
        status=ReplaceSlot(headSlot,reinterpret_cast<void*>(orientationFunction),reinterpret_cast<void*>(&ObserveOrientation));
        if(status!=ERROR_SUCCESS) ReplaceSlot(slot,reinterpret_cast<void*>(&ObserveMatrix),reinterpret_cast<void*>(original));
    }
#endif
    telemetry->status=status;
#ifdef FH4VR_FOV_PROBE
    if(status==ERROR_SUCCESS) {
        status=ReplaceSlot(fovSlot,reinterpret_cast<void*>(originalFov),reinterpret_cast<void*>(&ObserveFov));
        if(status!=ERROR_SUCCESS) {
#ifdef FH4VR_STEREO_HEAD
            ReplaceSlot(headSlot,reinterpret_cast<void*>(&ObserveOrientation),reinterpret_cast<void*>(orientationFunction));
#endif
            ReplaceSlot(slot,reinterpret_cast<void*>(&ObserveMatrix),reinterpret_cast<void*>(original));
        }
    }
    telemetry->status=status;
#endif
#ifdef FH4VR_NEAR_CLIP_TEST
    if(status==ERROR_SUCCESS) {
        status=ReplaceSlot(nearSlot,reinterpret_cast<void*>(originalNear),reinterpret_cast<void*>(&ObserveNear));
        if(status!=ERROR_SUCCESS) {
            ReplaceSlot(fovSlot,reinterpret_cast<void*>(&ObserveFov),reinterpret_cast<void*>(originalFov));
            ReplaceSlot(headSlot,reinterpret_cast<void*>(&ObserveOrientation),reinterpret_cast<void*>(orientationFunction));
            ReplaceSlot(slot,reinterpret_cast<void*>(&ObserveMatrix),reinterpret_cast<void*>(original));
        }
    }
    telemetry->status=status;
#endif
    telemetry->enabled=(*slot==reinterpret_cast<void*>(&ObserveMatrix));
    telemetry->phase=telemetry->enabled ? 3 : 4;
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(original),telemetry->patchedBytes,32,&copied);
    Log(status);
#ifdef FH4VR_UI_PROJECTION_PROBE
    if(status==ERROR_SUCCESS)ui_projection_probe::Start(base,self,ReplaceSlot);
#ifdef FH4VR_UI_ROOT_PROBE
    if(status==ERROR_SUCCESS) {
        const DWORD rootStatus=ui_root_probe::Start(base,self,ReplaceSlot);
        ui_projection_probe::Record("root_scope_enabled",rootStatus,nullptr,nullptr);
    }
#endif
#ifdef FH4VR_UI_MATERIAL_PROBE
    if(status==ERROR_SUCCESS) {
        const DWORD materialStatus=ui_material_probe::Start(base,self,ReplaceSlot);
        ui_projection_probe::Record("material_probe_enabled",materialStatus,nullptr,nullptr);
    }
#endif
#ifdef FH4VR_UI_MAP_PROBE
    if(status==ERROR_SUCCESS) {
        const DWORD mapStatus=ui_map_probe::Start(base,self,ReplaceSlot,
            [](){return stereo_trial::Offset(true,GetTickCount64())!=0;});
        ui_projection_probe::Record("map_probe_enabled",mapStatus,nullptr,nullptr);
#ifdef FH4VR_UI_MAP_CLIP
        if(mapStatus==ERROR_SUCCESS) {
            const DWORD modeStatus=ui_map_mode_probe::Start(base,self,ReplaceSlot);
            ui_projection_probe::Record("map_clip_enabled",modeStatus,nullptr,nullptr);
        }
#endif
    }
#endif
    if(status==ERROR_SUCCESS)Log(multicam_probe::Start(base,self,ReplaceSlot));
#ifdef FH4VR_RENDER_MATRIX_PROBE
    if(status==ERROR_SUCCESS)ui_projection_probe::Record("render_matrix_probe_enabled",render_matrix_probe::Start(base,ReplaceSlot),nullptr,nullptr);
#endif
#endif
    return status;
}

extern "C" DWORD WINAPI StopProbe(void*) {
#ifdef FH4VR_UI_PROJECTION_PROBE
#ifdef FH4VR_UI_MAP_CLIP
    ui_map_mode_probe::Stop(ReplaceSlot);
#endif
#ifdef FH4VR_UI_MAP_PROBE
    ui_map_probe::Stop(ReplaceSlot);
#endif
#ifdef FH4VR_UI_MATERIAL_PROBE
    ui_material_probe::Stop(ReplaceSlot);
#endif
#ifdef FH4VR_UI_ROOT_PROBE
    ui_root_probe::Stop(ReplaceSlot);
#endif
    menu_camera_probe::Stop(ReplaceSlot);
    presentation_camera_probe::Stop(ReplaceSlot);
    multicam_probe::Stop(ReplaceSlot);
#ifdef FH4VR_RENDER_MATRIX_PROBE
    render_matrix_probe::Stop(ReplaceSlot);
#endif
    ui_projection_probe::StopClipHooks(ReplaceSlot);
    if(ui_pass_probe::slot)ReplaceSlot(ui_pass_probe::slot,reinterpret_cast<void*>(&ui_pass_probe::Observe),reinterpret_cast<void*>(ui_pass_probe::original));
    if(ui_pass_probe::endSlot)ReplaceSlot(ui_pass_probe::endSlot,reinterpret_cast<void*>(&ui_pass_probe::ObserveEnd),reinterpret_cast<void*>(ui_pass_probe::originalEnd));
    if(ui_pass_probe::layerEndSlot)ReplaceSlot(ui_pass_probe::layerEndSlot,reinterpret_cast<void*>(&ui_pass_probe::ObserveLayerEnd),reinterpret_cast<void*>(ui_pass_probe::originalLayerEnd));
    if(ui_pass_probe::drawSlot)ReplaceSlot(ui_pass_probe::drawSlot,reinterpret_cast<void*>(&ui_pass_probe::ObserveDraw),reinterpret_cast<void*>(ui_pass_probe::originalDraw));
#endif
#ifdef FH4VR_NEAR_CLIP_TEST
    if(nearSlot) ReplaceSlot(nearSlot,reinterpret_cast<void*>(&ObserveNear),reinterpret_cast<void*>(originalNear));
#endif
#ifdef FH4VR_FOV_PROBE
    if(fovSlot) ReplaceSlot(fovSlot,reinterpret_cast<void*>(&ObserveFov),reinterpret_cast<void*>(originalFov));
#endif
#ifdef FH4VR_STEREO_HEAD
    stereo_trial::command.store(0);
    if(headSlot) ReplaceSlot(headSlot,reinterpret_cast<void*>(&ObserveOrientation),reinterpret_cast<void*>(orientationFunction));
#endif
    if (!telemetry || !telemetry->enabled) return ERROR_SUCCESS;
    DWORD status=ReplaceSlot(slot,reinterpret_cast<void*>(&ObserveMatrix),reinterpret_cast<void*>(original));
    telemetry->status=status;
    telemetry->enabled=(*slot==reinterpret_cast<void*>(&ObserveMatrix));
    telemetry->phase=telemetry->enabled ? 3 : 4;
    Log(status); return status;
}
