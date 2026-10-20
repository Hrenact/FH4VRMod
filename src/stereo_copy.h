#pragma once
#include "frame_capture.h"
#include <algorithm>
#include <cstdint>

namespace gpu_copy {
// Three complete SBS pairs, each with independent command memory for each eye.
// All copies and consumer work use one ordered DIRECT queue. No render-path waits.
class StereoRing {
    struct Eye {
        capture::ComPtr<ID3D12Resource> source;
        capture::ComPtr<ID3D12CommandAllocator> allocator;
        capture::ComPtr<ID3D12GraphicsCommandList> list;
        UINT64 value=0;
    };
    struct Slot {
        capture::ComPtr<ID3D12Resource> texture;
        Eye eyes[2];
        UINT64 retired=0,generation=0;
        unsigned mask=0;
    };
    struct State {
        capture::ComPtr<ID3D12CommandQueue> queue;
        capture::ComPtr<ID3D12Fence> fence;
        D3D12_RESOURCE_DESC desc{};
        Slot slots[3];
        UINT64 next=0,lastStarted=0;
        int active=-1,latest=-1;
        unsigned cursor=0;
        bool poisoned=false,needsSeal=false;
    };
    std::unique_ptr<State> state;
    static constexpr auto resting=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    HRESULT Initialize(ID3D12CommandQueue* queue,ID3D12Resource* source){
        capture::ComPtr<ID3D12Device> device,other;
        HRESULT hr=queue->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr))return hr;
        hr=source->GetDevice(IID_PPV_ARGS(&other));if(FAILED(hr))return hr;
        capture::ComPtr<IUnknown> a,b;device.As(&a);other.As(&b);if(a.Get()!=b.Get())return E_INVALIDARG;
        std::unique_ptr<State> fresh(new(std::nothrow) State);if(!fresh)return E_OUTOFMEMORY;
        fresh->queue=queue;fresh->desc=source->GetDesc();
        hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fresh->fence));if(FAILED(hr))return hr;
        auto desc=fresh->desc;desc.Width*=2;desc.Flags=D3D12_RESOURCE_FLAG_NONE;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(auto& slot:fresh->slots){
            hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,resting,nullptr,IID_PPV_ARGS(&slot.texture));if(FAILED(hr))return hr;
            for(auto& eye:slot.eyes){
                hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&eye.allocator));if(FAILED(hr))return hr;
                hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,eye.allocator.Get(),nullptr,IID_PPV_ARGS(&eye.list));if(FAILED(hr))return hr;
                hr=eye.list->Close();if(FAILED(hr))return hr;
            }
        }
        state=std::move(fresh);return S_OK;
    }
