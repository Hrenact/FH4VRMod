#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#include <atomic>
#ifdef FH4VR_UI_LAYER_CAPTURE
#include "ui_layer_capture.h"
#endif
#ifdef FH4VR_UI_PROJECTION_PROBE
#include "ui_projection_test.h"
#endif

static HMODULE module;
static SRWLOCK hooksLock=SRWLOCK_INIT,logLock=SRWLOCK_INIT;
struct Hook { void** slot; void* original; };
static Hook hooks[128]{};
static unsigned hookCount=0;
#ifdef FH4VR_EXPERIMENTAL_PRESENT_PROBE
static std::atomic<unsigned long long> presentCount{0};
#endif
static void Log(const char* format,...) {
    AcquireSRWLockExclusive(&logLock);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module,path,MAX_PATH);
    wchar_t* tail=wcsrchr(path,L'\\');
    if(tail) {
        wcscpy_s(tail+1,MAX_PATH-(tail+1-path),L"graphics-probe.log");
        FILE* file=_wfsopen(path,L"a",_SH_DENYWR);
        if(file) {
            fprintf(file,"pid=%lu tick=%llu ",GetCurrentProcessId(),GetTickCount64());
            va_list args;va_start(args,format);vfprintf(file,format,args);va_end(args);
            fputc('\n',file);fclose(file);
        }
    }
    ReleaseSRWLockExclusive(&logLock);
}
static void* Original(void* object,unsigned index) {
    DWORD saved=GetLastError();
    void** address=*static_cast<void***>(object)+index;
    void* result=nullptr;
    AcquireSRWLockShared(&hooksLock);
    for(unsigned i=0;i<hookCount;++i) if(hooks[i].slot==address) { result=hooks[i].original;break; }
    ReleaseSRWLockShared(&hooksLock);
    SetLastError(saved);
    return result;
}
static void Patch(void* object,unsigned index,void* replacement) {
    void** address=*static_cast<void***>(object)+index;
    AcquireSRWLockExclusive(&hooksLock);
    for(unsigned i=0;i<hookCount;++i) if(hooks[i].slot==address) { ReleaseSRWLockExclusive(&hooksLock);return; }
    if(hookCount==_countof(hooks)) { ReleaseSRWLockExclusive(&hooksLock);Log("hook_capacity");return; }
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(address,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect!=PAGE_READONLY && info.Protect!=PAGE_READWRITE && info.Protect!=PAGE_EXECUTE_READWRITE)) {
        ReleaseSRWLockExclusive(&hooksLock);Log("slot_unsupported index=%u",index);return;
    }
    DWORD old=0;bool changed=info.Protect==PAGE_READONLY;
    if(changed && !VirtualProtect(address,sizeof(void*),PAGE_READWRITE,&old)) {
        ReleaseSRWLockExclusive(&hooksLock);Log("slot_protect_failed index=%u",index);return;
    }
    void* previous=*address;
    hooks[hookCount++]={address,previous};
    void* observed=InterlockedCompareExchangePointer(address,replacement,previous);
    if(observed!=previous) --hookCount;
    DWORD ignored=0;bool restored=!changed || VirtualProtect(address,sizeof(void*),old,&ignored);
    ReleaseSRWLockExclusive(&hooksLock);
    Log("slot_patch index=%u slot=%p success=%d protection_restored=%d",index,address,observed==previous,restored);
}
static void Describe(IDXGISwapChain* chain,const char* event) {
    DWORD saved=GetLastError();
    DXGI_SWAP_CHAIN_DESC desc{};HRESULT hr=chain->GetDesc(&desc);
#ifdef FH4VR_UI_PROJECTION_PROBE
    if(SUCCEEDED(hr))ui_projection_test::dimensions.store((uint64_t(desc.BufferDesc.Width)<<32)|desc.BufferDesc.Height);
#endif
    UINT index=0; IDXGISwapChain3* chain3=nullptr;
    if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) { index=chain3->GetCurrentBackBufferIndex();chain3->Release(); }
    ID3D12Resource* buffer=nullptr;
    HRESULT bufferHr=chain->GetBuffer(index,IID_PPV_ARGS(&buffer));
    if(SUCCEEDED(bufferHr)) {
        auto resource=buffer->GetDesc();
        Log("%s chain=%p desc_hr=%08lx size=%ux%u format=%u count=%u effect=%u flags=%u index=%u resource=%llux%u resource_format=%u samples=%u",
            event,chain,hr,desc.BufferDesc.Width,desc.BufferDesc.Height,desc.BufferDesc.Format,desc.BufferCount,
            desc.SwapEffect,desc.Flags,index,resource.Width,resource.Height,resource.Format,resource.SampleDesc.Count);
        buffer->Release(); // Never retain a back buffer across ResizeBuffers.
    } else Log("%s chain=%p desc_hr=%08lx buffer_hr=%08lx",event,chain,hr,bufferHr);
    SetLastError(saved);
}
#ifdef FH4VR_EXPERIMENTAL_PRESENT_PROBE
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
static HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain,UINT sync,UINT flags) {
    auto original=reinterpret_cast<PresentFn>(Original(chain,8));
    if(!original) return E_UNEXPECTED;
    auto count=++presentCount;
    if(count==1 || count%600==0) Describe(chain,"present_before");
    HRESULT hr=original(chain,sync,flags);DWORD saved=GetLastError();
    if(count==1 || count%600==0 || FAILED(hr)) Log("present_return count=%llu chain=%p sync=%u flags=%u hr=%08lx",count,chain,sync,flags,hr);
    SetLastError(saved);return hr;
}
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
static HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* chain,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* params) {
    auto original=reinterpret_cast<Present1Fn>(Original(chain,22));if(!original) return E_UNEXPECTED;
    auto count=++presentCount;
    if(count==1 || count%600==0) Describe(chain,"present1_before");
    HRESULT hr=original(chain,sync,flags,params);DWORD saved=GetLastError();
    if(count==1 || count%600==0 || FAILED(hr)) Log("present1_return count=%llu chain=%p hr=%08lx",count,chain,hr);
    SetLastError(saved);return hr;
}
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
static HRESULT STDMETHODCALLTYPE Resize(IDXGISwapChain* chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
    auto original=reinterpret_cast<ResizeFn>(Original(chain,13));if(!original) return E_UNEXPECTED;
    HRESULT hr=original(chain,count,width,height,format,flags);DWORD saved=GetLastError();
    Log("resize_return chain=%p requested=%ux%u count=%u hr=%08lx",chain,width,height,count,hr);
    if(SUCCEEDED(hr)) Describe(chain,"resize_after");
    SetLastError(saved);return hr;
}
#endif
#ifdef FH4VR_UI_LAYER_CAPTURE
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
static void STDMETHODCALLTYPE ObserveUiExecute(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists) {
    auto original=reinterpret_cast<ExecuteFn>(Original(queue,10));
    if(!original)return;
    original(queue,count,lists);DWORD saved=GetLastError();
    ui_layer_capture::Submitted(queue,count,lists);SetLastError(saved);
}
#endif
static void AttachChain(IUnknown* device,IDXGISwapChain* chain,const char* event) {
    if(!chain) return;
    ID3D12CommandQueue* queue=nullptr;
    HRESULT hr=device?device->QueryInterface(IID_PPV_ARGS(&queue)):E_POINTER;
    if(SUCCEEDED(hr)) {
#ifdef FH4VR_UI_LAYER_CAPTURE
        Patch(queue,10,reinterpret_cast<void*>(&ObserveUiExecute));
        ui_layer_capture::queueHookReady=Original(queue,10)!=nullptr;
        Log("ui_capture_queue_hook ready=%u",ui_layer_capture::queueHookReady.load()?1u:0u);
#endif
        auto desc=queue->GetDesc();ID3D12Device* gpu=nullptr;
        HRESULT deviceHr=queue->GetDevice(IID_PPV_ARGS(&gpu));
        LUID luid{};if(SUCCEEDED(deviceHr)) { luid=gpu->GetAdapterLuid();gpu->Release(); }
        Log("%s chain=%p queue=%p type=%u device_hr=%08lx adapter=%08lx:%08lx",event,chain,queue,desc.Type,deviceHr,static_cast<DWORD>(luid.HighPart),luid.LowPart);
        queue->Release();
    } else { Log("%s non_d3d12 chain=%p queue_hr=%08lx",event,chain,hr);return; }
    Describe(chain,"created");
    // Shared swapchain-table interception caused a rapid reentry pattern in FH4.
    // Quarantined pending a representative reproduction; creation diagnostics
    // remain available without touching presentation or resize slots.
#ifdef FH4VR_EXPERIMENTAL_PRESENT_PROBE
    Patch(chain,8,reinterpret_cast<void*>(&Present));Patch(chain,13,reinterpret_cast<void*>(&Resize));
    IDXGISwapChain1* chain1=nullptr;
    if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain1)))) {
        Patch(chain1,8,reinterpret_cast<void*>(&Present));Patch(chain1,13,reinterpret_cast<void*>(&Resize));
        Patch(chain1,22,reinterpret_cast<void*>(&Present1));chain1->Release();
    }
