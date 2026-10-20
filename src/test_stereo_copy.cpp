#define NOMINMAX
#include <windows.h>
#include <d3d12sdklayers.h>
#include "stereo_copy.h"
#include <cstdio>
#include <cmath>
using capture::ComPtr;
#define REQUIRE(x) do{if(!(x)){printf("FAIL line=%d: %s\n",__LINE__,#x);return 1;}}while(0)
#define OK(x) REQUIRE((x)==S_OK)
// On every failure unblock the queue BEFORE the ring/source destructors run.
struct Gate {
    ComPtr<ID3D12Fence> fence;
    ~Gate(){if(fence)fence->Signal(UINT64_MAX-1);}
};
int main(){
    ComPtr<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<ID3D12Device> device;OK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;device.As(&info);
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;OK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM}){
        ComPtr<ID3D12Resource> sources[6];
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=257;desc.Height=19;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=6;
        ComPtr<ID3D12DescriptorHeap> rtvs;OK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
        ComPtr<ID3D12CommandAllocator> allocator;OK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;OK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        const float colors[6][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1},{1,0,1,1},{0,1,1,1}};
        for(unsigned i=0;i<6;++i){
            OK(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&sources[i])));
            auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=static_cast<SIZE_T>(i)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device->CreateRenderTargetView(sources[i].Get(),nullptr,rtv);list->ClearRenderTargetView(rtv,colors[i],0,nullptr);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=sources[i].Get();
            barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
            list->ResourceBarrier(1,&barrier);
        }
        OK(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        capture::Image image;bool retained=false;OK(capture::Readback(queue.Get(),sources[0].Get(),image,retained));
        gpu_copy::StereoRing ring;
        Gate gate;OK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate.fence)));
        OK(queue->Wait(gate.fence.Get(),1)); // None of the next copies can complete yet.
        ComPtr<ID3D12Resource> pairs[3];
        const auto started=GetTickCount64();
        for(unsigned i=0;i<3;++i){
            const UINT64 generation=i+1;
            OK(ring.Copy(queue.Get(),sources[i*2].Get(),0,generation));
            REQUIRE(!ring.StereoReady(generation));
            OK(ring.Copy(queue.Get(),sources[i*2+1].Get(),1,generation));
            REQUIRE(ring.StereoReady(generation));pairs[i]=ring.Latest();
            for(unsigned j=0;j<i;++j)REQUIRE(pairs[i].Get()!=pairs[j].Get());
            OK(ring.SealLatest());
        }
        REQUIRE(ring.Copy(queue.Get(),sources[0].Get(),0,4)==S_FALSE); // All three slots busy.
        REQUIRE(ring.Copy(queue.Get(),sources[1].Get(),1,4)==S_FALSE); // Cannot borrow generation 3 left.
        REQUIRE(!ring.StereoReady(4));REQUIRE(ring.submitted==6 && ring.completed==0);
        REQUIRE(GetTickCount64()-started<2000); // No render-path GPU waiting.
        OK(gate.fence->Signal(1));
        for(unsigned i=0;i<3;++i){
            OK(capture::Readback(queue.Get(),pairs[i].Get(),image,retained,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
            REQUIRE(!retained && image.width==514 && image.height==19);
            for(UINT y=0;y<image.height;++y)for(UINT x=0;x<image.width;++x){
                const auto pixel=image.rgba.data()+(static_cast<size_t>(y)*image.width+x)*4;
                const auto color=colors[i*2+(x>=257?1:0)];
                for(unsigned c=0;c<4;++c)REQUIRE(std::abs(int(pixel[c])-int(color[c]*255))<=1);
            }
        }
        OK(ring.Poll());REQUIRE(ring.completed==6);
        OK(ring.Copy(queue.Get(),sources[0].Get(),0,5));
        REQUIRE(ring.Copy(queue.Get(),sources[1].Get(),1,6)==S_FALSE); // Wrong-generation right discards left.
        REQUIRE(ring.Copy(queue.Get(),sources[1].Get(),1,5)==S_FALSE); // No resurrection.
        OK(ring.Copy(queue.Get(),sources[2].Get(),0,6));
        OK(ring.Copy(queue.Get(),sources[3].Get(),1,6));
        REQUIRE(ring.Copy(queue.Get(),sources[0].Get(),0,7)==E_UNEXPECTED); // Consumer has not sealed yet.
        OK(ring.SealLatest());
        OK(capture::Readback(queue.Get(),ring.Latest(),image,retained,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        REQUIRE(image.rgba[2]==255 && image.rgba[257*4]==255 && image.rgba[257*4+1]==255);
        OK(ring.Drain());REQUIRE(!ring.retained && ring.completed==ring.submitted);
        // Delayed external consumer fence alone must prevent recycling too,
        // even after the first pair's copies are fully complete.
        OK(ring.Copy(queue.Get(),sources[0].Get(),0,1));
        OK(ring.Copy(queue.Get(),sources[1].Get(),1,1));
        ComPtr<ID3D12Resource> consumed=ring.Latest();
        OK(capture::Readback(queue.Get(),consumed.Get(),image,retained,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        OK(ring.Poll());
        OK(queue->Wait(gate.fence.Get(),2));OK(ring.SealLatest());
        for(UINT64 generation=2;generation<=3;++generation){
            OK(ring.Copy(queue.Get(),sources[2].Get(),0,generation));
            OK(ring.Copy(queue.Get(),sources[3].Get(),1,generation));
            REQUIRE(ring.Latest()!=consumed.Get());OK(ring.SealLatest());
        }
        REQUIRE(ring.Copy(queue.Get(),sources[0].Get(),0,4)==S_FALSE);
        OK(gate.fence->Signal(2));OK(ring.Drain());
        REQUIRE(!ring.retained && ring.completed==ring.submitted);
        // Start/stop reuse resets generation identity and incomplete halves.
        OK(ring.Copy(queue.Get(),sources[0].Get(),0,1));ring.DiscardPair();
        REQUIRE(ring.Copy(queue.Get(),sources[1].Get(),1,1)==S_FALSE);
        OK(ring.Drain());REQUIRE(!ring.retained && ring.completed==ring.submitted);
        printf("PASS stereo GPU format=%u\n",format);
    }
    if(info){
        for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
            SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<BYTE> storage(bytes);
            auto message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());OK(info->GetMessage(i,message,&bytes));
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(message->pDescription);return 2;}
        }
    }
    printf("PASS independent eye allocation, 3 in-flight pairs, GPU backpressure/recovery, generation rejection, consumer fence, pixels; debug_layer=%d\n",debug?1:0);
}
