#define NOMINMAX
#include <windows.h>
#include <d3d12sdklayers.h>
#include "frame_phase_capture.h"
#include <cstdio>
#define REQUIRE(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
#define OK(x) REQUIRE((x)==S_OK)
using capture::ComPtr;
struct Gate{ComPtr<ID3D12Fence> fence;~Gate(){if(fence)fence->Signal(1);}};
int main(){
    frame_phase::State state;REQUIRE(state.Reserve());REQUIRE(!state.Reserve());REQUIRE(!state.Read(true,1));
    REQUIRE(state.Begin(100));REQUIRE(!state.Begin(100));
    const int expected[]={0,0,0,-1,-1,-1,1,1,1,0,0,-1,1,-1,1,1,-1,-1,1,-1,1,0,0,0};
    for(unsigned i=1;i<=24;++i){REQUIRE(state.Read(true,100+i)==i);REQUIRE(!state.Read(false,100+i));
        REQUIRE(frame_phase::Eye(i)==expected[i-1]*.032f);REQUIRE(state.Advance(i,100+i)==(i<24));}
    REQUIRE(!state.Read(true,200));REQUIRE(!state.Begin(200));
    frame_phase::State expired;REQUIRE(expired.Start(100));REQUIRE(!expired.Read(true,10100));REQUIRE(!expired.Advance(1,10100));REQUIRE(!expired.phase);
    frame_phase::State cancelled;REQUIRE(cancelled.Start(1));cancelled.Stop();REQUIRE(!cancelled.Advance(1,2));REQUIRE(!cancelled.Read(true,2));
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<ID3D12Device> device;OK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;device.As(&info);
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;OK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    for(const UINT sourceWidth:{257u,1200u}){
        const UINT sourceHeight=sourceWidth==257?19:1100;
        ComPtr<ID3D12Resource> sources[3];
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=sourceWidth;desc.Height=sourceHeight;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;
        ComPtr<ID3D12DescriptorHeap> heap;OK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        ComPtr<ID3D12CommandAllocator> allocator;OK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;OK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        const float colors[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}},white[4]={1,1,1,1};
        for(unsigned i=0;i<3;++i){
            OK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&sources[i])));
            auto rtv=heap->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=static_cast<SIZE_T>(i)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device->CreateRenderTargetView(sources[i].Get(),nullptr,rtv);list->ClearRenderTargetView(rtv,colors[i],0,nullptr);
            if(sourceWidth>1024){D3D12_RECT outside[]={{0,0,88,1100},{0,0,1200,38}};list->ClearRenderTargetView(rtv,white,2,outside);}
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=sources[i].Get();b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;list->ResourceBarrier(1,&b);
        }
        OK(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        capture::Image pixels;bool retained=false;OK(capture::Readback(queue.Get(),sources[0].Get(),pixels,retained));
        frame_phase::Batch batch;OK(batch.Prepare(queue.Get(),sources[0].Get()));
        REQUIRE(batch.width==std::min(sourceWidth,1024u) && batch.height==std::min(sourceHeight,1024u));
        REQUIRE(batch.x==(sourceWidth-batch.width)/2 && batch.y==(sourceHeight-batch.height)/2);
        Gate gate;OK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate.fence)));OK(queue->Wait(gate.fence.Get(),1));
        const auto begin=GetTickCount64();
        for(unsigned phase=1;phase<=24;++phase){
            OK(batch.Copy(phase,sources[expected[phase-1]+1].Get()));
            REQUIRE(batch.Image(phase));for(unsigned prior=1;prior<phase;++prior)REQUIRE(batch.Image(phase)!=batch.Image(prior));
        }
        REQUIRE(GetTickCount64()-begin<2000);REQUIRE(batch.Copy(1,sources[0].Get())==E_UNEXPECTED);
        OK(gate.fence->Signal(1));
        for(unsigned phase=1;phase<=24;++phase){
            OK(capture::Readback(queue.Get(),batch.Image(phase),pixels,retained));REQUIRE(!retained);
            const auto color=colors[expected[phase-1]+1];
            for(size_t pixel=0;pixel<pixels.rgba.size();pixel+=4)for(unsigned c=0;c<4;++c)REQUIRE(pixels.rgba[pixel+c]==static_cast<BYTE>(color[c]*255));
        }
        OK(batch.Drain());REQUIRE(!batch.Image(1));
        printf("PASS %ux%u: 24 independent GPU snapshots, center crop coordinates/pixels, blocked queue, deferred readback\n",sourceWidth,sourceHeight);
    }
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<BYTE> bytes(size);auto message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());OK(info->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(message->pDescription);return 2;}
    }
    printf("PASS sequence, after-Present activation, one-shot, timeout/focus gating/cancellation; GPU debug_layer=%d\n",debug?1:0);
}