#endif
}
#ifdef FH4VR_SWAPCHAIN_WRAPPER
#include "swapchain_wrapper.h"
#endif
using CreateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
static HRESULT STDMETHODCALLTYPE Create(IDXGIFactory* factory,IUnknown* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** output) {
    auto original=reinterpret_cast<CreateFn>(Original(factory,10));if(!original) return E_UNEXPECTED;
    HRESULT hr=original(factory,device,desc,output);DWORD saved=GetLastError();
    Log("CreateSwapChain hr=%08lx",hr);if(SUCCEEDED(hr) && output) AttachChain(device,*output,"legacy_queue");
#ifdef FH4VR_SWAPCHAIN_WRAPPER
    if(SUCCEEDED(hr)) WrapChain(output,device);
#endif
    SetLastError(saved);return hr;
}
using HwndFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
static HRESULT STDMETHODCALLTYPE Hwnd(IDXGIFactory2* f,IUnknown* d,HWND w,const DXGI_SWAP_CHAIN_DESC1* desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrictOutput,IDXGISwapChain1** output) {
    auto original=reinterpret_cast<HwndFn>(Original(f,15));if(!original) return E_UNEXPECTED;
    HRESULT hr=original(f,d,w,desc,full,restrictOutput,output);DWORD saved=GetLastError();
    Log("CreateSwapChainForHwnd hr=%08lx hwnd=%p",hr,w);if(SUCCEEDED(hr) && output) AttachChain(d,*output,"hwnd_queue");
#ifdef FH4VR_SWAPCHAIN_WRAPPER
    if(SUCCEEDED(hr)) WrapChain(output,d);
#endif
    SetLastError(saved);return hr;
}
using CoreFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**);
static HRESULT STDMETHODCALLTYPE Core(IDXGIFactory2* f,IUnknown* d,IUnknown* window,const DXGI_SWAP_CHAIN_DESC1* desc,IDXGIOutput* restrictOutput,IDXGISwapChain1** output) {
    auto original=reinterpret_cast<CoreFn>(Original(f,16));if(!original) return E_UNEXPECTED;
    HRESULT hr=original(f,d,window,desc,restrictOutput,output);DWORD saved=GetLastError();
    Log("CreateSwapChainForCoreWindow hr=%08lx",hr);if(SUCCEEDED(hr) && output) AttachChain(d,*output,"core_queue");
    SetLastError(saved);return hr;
}
using CompositionFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**);
static HRESULT STDMETHODCALLTYPE Composition(IDXGIFactory2* f,IUnknown* d,const DXGI_SWAP_CHAIN_DESC1* desc,IDXGIOutput* restrictOutput,IDXGISwapChain1** output) {
    auto original=reinterpret_cast<CompositionFn>(Original(f,24));if(!original) return E_UNEXPECTED;
    HRESULT hr=original(f,d,desc,restrictOutput,output);DWORD saved=GetLastError();
    Log("CreateSwapChainForComposition hr=%08lx",hr);if(SUCCEEDED(hr) && output) AttachChain(d,*output,"composition_queue");
    SetLastError(saved);return hr;
}
#include "display_mode_probe.h"
extern "C" void SetGraphicsModule(HMODULE value) { module=value; }
extern "C" void ObserveFactory(IUnknown* object) {
    if(!object) return;
    // Patched COM tables may outlive the importing client. Never unload callbacks.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&ObserveFactory),&pinned)) return;
    IDXGIFactory* factory=nullptr;
    if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&factory)))) {
        display_mode_probe::Attach(factory);
        Patch(factory,10,reinterpret_cast<void*>(&Create));factory->Release();
    }
    IDXGIFactory2* factory2=nullptr;
    if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&factory2)))) {
        Patch(factory2,10,reinterpret_cast<void*>(&Create));Patch(factory2,15,reinterpret_cast<void*>(&Hwnd));
        Patch(factory2,16,reinterpret_cast<void*>(&Core));Patch(factory2,24,reinterpret_cast<void*>(&Composition));factory2->Release();
    }
}
