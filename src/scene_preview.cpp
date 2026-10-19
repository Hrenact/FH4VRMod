#define NOMINMAX
#include <windows.h>
#include <openvr.h>
#include "frame_capture.h"
#include "gpu_copy.h"
#include "projection_crop.h"
#include <cstdio>
using capture::ComPtr;
#define HR(x) do { HRESULT h=(x);if(FAILED(h)) {printf("FAIL line=%d hr=%08lx\n",__LINE__,h);return 2;} } while(0)
#define VR(x) do { auto e=(x);if(e!=vr::VROverlayError_None) {printf("OVERLAY FAIL line=%d error=%d\n",__LINE__,int(e));return 3;} } while(0)
struct VrSession {
    bool active=false;
    ~VrSession() {if(active) {if(vr::VRCompositor()) vr::VRCompositor()->ClearLastSubmittedFrame();vr::VR_Shutdown();}}
};
int wmain(int argc,wchar_t** argv) {
    if(argc!=3 && argc!=4) {puts("Usage: scene_preview stereo-sbs.png seconds OR mono.png seconds source_vertical_degrees");return 1;}
    const bool monoCalibrated=argc==4;
    const float sourceVertical=monoCalibrated?static_cast<float>(_wtof(argv[3]))*3.14159265359f/180.f:0;
    int seconds=_wtoi(argv[2]);if(seconds<1 || seconds>600) return 1;
    setvbuf(stdout,nullptr,_IONBF,0);
    HR(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
    struct ComScope {~ComScope(){CoUninitialize();}} comScope;
    // GPU objects outlive VR_Shutdown, including failure paths.
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Resource> texture,upload;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    VrSession session;
    vr::EVRInitError error=vr::VRInitError_None;
    auto system=vr::VR_Init(&error,vr::VRApplication_Scene);
    if(error!=vr::VRInitError_None || !system) {printf("VR_Init error=%d\n",error);return 3;}
    session.active=true;if(!vr::VRCompositor()) return 3;
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
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1,&barrier);HR(list->Close());
    HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    // On unknown GPU completion terminate the standalone process without unwinding in-flight resources.
    HRESULT signal=queue->Signal(fence.Get(),1);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(FAILED(signal) || !event || FAILED(fence->SetEventOnCompletion(1,event)) || WaitForSingleObject(event,5000)!=WAIT_OBJECT_0 || fence->GetCompletedValue()==UINT64_MAX) {puts("GPU upload completion failed");ExitProcess(5);}
    CloseHandle(event);
    if(width%2) return 4;
    uint32_t targetWidth=0,targetHeight=0;system->GetRecommendedRenderTargetSize(&targetWidth,&targetHeight);
    printf("SCENE source=%ux%u recommended_per_eye=%ux%u adapter=%d seconds=%d\n",width,height,targetWidth,targetHeight,adapterIndex,seconds);
    vr::VRTextureBounds_t bounds[2]={{0,0,.5f,1},{.5f,0,1,1}};
    for(int eye=0;eye<2;++eye) {
        float left=0,right=0,top=0,bottom=0;
        system->GetProjectionRaw(static_cast<vr::EVREye>(eye),&left,&right,&top,&bottom);
        auto eyePose=system->GetEyeToHeadTransform(static_cast<vr::EVREye>(eye));
        printf("EYE %d tangents=%f,%f,%f,%f offset=%f,%f,%f\n",eye,left,right,top,bottom,eyePose.m[0][3],eyePose.m[1][3],eyePose.m[2][3]);
        if(monoCalibrated) {
            ProjectionCrop crop{};
            if(!ComputeProjectionCrop(static_cast<float>(width)/height,sourceVertical,left,right,top,bottom,crop)) {
                puts("Source projection does not cover runtime eye frustum");return 10;
            }
            bounds[eye]={crop.u0,crop.v0,crop.u1,crop.v1};
            printf("MONO_ANGULAR eye=%d bounds=%.7f,%.7f,%.7f,%.7f source_vdeg=%.6f\n",eye,crop.u0,crop.v0,crop.u1,crop.v1,sourceVertical*180.f/3.14159265359f);
        }
    }
    // Static diagnostic: no claim of matched game projection or live head tracking.
    vr::D3D12TextureData_t data{texture.Get(),queue.Get(),0};
    vr::Texture_t image{&data,vr::TextureType_DirectX12,vr::ColorSpace_Gamma};
    unsigned pairs=0,focusSkips=0;
    auto start=GetTickCount64();
    while(GetTickCount64()-start<static_cast<ULONGLONG>(seconds)*1000) {
        vr::VREvent_t vrEvent{};
        while(system->PollNextEvent(&vrEvent,sizeof(vrEvent))) if(vrEvent.eventType==vr::VREvent_Quit) {puts("SteamVR quit requested");return 0;}
        vr::TrackedDevicePose_t pose{};
        auto wait=vr::VRCompositor()->WaitGetPoses(&pose,1,nullptr,0);
        if(wait==vr::VRCompositorError_DoNotHaveFocus) {++focusSkips;continue;}
        if(wait!=vr::VRCompositorError_None) {printf("WAIT error=%d\n",wait);return 7;}
        auto left=vr::VRCompositor()->Submit(vr::Eye_Left,&image,&bounds[0]);
        auto right=vr::VRCompositor()->Submit(vr::Eye_Right,&image,&bounds[1]);
        if(left!=vr::VRCompositorError_None || right!=vr::VRCompositorError_None) {printf("SUBMIT error=%d,%d\n",left,right);return 8;}
        ++pairs;
        vr::VRCompositor()->PostPresentHandoff();
    }
    vr::VRCompositor()->ClearLastSubmittedFrame();vr::VR_Shutdown();session.active=false;
    printf("COMPLETE pairs=%u focus_skips=%u\n",pairs,focusSkips);
    return pairs?0:9;
}

