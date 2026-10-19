#define NOMINMAX
#include <windows.h>
#include "frame_capture.h"
#include "gpu_copy.h"
#include <cstdio>
#include <cmath>
using capture::ComPtr;
#define CHECK(call) do { HRESULT hr=(call);if(FAILED(hr)) { printf("FAIL line=%d hr=%08lx\n",__LINE__,hr);return 1; } } while(0)
int main() {
    ComPtr<ID3D12Device> device;CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;CHECK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    DXGI_FORMAT formats[]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM};
    for(auto format:formats) {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=257;desc.Height=19;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> source;CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&source)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        ComPtr<ID3D12DescriptorHeap> heap;CHECK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),nullptr,rtv);
        ComPtr<ID3D12CommandAllocator> allocator;CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        float color[4]={.25f,.5f,.75f,1};list->ClearRenderTargetView(rtv,color,0,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=source.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&barrier);CHECK(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        capture::Image image;bool retained=false;CHECK(capture::Readback(queue.Get(),source.Get(),image,retained));
        if(retained || image.width!=257 || image.height!=19) return 2;
        for(size_t i=0;i<image.rgba.size();i+=4) {
            if(std::abs(int(image.rgba[i])-64)>1 || std::abs(int(image.rgba[i+1])-128)>1 ||
               std::abs(int(image.rgba[i+2])-191)>1 || image.rgba[i+3]!=255) return 3;
        }
        gpu_copy::Ring ring;
        for(unsigned i=0;i<1000;++i) { CHECK(ring.Copy(queue.Get(),source.Get())); }
        // Same queue readback also waits for all preceding copies, then validates the GPU texture.
        CHECK(capture::Readback(queue.Get(),ring.Latest(),image,retained));
        for(size_t i=0;i<image.rgba.size();i+=4) {
            if(std::abs(int(image.rgba[i])-64)>1 || std::abs(int(image.rgba[i+1])-128)>1 ||
               std::abs(int(image.rgba[i+2])-191)>1 || image.rgba[i+3]!=255) return 4;
        }
        CHECK(ring.Drain());
        if(ring.retained || ring.completed!=ring.submitted || !ring.submitted) return 5;
        printf("PASS GPU ring format=%u submitted=%llu completed=%llu skipped=%llu\n",format,ring.submitted,ring.completed,ring.skipped);
        CHECK(ring.Copy(queue.Get(),source.Get()));CHECK(ring.Drain());
        std::unique_ptr<gpu_copy::Ring> snapshots[9];
        for(unsigned n=0;n<9;++n) {
            snapshots[n]=std::make_unique<gpu_copy::Ring>(D3D12_RESOURCE_STATE_COMMON,1);
            CHECK(snapshots[n]->Copy(queue.Get(),source.Get()));
            for(unsigned j=0;j<n;++j) if(snapshots[n]->Latest()==snapshots[j]->Latest()) return 6;
        }
        for(auto& snapshot:snapshots) {
            CHECK(capture::Readback(queue.Get(),snapshot->Latest(),image,retained));
            for(size_t i=0;i<image.rgba.size();i+=4) {
                if(std::abs(int(image.rgba[i])-64)>1 || std::abs(int(image.rgba[i+1])-128)>1 ||
                   std::abs(int(image.rgba[i+2])-191)>1 || image.rgba[i+3]!=255) return 7;
            }
            CHECK(snapshot->Drain());if(snapshot->retained || snapshot->completed!=1) return 8;
        }
        printf("PASS nine independent deferred GPU snapshots format=%u\n",format);
        gpu_copy::Ring stereo(D3D12_RESOURCE_STATE_COMMON,1);
        CHECK(stereo.Copy(queue.Get(),source.Get(),0));
        if(stereo.StereoReady()) return 9;
        CHECK(capture::Readback(queue.Get(),source.Get(),image,retained)); // Complete the left copy before changing source.
        CHECK(allocator->Reset());CHECK(list->Reset(allocator.Get(),nullptr));
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&barrier);float changed[4]={.75f,.25f,.5f,0};list->ClearRenderTargetView(rtv,changed,0,nullptr);
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&barrier);CHECK(list->Close());queue->ExecuteCommandLists(1,lists);
        CHECK(stereo.Copy(queue.Get(),source.Get(),1));if(!stereo.StereoReady()) return 10;
        CHECK(stereo.SealLatest());CHECK(capture::Readback(queue.Get(),stereo.Latest(),image,retained));
        if(image.width!=514 || image.height!=19) return 11;
        if(image.alphaZero!=257*19 || image.alphaOpaque!=257*19 || image.alphaZeroTop!=257*9) return 14;
        for(UINT y=0;y<image.height;++y) for(UINT x=0;x<image.width;++x) {
            auto pixel=image.rgba.data()+(static_cast<size_t>(y)*image.width+x)*4;
            int expected[3]={x<257?64:191,x<257?128:64,x<257?191:128};
            for(unsigned c=0;c<3;++c) if(std::abs(int(pixel[c])-expected[c])>1) return 12;
        }
        CHECK(stereo.Drain());if(stereo.retained || stereo.completed!=2) return 13;
        // Initialization must also place a right-first copy in the correct half.
        gpu_copy::Ring rightFirst(D3D12_RESOURCE_STATE_COMMON,1);
        CHECK(rightFirst.Copy(queue.Get(),source.Get(),1));
        CHECK(capture::Readback(queue.Get(),rightFirst.Latest(),image,retained));
        for(UINT y=0;y<19;++y) for(UINT x=257;x<514;++x) {
            auto pixel=image.rgba.data()+(static_cast<size_t>(y)*514+x)*4;
            if(std::abs(int(pixel[0])-191)>1 || std::abs(int(pixel[1])-64)>1 || std::abs(int(pixel[2])-128)>1) return 15;
        }
        CHECK(rightFirst.Drain());
        printf("PASS SBS distinct left/right colors and half preservation format=%u\n",format);
        wchar_t path[128];swprintf_s(path,L"F:\\FH4VRMod\\reports\\capture-fixture-%u.png",format);
        CHECK(capture::SavePng(path,image));
        printf("PASS format=%u: 257x19 known-color GPU readback and PNG\n",format);
        // A second capture verifies the first command list restored PRESENT.
        CHECK(capture::Readback(queue.Get(),source.Get(),image,retained));
    }
    return 0;
}
