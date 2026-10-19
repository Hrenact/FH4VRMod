#pragma once
#include "ui_layer_capture.h"
namespace ui_isolated_capture {
inline std::mutex mutex;
inline bool disabled=false;
struct Target {
    capture::ComPtr<ID3D12Resource> texture;
    capture::ComPtr<ID3D12DescriptorHeap> heap;
};
// True means draw ran exactly once. False means caller must run its normal draw.
// Caller verifies one RTV, no DSV, and a currently recording direct command list.
template<class Draw> bool Try(ID3D12GraphicsCommandList* list,ID3D12Resource* source,
    D3D12_CPU_DESCRIPTOR_HANDLE restoreRtv,Draw draw) {
    if(!ui_layer_capture::remaining.load())return false;
    std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);
    if(!guard.owns_lock() || disabled || !ui_layer_capture::remaining.load())return false;
    if(!list || !source || !restoreRtv.ptr || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    auto desc=source->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1 || !desc.Width || !desc.Height || desc.Width>4096 || desc.Height>4096)return false;
    capture::ComPtr<ID3D12Device> device,other;
    auto hr=source->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr))return false;
    hr=list->GetDevice(IID_PPV_ARGS(&other));if(FAILED(hr)||device.Get()!=other.Get())return false;
    std::unique_ptr<Target> target(new(std::nothrow) Target);if(!target)return false;
    desc.MipLevels=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;
    hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&target->texture));
    if(FAILED(hr)){ui_layer_capture::Log("isolate_create_failed",0,hr);return false;}
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    hr=device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&target->heap));if(FAILED(hr))return false;
    auto rtv=target->heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target->texture.Get(),nullptr,rtv);
    list->ClearRenderTargetView(rtv,clear.Color,0,nullptr);
    list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    draw();
    list->OMSetRenderTargets(1,&restoreRtv,FALSE,nullptr);
    hr=ui_layer_capture::Record(list,target->texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,false,target->heap.Get());
    ui_layer_capture::remaining=0;
    ui_layer_capture::Log("isolated_draw",0,hr,static_cast<UINT>(desc.Width),desc.Height);
    if(hr!=S_OK){
        // Commands reference these allocations even when readback setup failed.
        // Retain until process exit and forbid further isolation attempts.
        disabled=true;target.release();
    }
    return true;
}
}
