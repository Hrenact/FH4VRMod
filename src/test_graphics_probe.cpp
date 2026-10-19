#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
#define CHECK(call) do { HRESULT result=(call); if(FAILED(result)) { printf("FAIL line=%d hr=%08lx\n",__LINE__,result);return 1; } } while(0)
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    HMODULE proxy=LoadLibraryW(argv[1]);if(!proxy) return 3;
    using FactoryFn=HRESULT(WINAPI*)(UINT,REFIID,void**);
    auto create=reinterpret_cast<FactoryFn>(GetProcAddress(proxy,"CreateDXGIFactory2"));if(!create) return 4;
    ComPtr<IDXGIFactory2> factory;CHECK(create(0,IID_PPV_ARGS(&factory)));
    ComPtr<IUnknown> identity1,identity2;CHECK(factory.As(&identity1));
    ComPtr<IDXGIFactory> base;CHECK(factory.As(&base));CHECK(base.As(&identity2));
    if(identity1.Get()!=identity2.Get()) return 5;
    ComPtr<ID3D12Device> device;CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;CHECK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"FH4VRGraphicsFixture";
    if(!RegisterClassW(&wc)) return 6;
    HWND window=CreateWindowW(wc.lpszClassName,L"Hidden graphics fixture",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window) return 7;
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=640;desc.Height=480;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    DXGI_SWAP_CHAIN_DESC1 compositionDesc=desc;compositionDesc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    ComPtr<IDXGISwapChain1> nativeComposition;
    CHECK(factory->CreateSwapChainForComposition(queue.Get(),&compositionDesc,nullptr,&nativeComposition));
    void** nativeTable=*reinterpret_cast<void***>(nativeComposition.Get());
    void* originalSlots[3]={nativeTable[8],nativeTable[13],nativeTable[22]};
    ComPtr<IDXGISwapChain1> chain;CHECK(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&chain));
    if(nativeTable[8]!=originalSlots[0] || nativeTable[13]!=originalSlots[1] || nativeTable[22]!=originalSlots[2]) return 11;
    ComPtr<IDXGISwapChain3> chain3;CHECK(chain.As(&chain3));
    ComPtr<IUnknown> chainIdentity1,chainIdentity2;CHECK(chain.As(&chainIdentity1));CHECK(chain3.As(&chainIdentity2));
    if(chainIdentity1.Get()!=chainIdentity2.Get()) return 12;
    chainIdentity1.Reset();chainIdentity2.Reset();
    ULONG refs=chain->AddRef();if(chain->Release()!=refs-1) return 13;
    for(unsigned i=0;i<3;++i) { CHECK(chain->Present(0,0)); DXGI_PRESENT_PARAMETERS pp{};CHECK(chain->Present1(0,0,&pp)); }
    ComPtr<ID3D12Fence> fence;CHECK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    CHECK(queue->Signal(fence.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event) return 8;
    CHECK(fence->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) return 9;CloseHandle(event);
    CHECK(chain->ResizeBuffers(2,800,600,DXGI_FORMAT_UNKNOWN,0));
    ComPtr<ID3D12Resource> buffer;CHECK(chain->GetBuffer(chain3->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer)));
    auto rd=buffer->GetDesc();if(rd.Width!=800 || rd.Height!=600) return 10;buffer.Reset();
    UINT masks[2]={1,1};IUnknown* queues[2]={queue.Get(),queue.Get()};
    CHECK(chain3->ResizeBuffers1(2,1024,768,DXGI_FORMAT_UNKNOWN,0,masks,queues));
    CHECK(chain3->GetBuffer(chain3->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer)));
    rd=buffer->GetDesc();if(rd.Width!=1024 || rd.Height!=768) return 14;buffer.Reset();
    chain3.Reset();chain.Reset();
    DXGI_SWAP_CHAIN_DESC legacy{};legacy.BufferDesc.Width=640;legacy.BufferDesc.Height=480;
    legacy.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;legacy.SampleDesc.Count=1;
    legacy.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;legacy.BufferCount=2;legacy.OutputWindow=window;
    legacy.Windowed=TRUE;legacy.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain> legacyChain;CHECK(factory->CreateSwapChain(queue.Get(),&legacy,&legacyChain));
    CHECK(legacyChain->Present(0,0));legacyChain.Reset();DestroyWindow(window);
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    CHECK(factory->CreateSwapChainForComposition(queue.Get(),&desc,nullptr,&chain));
    chain.Reset();
    puts("PASS: simultaneous composition/HWND and legacy chains; native presentation slots unchanged; COM identity/refcounts; Present/Present1 and both resize APIs.");
    return 0;
}
