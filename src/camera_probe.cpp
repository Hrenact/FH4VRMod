#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "probe_telemetry.h"

static HMODULE self;
static std::atomic<unsigned long long> calls{0};
static std::atomic<uintptr_t> lastCamera{0};
static bool enabled = false;
static void* target;
using CameraUpdate = void(__fastcall*)(void*, void*);
static CameraUpdate original;
static HANDLE mapping;
static ProbeTelemetry* telemetry;
static std::atomic_flag publishing = ATOMIC_FLAG_INIT;
static uintptr_t gameBase;

static void __fastcall Observe(void* camera, void* positionOutput) {
    // Count before touching the original so a crash in its trampoline is distinguishable.
    if (telemetry) {
        InterlockedIncrement64(&telemetry->entered);
        InterlockedExchange64(&telemetry->lastEntry,reinterpret_cast<LONG64>(camera));
    }
    original(camera, positionOutput);
    if (telemetry) InterlockedIncrement64(&telemetry->returned);
    lastCamera.store(reinterpret_cast<uintptr_t>(camera), std::memory_order_relaxed);
    calls.fetch_add(1, std::memory_order_relaxed);
    if (telemetry && !publishing.test_and_set(std::memory_order_acquire)) {
        unsigned char state[0x300];
        SIZE_T copied = 0;
        // ReadProcessMemory avoids dereferencing an unexpected or short-lived object.
        if (ReadProcessMemory(GetCurrentProcess(),camera,state,sizeof(state),&copied) && copied==sizeof(state)) {
            uintptr_t vtable=0;
            memcpy(&vtable,state,sizeof(vtable));
            if (vtable==gameBase+0x55397d0) {
                InterlockedIncrement64(&telemetry->matched);
                InterlockedIncrement(&telemetry->sequence);
                telemetry->calls=calls.load(std::memory_order_relaxed);
                telemetry->camera=reinterpret_cast<uintptr_t>(camera);
                telemetry->tick=GetTickCount64();
                memcpy(telemetry->position,state+0x160,sizeof(telemetry->position));
                memcpy(&telemetry->fov,state+0x1b0,sizeof(float));
                memcpy(&telemetry->yaw,state+0x2e4,sizeof(float));
                InterlockedIncrement(&telemetry->sequence);
            }
        } else InterlockedIncrement64(&telemetry->readFailures);
        publishing.clear(std::memory_order_release);
    }
}

static void Log(const char* event, int status) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"camera-probe.log");
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a") || !file) return;
    LARGE_INTEGER tick;
    QueryPerformanceCounter(&tick);
    const uintptr_t camera = lastCamera.load(std::memory_order_relaxed);
    unsigned char state[0x380]{};
    SIZE_T copied = 0;
    const bool readable = camera && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(camera), state, sizeof(state), &copied) && copied == sizeof(state);
    float yaw = 0;
    if (readable) memcpy(&yaw, state + 0x2e4, sizeof(yaw));
    fprintf(file, "%s status=%d qpc=%lld calls=%llu camera=0x%llx readable=%d yaw=%f\n",
            event, status, tick.QuadPart, calls.load(), static_cast<unsigned long long>(camera), readable, yaw);
    if (telemetry) {
        telemetry->status=static_cast<DWORD>(status);
        fprintf(file,"pid=%lu target=0x%llx trampoline=0x%llx detour=0x%llx phase=%lu entered=%lld returned=%lld matched=%lld\n",
                GetCurrentProcessId(),telemetry->target,telemetry->trampoline,telemetry->detour,
                telemetry->phase,telemetry->entered,telemetry->returned,telemetry->matched);
        const unsigned char* buffers[]={telemetry->originalBytes,telemetry->patchedBytes,telemetry->trampolineBytes};
        const char* labels[]={"original_bytes","patched_bytes","trampoline_bytes"};
        for(unsigned i=0;i<3;++i) {
            fprintf(file,"%s=",labels[i]);
            for(unsigned j=0;j<32;++j) fprintf(file,"%02x",buffers[i][j]);
            fputc('\n',file);
        }
    }
    fclose(file);
}

