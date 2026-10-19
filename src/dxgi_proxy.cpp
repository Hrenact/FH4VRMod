#include <windows.h>
#include <cstdio>
#include <cwchar>

static HMODULE self;
static HMODULE systemDxgi;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static const char* const names[] = {
#include "dxgi_exports.inc"
};
static FARPROC addresses[_countof(names)]{};
static LONG seen[_countof(names)]{};
#ifdef FH4VR_GRAPHICS_PROBE
#include <unknwn.h>
extern "C" void ObserveFactory(IUnknown*);
extern "C" void SetGraphicsModule(HMODULE);
using FactoryFn=HRESULT(WINAPI*)(REFIID,void**);
using Factory2Fn=HRESULT(WINAPI*)(UINT,REFIID,void**);
static HRESULT WrappedFactory(unsigned index,REFIID iid,void** output) {
    HRESULT hr=reinterpret_cast<FactoryFn>(addresses[index])(iid,output);
    DWORD saved=GetLastError();if(SUCCEEDED(hr) && output) ObserveFactory(static_cast<IUnknown*>(*output));SetLastError(saved);return hr;
}
static HRESULT WINAPI WrappedFactory0(REFIID iid,void** output) { return WrappedFactory(3,iid,output); }
static HRESULT WINAPI WrappedFactory1(REFIID iid,void** output) { return WrappedFactory(4,iid,output); }
static HRESULT WINAPI WrappedFactory2(UINT flags,REFIID iid,void** output) {
    HRESULT hr=reinterpret_cast<Factory2Fn>(addresses[5])(flags,iid,output);
    DWORD saved=GetLastError();if(SUCCEEDED(hr) && output) ObserveFactory(static_cast<IUnknown*>(*output));SetLastError(saved);return hr;
}
#endif
#ifdef FH4VR_CAMERA_PROBE
extern "C" DWORD WINAPI StartProbe(void*);
extern "C" void SetProbeModule(HMODULE);
static INIT_ONCE probeOnce = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK InitializeProbe(PINIT_ONCE,PVOID,PVOID*) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr,path,MAX_PATH);
    const wchar_t* name=wcsrchr(path,L'\\');
    if (name && !_wcsicmp(name+1,L"ForzaHorizon4.exe")) StartProbe(nullptr);
    return TRUE;
}
#endif

static void Log(const char* event, const char* name, DWORD error) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(self, path, MAX_PATH)) return;
    wchar_t* tail = wcsrchr(path, L'\\');
    if (!tail) return;
    wcscpy_s(tail+1, MAX_PATH-(tail+1-path), L"fh4vr-dxgi.log");
    FILE* file = nullptr;
    if (_wfopen_s(&file,path,L"a") || !file) return;
    SYSTEMTIME time; GetSystemTime(&time);
    fprintf(file,"%04u-%02u-%02uT%02u:%02u:%02uZ pid=%lu tid=%lu %s %s error=%lu\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,
            GetCurrentProcessId(),GetCurrentThreadId(),event,name,error);
    fclose(file);
}

static BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT length = GetSystemDirectoryW(path,MAX_PATH);
    if (!length || length >= MAX_PATH-10) return FALSE;
    wcscat_s(path,L"\\dxgi.dll");
    systemDxgi = LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!systemDxgi || systemDxgi == self) { Log("system_load_failed","dxgi",GetLastError()); return FALSE; }
    for (unsigned i=0;i<_countof(names);++i) addresses[i]=GetProcAddress(systemDxgi,names[i]);
    Log("forwarder_ready","system32_dxgi",0);
    return TRUE;
}

extern "C" FARPROC ResolveDxgi(unsigned index) {
    const DWORD previousError = GetLastError();
    if (index >= _countof(names) || !InitOnceExecuteOnce(&once,Initialize,nullptr,nullptr) || !addresses[index]) {
        Log("missing_export",index<_countof(names)?names[index]:"invalid_index",GetLastError());
        RaiseFailFastException(nullptr,nullptr,0);
        return nullptr;
    }
    if (InterlockedCompareExchange(&seen[index],1,0)==0) Log("forwarding",names[index],0);
#ifdef FH4VR_CAMERA_PROBE
    if (index>=3 && index<=5) InitOnceExecuteOnce(&probeOnce,InitializeProbe,nullptr,nullptr);
#endif
    SetLastError(previousError);
#ifdef FH4VR_GRAPHICS_PROBE
    if(index==3) return reinterpret_cast<FARPROC>(&WrappedFactory0);
    if(index==4) return reinterpret_cast<FARPROC>(&WrappedFactory1);
    if(index==5) return reinterpret_cast<FARPROC>(&WrappedFactory2);
#endif
    return addresses[index];
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        self=module;
#ifdef FH4VR_GRAPHICS_PROBE
        SetGraphicsModule(module);
#endif
#ifdef FH4VR_CAMERA_PROBE
        SetProbeModule(module);
#endif
    }
    return TRUE;
}
