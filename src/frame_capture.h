#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <vector>
#include <memory>
#include <cstring>
namespace capture {
using Microsoft::WRL::ComPtr;
struct Image { UINT width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;std::vector<BYTE> rgba;UINT64 alphaZero=0,alphaOpaque=0,alphaZeroTop=0; };
struct Job {
    ComPtr<ID3D12Resource> source,readback;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12CommandQueue> queue;
    HANDLE event=nullptr;
    ~Job() { if(event) CloseHandle(event); }
};
inline HRESULT Readback(ID3D12CommandQueue* queue,ID3D12Resource* source,Image& image,bool& retained,D3D12_RESOURCE_STATES sourceState=D3D12_RESOURCE_STATE_PRESENT) {
    retained=false;
    if(!queue || !source || queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) return E_INVALIDARG;
    auto desc=source->GetDesc();
    bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    bool packed=desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
    if((!rgba && !bgra && !packed) || desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1 || desc.MipLevels!=1 ||
       !desc.Width || !desc.Height || desc.Width>8192 || desc.Height>8192) return E_INVALIDARG;
    ComPtr<ID3D12Device> device,sourceDevice;
    HRESULT hr=queue->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr)) return hr;
    hr=source->GetDevice(IID_PPV_ARGS(&sourceDevice));if(FAILED(hr)) return hr;
    ComPtr<IUnknown> qa,sa;device.As(&qa);sourceDevice.As(&sa);if(qa.Get()!=sa.Get()) return E_INVALIDARG;
    std::unique_ptr<Job> job(new(std::nothrow) Job);if(!job) return E_OUTOFMEMORY;
    job->source=source;job->queue=queue;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&job->readback));if(FAILED(hr)) return hr;
    hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&job->allocator));if(FAILED(hr)) return hr;
    hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,job->allocator.Get(),nullptr,IID_PPV_ARGS(&job->list));if(FAILED(hr)) return hr;
    hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&job->fence));if(FAILED(hr)) return hr;
    job->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!job->event) return HRESULT_FROM_WIN32(GetLastError());
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=source;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=sourceState;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    job->list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=job->readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    job->list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=sourceState;
    job->list->ResourceBarrier(1,&barrier);
    hr=job->list->Close();if(FAILED(hr)) return hr;
    ID3D12CommandList* lists[]={job->list.Get()};queue->ExecuteCommandLists(1,lists);
    hr=queue->Signal(job->fence.Get(),1);
    if(SUCCEEDED(hr)) hr=job->fence->SetEventOnCompletion(1,job->event);
    if(SUCCEEDED(hr) && WaitForSingleObject(job->event,5000)!=WAIT_OBJECT_0) hr=DXGI_ERROR_WAIT_TIMEOUT;
    if(SUCCEEDED(hr) && job->fence->GetCompletedValue()==UINT64_MAX) hr=DXGI_ERROR_DEVICE_REMOVED;
    if(FAILED(hr)) {
        // GPU completion is unknown. Retain *all* submitted resources until process
        // exit rather than freeing in-flight command memory. Caller disables capture.
        retained=true;job.release();return hr;
    }
    image.width=static_cast<UINT>(desc.Width);image.height=desc.Height;image.format=desc.Format;
    try { image.rgba.resize(static_cast<size_t>(image.width)*image.height*4); }
    catch(...) { return E_OUTOFMEMORY; }
    void* mapped=nullptr;D3D12_RANGE readRange{0,static_cast<SIZE_T>(total)};
    hr=job->readback->Map(0,&readRange,&mapped);if(FAILED(hr)) return hr;
    image.alphaZero=image.alphaOpaque=image.alphaZeroTop=0;
    for(UINT y=0;y<image.height;++y) for(UINT x=0;x<image.width;++x) {
        const BYTE* src=static_cast<BYTE*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch+x*4;
        BYTE* dst=image.rgba.data()+(static_cast<size_t>(y)*image.width+x)*4;
        BYTE alpha=src[3];
        if(packed) {
            UINT pixel;memcpy(&pixel,src,4);
            alpha=static_cast<BYTE>(((pixel>>30)&3)*85);
            for(UINT c=0;c<3;++c) dst[c]=static_cast<BYTE>((((pixel>>(c*10))&1023)*255+511)/1023);
        } else { dst[0]=src[bgra?2:0];dst[1]=src[1];dst[2]=src[bgra?0:2]; }
        if(alpha==0) {++image.alphaZero;if(y<image.height/2) ++image.alphaZeroTop;}
        if(alpha==255) ++image.alphaOpaque;
        dst[3]=255; // Diagnostic screenshot is opaque; no HDR tone mapping.
    }
    D3D12_RANGE written{0,0};job->readback->Unmap(0,&written);return S_OK;
}
inline HRESULT SavePng(const wchar_t* path,const Image& image) {
    std::vector<BYTE> bgra;
    try { bgra=image.rgba; } catch(...) { return E_OUTOFMEMORY; }
    for(size_t i=0;i<bgra.size();i+=4) { BYTE red=bgra[i];bgra[i]=bgra[i+2];bgra[i+2]=red; }
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init) && init!=RPC_E_CHANGED_MODE) return init;
    HRESULT result=[&]() -> HRESULT {
        ComPtr<IWICImagingFactory> factory;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));if(FAILED(hr)) return hr;
        ComPtr<IWICStream> stream;hr=factory->CreateStream(&stream);if(FAILED(hr)) return hr;
        hr=stream->InitializeFromFilename(path,GENERIC_WRITE);if(FAILED(hr)) return hr;
        ComPtr<IWICBitmapEncoder> encoder;hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(FAILED(hr)) return hr;
        hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);if(FAILED(hr)) return hr;
        ComPtr<IWICBitmapFrameEncode> frame;hr=encoder->CreateNewFrame(&frame,nullptr);if(FAILED(hr)) return hr;
        hr=frame->Initialize(nullptr);if(FAILED(hr)) return hr;
        hr=frame->SetSize(image.width,image.height);if(FAILED(hr)) return hr;
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;hr=frame->SetPixelFormat(&format);
        if(FAILED(hr) || format!=GUID_WICPixelFormat32bppBGRA) return E_FAIL;
        hr=frame->WritePixels(image.height,image.width*4,static_cast<UINT>(bgra.size()),bgra.data());if(FAILED(hr)) return hr;
        hr=frame->Commit();if(FAILED(hr)) return hr;return encoder->Commit();
    }();
    if(SUCCEEDED(init)) CoUninitialize();return result;
}
}