public:
    UINT64 submitted=0,completed=0,skipped=0,droppedPairs=0,wrongGeneration=0;
    bool retained=false;
    ~StereoRing(){Drain();}
    void DiscardPair(){if(state && state->active>=0){state->active=-1;++droppedPairs;}}
    HRESULT Poll(){
        if(!state)return S_OK;if(state->poisoned)return E_FAIL;
        const UINT64 done=state->fence->GetCompletedValue();if(done==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
        for(auto& slot:state->slots)for(auto& eye:slot.eyes)if(eye.source && eye.value<=done){eye.source.Reset();++completed;}
        return S_OK;
    }
    ID3D12Resource* Latest() const {return state && state->latest>=0?state->slots[state->latest].texture.Get():nullptr;}
    bool StereoReady(UINT64 generation=0) const {
        if(!state || state->latest<0)return false;
        const auto& slot=state->slots[state->latest];
        return slot.mask==3 && (!generation || slot.generation==generation);
    }
    HRESULT SealLatest(){
        if(!state || state->latest<0 || !state->needsSeal || state->poisoned)return E_UNEXPECTED;
        auto& slot=state->slots[state->latest];slot.retired=++state->next;
        const HRESULT hr=state->queue->Signal(state->fence.Get(),slot.retired);
        if(FAILED(hr))state->poisoned=true;
        state->needsSeal=false;return hr;
    }
    HRESULT Copy(ID3D12CommandQueue* queue,ID3D12Resource* source,int eyeIndex,UINT64 generation){
        if(retained || !queue || !source || !generation || eyeIndex<0 || eyeIndex>1 || queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return E_INVALIDARG;
        const auto desc=source->GetDesc();
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1 ||
           desc.MipLevels!=1 || !desc.Width || desc.Width>4096 || !desc.Height || desc.Height>8192)return E_INVALIDARG;
        if(state && (state->queue.Get()!=queue || state->desc.Width!=desc.Width || state->desc.Height!=desc.Height || state->desc.Format!=desc.Format))return DXGI_ERROR_INVALID_CALL;
        if(!state){HRESULT hr=Initialize(queue,source);if(FAILED(hr))return hr;}
        HRESULT hr=Poll();if(FAILED(hr))return hr;
        if(state->needsSeal)return E_UNEXPECTED; // Never recycle a texture before runtime consumption is fenced.
        if(eyeIndex==0){
            if(generation<=state->lastStarted){++skipped;++wrongGeneration;return S_FALSE;}
            DiscardPair();state->lastStarted=generation;
            const auto done=state->fence->GetCompletedValue();if(done==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
            for(unsigned offset=0;offset<3;++offset){
                const auto index=(state->cursor+offset)%3;auto& candidate=state->slots[index];
                const auto lastUse=std::max(candidate.retired,std::max(candidate.eyes[0].value,candidate.eyes[1].value));
                if(lastUse<=done){state->active=static_cast<int>(index);state->cursor=(index+1)%3;
                    // GPU progress may have advanced since Poll's earlier read.
                    for(auto& oldEye:candidate.eyes)if(oldEye.source){oldEye.source.Reset();++completed;}
                    candidate.mask=0;candidate.generation=generation;
                    if(state->latest==state->active)state->latest=-1;
                    break;
                }
            }
            if(state->active<0){++skipped;++droppedPairs;return S_FALSE;}
        } else if(state->active<0 || state->slots[state->active].generation!=generation || state->slots[state->active].mask!=1){
            ++skipped;++wrongGeneration;DiscardPair();return S_FALSE;
        }
        auto& slot=state->slots[state->active];auto& eye=slot.eyes[eyeIndex];
        // Both allocators were available when the pair was reserved. The right
        // allocator is independent of the in-flight left copy, so it need not wait.
        hr=eye.allocator->Reset();if(FAILED(hr)){DiscardPair();return hr;}
        hr=eye.list->Reset(eye.allocator.Get(),nullptr);if(FAILED(hr)){DiscardPair();return hr;}
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for(auto& barrier:barriers){barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        barriers[0].Transition.pResource=source;barriers[0].Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource=slot.texture.Get();barriers[1].Transition.StateBefore=resting;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        eye.list->ResourceBarrier(2,barriers);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=slot.texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        eye.list->CopyTextureRegion(&to,static_cast<UINT>(desc.Width)*eyeIndex,0,0,&from,nullptr);
        for(auto& barrier:barriers)std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        eye.list->ResourceBarrier(2,barriers);hr=eye.list->Close();if(FAILED(hr)){DiscardPair();return hr;}
        eye.source=source;eye.value=++state->next;
        ID3D12CommandList* lists[]={eye.list.Get()};queue->ExecuteCommandLists(1,lists);
        hr=queue->Signal(state->fence.Get(),eye.value);if(FAILED(hr)){state->poisoned=true;return hr;}
        slot.mask|=1u<<eyeIndex;++submitted;
        if(eyeIndex==1){state->latest=state->active;state->active=-1;state->needsSeal=true;}
        return S_OK;
    }
    HRESULT Drain(){
        if(!state)return S_OK;
        HRESULT hr=(state->poisoned || state->needsSeal)?E_FAIL:S_OK;
        if(SUCCEEDED(hr)){
            HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!event)hr=HRESULT_FROM_WIN32(GetLastError());
            else {
                hr=state->fence->SetEventOnCompletion(state->next,event);
                if(SUCCEEDED(hr) && WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)hr=DXGI_ERROR_WAIT_TIMEOUT;
                CloseHandle(event);if(SUCCEEDED(hr))hr=Poll();
            }
        }
        if(FAILED(hr)){retained=true;state.release();}else state.reset();
        return hr;
    }
};
} // namespace gpu_copy