extern "C" __declspec(dllexport) DWORD WINAPI StartProbe(void*) {
    if (enabled) return 0;
    if (original) {
        const auto status = MH_EnableHook(target);
        if (status != MH_OK) { Log("reenable_failed", status); return 100 + status; }
        enabled = true;
        if (telemetry) { telemetry->enabled=1; telemetry->phase=3; }
        Log("restarted", 0);
        return 0;
    }
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    gameBase=base;
    target = reinterpret_cast<void*>(base + 0x327ce40);
    // This exact prologue was read and disassembled on SHA256 b838a79a...abbe.
    constexpr unsigned char expected[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55};
    unsigned char actual[32];
    SIZE_T copied;
    if (!ReadProcessMemory(GetCurrentProcess(), target, actual, sizeof(actual), &copied) || copied != sizeof(actual) || memcmp(actual, expected, sizeof(expected))) {
        Log("prologue_mismatch", 1); return 1;
    }
    wchar_t mappingName[80];
    swprintf_s(mappingName,L"Local\\FH4VR_CameraProbe_%lu",GetCurrentProcessId());
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ProbeTelemetry),mappingName);
    if (!mapping || GetLastError()==ERROR_ALREADY_EXISTS) { Log("mapping_failed",GetLastError()); if(mapping) CloseHandle(mapping); mapping=nullptr; return 2; }
    telemetry=static_cast<ProbeTelemetry*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ProbeTelemetry)));
    if (!telemetry) { Log("mapping_view_failed",GetLastError()); CloseHandle(mapping); mapping=nullptr; return 3; }
    ZeroMemory(telemetry,sizeof(*telemetry));
    telemetry->pid=GetCurrentProcessId();
    telemetry->phase=1;
    telemetry->target=reinterpret_cast<unsigned long long>(target);
    telemetry->detour=reinterpret_cast<unsigned long long>(&Observe);
    memcpy(telemetry->originalBytes,actual,sizeof(actual));
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(target,&memory,sizeof(memory))) telemetry->targetProtection=memory.Protect;
    auto status = MH_Initialize();
    if (status != MH_OK) { Log("initialize_failed", status); return 100 + status; }
    status = MH_CreateHook(target, reinterpret_cast<void*>(&Observe), reinterpret_cast<void**>(&original));
    if (status != MH_OK) { Log("create_failed", status); MH_Uninitialize(); return 100 + status; }
    telemetry->trampoline=reinterpret_cast<unsigned long long>(original);
    telemetry->phase=2;
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(original),telemetry->trampolineBytes,sizeof(telemetry->trampolineBytes),&copied);
    Log("prepared",0);
    status = MH_EnableHook(target);
    if (status != MH_OK) { Log("enable_failed", status); MH_RemoveHook(target); MH_Uninitialize(); original = nullptr; return 100 + status; }
    enabled = true;
    telemetry->enabled=1;
    telemetry->phase=3;
    ReadProcessMemory(GetCurrentProcess(),target,telemetry->patchedBytes,sizeof(telemetry->patchedBytes),&copied);
    Log("started", 0);
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI SnapshotProbe(void*) {
    Log("snapshot", 0); return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI StopProbe(void*) {
    if (!enabled) return 0;
    const auto status = MH_DisableHook(target);
    if (status != MH_OK) { Log("disable_failed", status); return 100 + status; }
    enabled = false;
    if (telemetry) { telemetry->enabled=0; telemetry->phase=4; }
    // Keep DLL and trampoline allocated until game exit: a callback may still be returning.
    Log("stopped", 0);
    return 0;
}

#ifdef FH4VR_CAMERA_PROBE
extern "C" void SetProbeModule(HMODULE module) { self=module; }
#else
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { self = module; }
    return TRUE;
}
#endif
