#include "runtime_mode.h"
// Opt-in diagnostic mode generated from the runtime before game launch.
namespace display_mode_probe {
inline std::atomic<unsigned> calls{0};
inline UINT targetWidth=0,targetHeight=0;
inline INIT_ONCE configOnce=INIT_ONCE_STATIC_INIT;
inline BOOL CALLBACK Config(PINIT_ONCE,PVOID,PVOID*) {
    wchar_t path[MAX_PATH]{};DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
    if(!length || length>=MAX_PATH)return TRUE;
    auto tail=wcsrchr(path,L'\\');if(!tail)return TRUE;
    if(wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"fh4vr.ini"))return TRUE;
    if(GetPrivateProfileIntW(L"Diagnostic",L"AddRuntimeMode",0,path)!=1)return TRUE;
    UINT w=GetPrivateProfileIntW(L"Diagnostic",L"RuntimeWidth",0,path);
    UINT h=GetPrivateProfileIntW(L"Diagnostic",L"RuntimeHeight",0,path);
    if(runtime_mode::Valid(w,h)){targetWidth=w;targetHeight=h;}
    Log("runtime_mode_config size=%ux%u enabled=%d",w,h,targetWidth!=0);return TRUE;
}
using ListFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput*,DXGI_FORMAT,UINT,UINT*,DXGI_MODE_DESC*);
using List1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput1*,DXGI_FORMAT,UINT,UINT*,DXGI_MODE_DESC1*);
template<class T> void Record(unsigned api,unsigned serial,DXGI_FORMAT format,UINT flags,UINT capacity,
    UINT* count,T* modes,HRESULT hr) {
    if(serial>64) return;
    Log("display_modes api=%u call=%u format=%u flags=%u capacity=%u output=%u list=%d hr=%08lx",
        api,serial,format,flags,capacity,count && (SUCCEEDED(hr) || hr==DXGI_ERROR_MORE_DATA)?*count:0,modes!=nullptr,hr);
    if(SUCCEEDED(hr) && count && modes) {
        UINT limit=(*count<capacity?*count:capacity);
        if(limit>256) limit=256;
        for(UINT i=0;i<limit;++i) Log("display_mode api=%u call=%u index=%u size=%ux%u refresh=%u/%u",
            api,serial,i,modes[i].Width,modes[i].Height,modes[i].RefreshRate.Numerator,modes[i].RefreshRate.Denominator);
    }
}
inline HRESULT STDMETHODCALLTYPE List(IDXGIOutput* output,DXGI_FORMAT format,UINT flags,UINT* count,DXGI_MODE_DESC* modes) {
    auto original=reinterpret_cast<ListFn>(Original(output,8)); if(!original) return E_UNEXPECTED;
    UINT capacity=count && modes?*count:0;
    HRESULT hr=runtime_mode::Enumerate<DXGI_MODE_DESC>([&](UINT* n,DXGI_MODE_DESC* m){return original(output,format,flags,n,m);},targetWidth,targetHeight,count,modes);DWORD saved=GetLastError();
    Record(0,++calls,format,flags,capacity,count,modes,hr);SetLastError(saved);return hr;
}
inline HRESULT STDMETHODCALLTYPE List1(IDXGIOutput1* output,DXGI_FORMAT format,UINT flags,UINT* count,DXGI_MODE_DESC1* modes) {
    auto original=reinterpret_cast<List1Fn>(Original(output,19)); if(!original) return E_UNEXPECTED;
    UINT capacity=count && modes?*count:0;
    HRESULT hr=runtime_mode::Enumerate<DXGI_MODE_DESC1>([&](UINT* n,DXGI_MODE_DESC1* m){return original(output,format,flags,n,m);},targetWidth,targetHeight,count,modes);DWORD saved=GetLastError();
    Record(1,++calls,format,flags,capacity,count,modes,hr);SetLastError(saved);return hr;
}
inline void Attach(IDXGIFactory* factory) {
    DWORD saved=GetLastError();
    InitOnceExecuteOnce(&configOnce,Config,nullptr,nullptr);
    // Observe existing output implementations; do not modify the game or OS mode list.
    for(UINT a=0;a<32;++a) {
        IDXGIAdapter* adapter=nullptr;
        if(FAILED(factory->EnumAdapters(a,&adapter))) break;
        for(UINT o=0;o<32;++o) {
            IDXGIOutput* output=nullptr;
            if(FAILED(adapter->EnumOutputs(o,&output))) break;
            Patch(output,8,reinterpret_cast<void*>(&List));
            IDXGIOutput1* output1=nullptr;
            if(SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output1)))) {
                Patch(output1,8,reinterpret_cast<void*>(&List));
                Patch(output1,19,reinterpret_cast<void*>(&List1));output1->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    SetLastError(saved);
}
}

