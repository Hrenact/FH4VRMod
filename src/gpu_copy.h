#pragma once
#include "frame_capture.h"
namespace gpu_copy {
using capture::ComPtr;
// Single presentation thread; callers must drain before resizing or changing queues.
class Ring {
    struct Slot {
        ComPtr<ID3D12Resource> texture,source;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        UINT64 value=0;
        unsigned eyes=0;
    };
    struct State {
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12Fence> fence;
        Slot slots[3];
        D3D12_RESOURCE_DESC desc{};
        UINT64 next=0;
        unsigned cursor=0;
        bool poisoned=false;
        bool stereo=false;
    };
    std::unique_ptr<State> state;
    D3D12_RESOURCE_STATES restingState;
    unsigned slotCount;
public:
    explicit Ring(D3D12_RESOURCE_STATES resting=D3D12_RESOURCE_STATE_COMMON,unsigned count=3):restingState(resting),slotCount(count>=1 && count<=3?count:3) {}
    UINT64 submitted=0,completed=0,skipped=0;
    bool retained=false;
    ~Ring() { Drain(); }
    HRESULT Poll() {
        if(!state) return S_OK;
        if(state->poisoned) return E_FAIL;
        UINT64 done=state->fence->GetCompletedValue();
        if(done==UINT64_MAX) return DXGI_ERROR_DEVICE_REMOVED;
        for(auto& slot:state->slots) if(slot.source && slot.value<=done) {
            slot.source.Reset();++completed;
        }
        return S_OK;
    }
    HRESULT Drain() {
        if(!state) return S_OK;
        HRESULT hr=S_OK;
        if(state->poisoned) hr=E_FAIL;
        else {
            HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!event) hr=HRESULT_FROM_WIN32(GetLastError());
            else {
                hr=state->fence->SetEventOnCompletion(state->next,event);
                if(SUCCEEDED(hr) && WaitForSingleObject(event,5000)!=WAIT_OBJECT_0) hr=DXGI_ERROR_WAIT_TIMEOUT;
                CloseHandle(event);
                if(SUCCEEDED(hr)) hr=Poll();
            }
        }
        if(FAILED(hr)) { retained=true;state.release(); } // Unknown GPU lifetime: retain until exit.
        else state.reset();
        return hr;
    }
    ID3D12Resource* Latest() const {
        return state && state->next ? state->slots[(state->cursor+slotCount-1)%slotCount].texture.Get():nullptr;
    }
    // Call immediately after external consumer work is enqueued on the same queue.
    HRESULT SealLatest() {
        if(!state || !state->next || state->poisoned) return E_UNEXPECTED;
        auto& slot=state->slots[(state->cursor+slotCount-1)%slotCount];slot.value=++state->next;
        HRESULT hr=state->queue->Signal(state->fence.Get(),slot.value);
        if(FAILED(hr)) state->poisoned=true;
        return hr;
    }
    bool StereoReady() const { return state && state->next && state->slots[(state->cursor+slotCount-1)%slotCount].eyes==3; }
    HRESULT Copy(ID3D12CommandQueue* queue,ID3D12Resource* source,int eye=-1) {
        if(retained || !queue || !source || queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) return E_INVALIDARG;
        if(eye < -1 || eye>1) return E_INVALIDARG;
        auto desc=source->GetDesc();
        if(eye>=0 && desc.Width>4096) return E_INVALIDARG;
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 ||
            desc.DepthOrArraySize!=1 || desc.MipLevels!=1 || !desc.Width || !desc.Height || desc.Width>8192 || desc.Height>8192) return E_INVALIDARG;
        if(state && (state->stereo!=(eye>=0) || state->queue.Get()!=queue || state->desc.Width!=desc.Width ||
            state->desc.Height!=desc.Height || state->desc.Format!=desc.Format)) return DXGI_ERROR_INVALID_CALL;
        if(!state) {
            ComPtr<ID3D12Device> device,other;
            HRESULT hr=queue->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr)) return hr;
            hr=source->GetDevice(IID_PPV_ARGS(&other));if(FAILED(hr)) return hr;
            ComPtr<IUnknown> a,b;device.As(&a);other.As(&b);if(a.Get()!=b.Get()) return E_INVALIDARG;
            std::unique_ptr<State> fresh(new(std::nothrow) State);if(!fresh) return E_OUTOFMEMORY;
            fresh->queue=queue;fresh->desc=desc;fresh->stereo=eye>=0;
            hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fresh->fence));if(FAILED(hr)) return hr;
            if(eye>=0) desc.Width*=2;
            desc.Flags=D3D12_RESOURCE_FLAG_NONE;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            for(unsigned i=0;i<slotCount;++i) {
                auto& slot=fresh->slots[i];
                hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,restingState,nullptr,IID_PPV_ARGS(&slot.texture));if(FAILED(hr)) return hr;
                hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator));if(FAILED(hr)) return hr;
                hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list));if(FAILED(hr)) return hr;
                hr=slot.list->Close();if(FAILED(hr)) return hr;
            }
            state=std::move(fresh);
        }
        HRESULT hr=Poll();if(FAILED(hr)) return hr;
        auto& slot=state->slots[state->cursor];
        if(slot.source) { ++skipped;return S_FALSE; } // Never wait for a slot on the frame path.
        hr=slot.allocator->Reset();if(FAILED(hr)) return hr;
        hr=slot.list->Reset(slot.allocator.Get(),nullptr);if(FAILED(hr)) return hr;
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for(auto& barrier:barriers) { barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
        barriers[0].Transition.pResource=source;
        barriers[0].Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource=slot.texture.Get();
        barriers[1].Transition.StateBefore=restingState;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        slot.list->ResourceBarrier(2,barriers);
        if(eye<0) slot.list->CopyResource(slot.texture.Get(),source);
        else {
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=slot.texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            slot.list->CopyTextureRegion(&to,static_cast<UINT>(state->desc.Width)*eye,0,0,&from,nullptr);
        }
        for(auto& barrier:barriers) std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        slot.list->ResourceBarrier(2,barriers);
        hr=slot.list->Close();if(FAILED(hr)) return hr;
        slot.source=source;slot.value=++state->next;
        ID3D12CommandList* lists[]={slot.list.Get()};queue->ExecuteCommandLists(1,lists);
        hr=queue->Signal(state->fence.Get(),slot.value);
        if(FAILED(hr)) { state->poisoned=true;return hr; }
        if(eye>=0) slot.eyes|=1u<<eye;
        state->cursor=(state->cursor+1)%slotCount;++submitted;return S_OK;
    }
};
}
