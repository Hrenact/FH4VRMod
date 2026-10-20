#pragma once
#include "frame_phase_state.h"
#include "frame_capture.h"
#include <algorithm>
namespace frame_phase {
// Preallocate a bounded center crop for every frame before publishing phase 1.
// No CPU readback or PNG encoding until every camera command has been stopped.
class Batch {
    struct Slot {
        capture::ComPtr<ID3D12Resource> image,source;
        capture::ComPtr<ID3D12CommandAllocator> allocator;
        capture::ComPtr<ID3D12GraphicsCommandList> list;
        bool ready=false;
    };
    struct Storage {
        Slot slots[Count];
        capture::ComPtr<ID3D12CommandQueue> queue;
        capture::ComPtr<ID3D12Fence> fence;
        D3D12_RESOURCE_DESC sourceDesc{};
        UINT64 submitted=0;
        bool poisoned=false;
    };
    std::unique_ptr<Storage> storage;
public:
    UINT x=0,y=0,width=0,height=0;
    ~Batch(){Drain();}
    HRESULT Prepare(ID3D12CommandQueue* queue,ID3D12Resource* source){
        if(storage || !queue || !source || queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return E_INVALIDARG;
        auto desc=source->GetDesc();
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || !desc.Width || desc.Width>8192 || !desc.Height || desc.Height>8192 ||
           desc.SampleDesc.Count!=1 || desc.MipLevels!=1 || desc.DepthOrArraySize!=1)return E_INVALIDARG;
        capture::ComPtr<ID3D12Device> device,other;
        HRESULT hr=queue->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr))return hr;
        hr=source->GetDevice(IID_PPV_ARGS(&other));if(FAILED(hr))return hr;
        capture::ComPtr<IUnknown> a,b;device.As(&a);other.As(&b);if(a.Get()!=b.Get())return E_INVALIDARG;
        std::unique_ptr<Storage> fresh(new(std::nothrow) Storage);if(!fresh)return E_OUTOFMEMORY;
        fresh->queue=queue;fresh->sourceDesc=desc;
        hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fresh->fence));if(FAILED(hr))return hr;
        width=std::min(static_cast<UINT>(desc.Width),1024u);height=std::min(desc.Height,1024u);
        x=(static_cast<UINT>(desc.Width)-width)/2;y=(desc.Height-height)/2;
        desc.Width=width;desc.Height=height;desc.Flags=D3D12_RESOURCE_FLAG_NONE;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(auto& slot:fresh->slots){
            hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&slot.image));if(FAILED(hr))return hr;
            hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator));if(FAILED(hr))return hr;
            hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list));if(FAILED(hr))return hr;
            hr=slot.list->Close();if(FAILED(hr))return hr;
        }
        storage=std::move(fresh);return S_OK;
    }
    HRESULT Copy(unsigned phase,ID3D12Resource* source){
        if(!storage || storage->poisoned || !source || phase<1 || phase>Count)return E_INVALIDARG;
        auto& slot=storage->slots[phase-1];if(slot.ready)return E_UNEXPECTED;
        auto desc=source->GetDesc();const auto& expected=storage->sourceDesc;
        if(desc.Width!=expected.Width || desc.Height!=expected.Height || desc.Format!=expected.Format || desc.SampleDesc.Count!=1)return DXGI_ERROR_INVALID_CALL;
        HRESULT hr=slot.allocator->Reset();if(FAILED(hr))return hr;
        hr=slot.list->Reset(slot.allocator.Get(),nullptr);if(FAILED(hr))return hr;
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        barriers[0].Transition.pResource=source;barriers[0].Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource=slot.image.Get();barriers[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        slot.list->ResourceBarrier(2,barriers);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=slot.image.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX box{x,y,0,x+width,y+height,1};slot.list->CopyTextureRegion(&to,0,0,0,&from,&box);
        for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        slot.list->ResourceBarrier(2,barriers);hr=slot.list->Close();if(FAILED(hr))return hr;
        slot.source=source;ID3D12CommandList* lists[]={slot.list.Get()};storage->queue->ExecuteCommandLists(1,lists);
        hr=storage->queue->Signal(storage->fence.Get(),++storage->submitted);
        if(FAILED(hr))storage->poisoned=true;else slot.ready=true;return hr;
    }
    ID3D12Resource* Image(unsigned phase)const{return storage && phase && phase<=Count && storage->slots[phase-1].ready?storage->slots[phase-1].image.Get():nullptr;}
    HRESULT Drain(){
        if(!storage)return S_OK;
        HRESULT hr=storage->poisoned?E_FAIL:S_OK;
        if(SUCCEEDED(hr)){
            HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!event)hr=HRESULT_FROM_WIN32(GetLastError());
            else {hr=storage->fence->SetEventOnCompletion(storage->submitted,event);
                if(SUCCEEDED(hr) && WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)hr=DXGI_ERROR_WAIT_TIMEOUT;
                CloseHandle(event);if(SUCCEEDED(hr) && storage->fence->GetCompletedValue()==UINT64_MAX)hr=DXGI_ERROR_DEVICE_REMOVED;}
        }
        if(FAILED(hr))storage.release();else storage.reset();return hr;
    }
};
}
