#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>

int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 2;
    HMODULE module=LoadLibraryW(argv[1]);
    if (!module) { printf("LoadLibrary failed %lu\n",GetLastError()); return 3; }
    const char* names[]={"CreateDXGIFactory","CreateDXGIFactory1","CreateDXGIFactory2"};
    for (const char* name:names) {
        FARPROC function=GetProcAddress(module,name);
        if (!function) return 4;
        IDXGIFactory* factory=nullptr;
        const IID& iid = !strcmp(name,"CreateDXGIFactory") ? __uuidof(IDXGIFactory) : __uuidof(IDXGIFactory1);
        HRESULT hr;
        if (!strcmp(name,"CreateDXGIFactory2")) {
            using Fn=HRESULT(WINAPI*)(UINT,REFIID,void**);
            hr=reinterpret_cast<Fn>(function)(0,iid,reinterpret_cast<void**>(&factory));
        } else {
            using Fn=HRESULT(WINAPI*)(REFIID,void**);
            hr=reinterpret_cast<Fn>(function)(iid,reinterpret_cast<void**>(&factory));
        }
        printf("%s hr=0x%08lx factory=%p\n",name,hr,factory);
        if (FAILED(hr)||!factory) return 5;
        IDXGIAdapter* adapter=nullptr;
        hr=factory->EnumAdapters(0,&adapter);
        if (SUCCEEDED(hr)) {
            DXGI_ADAPTER_DESC description{};
            hr=adapter->GetDesc(&description);
            if (SUCCEEDED(hr)) wprintf(L"Adapter: %s\n",description.Description);
            adapter->Release();
        }
        factory->Release();
        if (FAILED(hr)) return 6;
    }
    // Match the system ordinal assignment as well as the public name.
    if (GetProcAddress(module,MAKEINTRESOURCEA(12))!=GetProcAddress(module,"CreateDXGIFactory2")) return 7;
    FreeLibrary(module);
    return 0;
}
