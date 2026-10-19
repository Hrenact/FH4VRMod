#define NOMINMAX
#include <windows.h>
#include <openvr.h>
#include "frame_capture.h"
#include "gpu_copy.h"
#include <cstdio>
using capture::ComPtr;
#define HR(x) do { HRESULT h=(x);if(FAILED(h)) {printf("FAIL line=%d hr=%08lx\n",__LINE__,h);return 2;} } while(0)
#define VR(x) do { auto e=(x);if(e!=vr::VROverlayError_None) {printf("OVERLAY FAIL line=%d error=%d\n",__LINE__,int(e));return 3;} } while(0)
struct VrSession {
    bool active=false;vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid;
    ~VrSession() { if(active) { if(overlay!=vr::k_ulOverlayHandleInvalid) {vr::VROverlay()->HideOverlay(overlay);vr::VROverlay()->ClearOverlayTexture(overlay);vr::VROverlay()->DestroyOverlay(overlay);} vr::VR_Shutdown(); } }
};
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) {puts("Usage: overlay_preview image.png seconds");return 1;}
    int seconds=_wtoi(argv[2]);if(seconds<1 || seconds>600) return 1;
    setvbuf(stdout,nullptr,_IONBF,0);
    HR(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
    struct ComScope {~ComScope(){CoUninitialize();}} comScope;
    // GPU objects outlive VR_Shutdown, including failure paths.
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Resource> texture,upload;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    gpu_copy::Ring ring{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,1};
    VrSession session;
    vr::EVRInitError error=vr::VRInitError_None;
    auto system=vr::VR_Init(&error,vr::VRApplication_Overlay);
    if(error!=vr::VRInitError_None || !system) {printf("VR_Init error=%d\n",error);return 3;}
    session.active=true;if(!vr::VROverlay()) return 3;
    int32_t adapterIndex=-1;system->GetDXGIOutputInfo(&adapterIndex);if(adapterIndex<0) return 4;
    ComPtr<IDXGIFactory1> factory;HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;HR(factory->EnumAdapters1(adapterIndex,&adapter));
    HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<IWICImagingFactory> wic;HR(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmapDecoder> decoder;HR(wic->CreateDecoderFromFilename(argv[1],nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame;HR(decoder->GetFrame(0,&frame));
    UINT width=0,height=0;HR(frame->GetSize(&width,&height));if(!width || !height || width>8192 || height>8192) return 4;
    ComPtr<IWICFormatConverter> converter;HR(wic->CreateFormatConverter(&converter));
    HR(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    std::vector<BYTE> pixels(static_cast<size_t>(width)*height*4);HR(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()));
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    void* mapped=nullptr;D3D12_RANGE empty{0,0};HR(upload->Map(0,&empty,&mapped));
    for(UINT y=0;y<height;++y) memcpy(static_cast<BYTE*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,pixels.data()+static_cast<size_t>(y)*width*4,width*4);
    upload->Unmap(0,nullptr);
    HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    HR(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=texture.Get();
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1,&barrier);HR(list->Close());
    HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    // On unknown GPU completion terminate the standalone process without unwinding in-flight resources.
    HRESULT signal=queue->Signal(fence.Get(),1);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(FAILED(signal) || !event || FAILED(fence->SetEventOnCompletion(1,event)) || WaitForSingleObject(event,5000)!=WAIT_OBJECT_0 || fence->GetCompletedValue()==UINT64_MAX) {puts("GPU upload completion failed");ExitProcess(5);}
    CloseHandle(event);
    VR(vr::VROverlay()->CreateOverlay("fh4vr.d3d12.preview","FH4VR D3D12 Preview",&session.overlay));
    VR(vr::VROverlay()->SetOverlayWidthInMeters(session.overlay,1.6f));
    vr::HmdMatrix34_t transform{};transform.m[0][0]=transform.m[1][1]=transform.m[2][2]=1;transform.m[2][3]=-2;
    VR(vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(session.overlay,vr::k_unTrackedDeviceIndex_Hmd,&transform));
    HR(ring.Copy(queue.Get(),texture.Get(),0));
    // Ensure the first copy completes; then initialize the second half before showing.
    while(ring.Poll()==S_OK && ring.completed<ring.submitted) Sleep(1);
    HR(ring.Copy(queue.Get(),texture.Get(),1));
    VR(vr::VROverlay()->SetOverlayFlag(session.overlay,vr::VROverlayFlags_SideBySide_Parallel,true));
    vr::D3D12TextureData_t data{ring.Latest(),queue.Get(),0};vr::Texture_t image{&data,vr::TextureType_DirectX12,vr::ColorSpace_Gamma};
    VR(vr::VROverlay()->SetOverlayTexture(session.overlay,&image));
    HR(ring.SealLatest());
    VR(vr::VROverlay()->ShowOverlay(session.overlay));
    printf("READY D3D12 overlay size=%ux%u adapter=%d seconds=%d visible=%d\n",width,height,adapterIndex,seconds,vr::VROverlay()->IsOverlayVisible(session.overlay));
    uint32_t runtimeWidth=0,runtimeHeight=0,flags=0;float aspect=0,meters=0;vr::VRTextureBounds_t bounds{};
    VR(vr::VROverlay()->GetOverlayTextureSize(session.overlay,&runtimeWidth,&runtimeHeight));
    VR(vr::VROverlay()->GetOverlayFlags(session.overlay,&flags));
    VR(vr::VROverlay()->GetOverlayTexelAspect(session.overlay,&aspect));
    VR(vr::VROverlay()->GetOverlayWidthInMeters(session.overlay,&meters));
    VR(vr::VROverlay()->GetOverlayTextureBounds(session.overlay,&bounds));
    printf("LAYOUT gpu=%llux%u runtime=%ux%u flags=%u aspect=%f meters=%f bounds=%f,%f,%f,%f\n",
        ring.Latest()->GetDesc().Width,ring.Latest()->GetDesc().Height,runtimeWidth,runtimeHeight,flags,aspect,meters,bounds.uMin,bounds.vMin,bounds.uMax,bounds.vMax);
    auto start=GetTickCount64();while(GetTickCount64()-start<static_cast<ULONGLONG>(seconds)*1000) {
        vr::VREvent_t e{};while(system->PollNextEvent(&e,sizeof(e))) if(e.eventType==vr::VREvent_Quit) {puts("SteamVR quit");return 0;}
        HRESULT copied=ring.Copy(queue.Get(),texture.Get(),static_cast<int>(ring.submitted%2));HR(copied);
        if(copied==S_OK) {data.m_pResource=ring.Latest();VR(vr::VROverlay()->SetOverlayTexture(session.overlay,&image));HR(ring.SealLatest());}
        Sleep(5);
    }
    vr::VROverlay()->HideOverlay(session.overlay);vr::VROverlay()->ClearOverlayTexture(session.overlay);
    vr::VROverlay()->DestroyOverlay(session.overlay);session.overlay=vr::k_ulOverlayHandleInvalid;
    vr::VR_Shutdown();session.active=false;
    capture::Image proof;bool retained=false;
    HR(capture::Readback(queue.Get(),ring.Latest(),proof,retained,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    HR(capture::SavePng(L"F:\\FH4VRMod\\reports\\stereo-layout-proof.png",proof));
    HR(ring.Drain());
    printf("COMPLETE ring submitted=%llu completed=%llu skipped=%llu retained=%d\n",ring.submitted,ring.completed,ring.skipped,ring.retained);
    return ring.submitted==ring.completed && !ring.retained?0:6;
}
