#define NOMINMAX
#include <windows.h>
#include "ui_layer_capture.h"
#include <cassert>
using capture::ComPtr;
#define CHECK(x) do{auto hr=(x);if(FAILED(hr)){printf("FAIL %d %08lx\n",__LINE__,hr);return 1;}}while(0)
int main(){
    ComPtr<ID3D12Device> device;CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;CHECK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ui_layer_capture::queueHookReady=true;
    for(auto state:{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET})
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_TYPELESS})for(UINT width:{257u,333u})for(UINT16 mips:{UINT16(1),UINT16(0)}){
        assert(ui_layer_capture::Arm());
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=19;
        desc.DepthOrArraySize=1;desc.MipLevels=mips;desc.Format=format;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> source;CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&source)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        ComPtr<ID3D12DescriptorHeap> heap;CHECK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),&view,rtv);
        ComPtr<ID3D12CommandAllocator> allocator;CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        float color[4]={.25f,.5f,.75f,.5f};list->ClearRenderTargetView(rtv,color,0,nullptr);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=source.Get();
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if(state==D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)list->ResourceBarrier(1,&b);
        CHECK(ui_layer_capture::Record(list.Get(),source.Get(),state));
        assert(ui_layer_capture::Record(list.Get(),source.Get(),state)==S_FALSE);
        assert(!ui_layer_capture::Arm());
        auto job=ui_layer_capture::jobs[0];assert(job && !job->submitted);
        capture::Image image;assert(ui_layer_capture::Decode(*job,image)==E_PENDING);
        ui_layer_capture::Submitted(queue.Get(),0,nullptr);assert(!job->submitted);
        // Reuse the source after the copy; snapshot must still contain the first color.
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        if(state==D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)list->ResourceBarrier(1,&b);
        float replacement[4]={1,0,0,1};list->ClearRenderTargetView(rtv,replacement,0,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        if(state==D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)list->ResourceBarrier(1,&b);
        CHECK(list->Close());ID3D12CommandList* submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);
        ui_layer_capture::Submitted(queue.Get(),1,submitted);assert(job->submitted);
        HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(done);CHECK(job->fence->SetEventOnCompletion(1,done));
        assert(WaitForSingleObject(done,5000)==WAIT_OBJECT_0);CloseHandle(done);
        CHECK(ui_layer_capture::Decode(*job,image));assert(image.width==width && image.height==19);
        for(size_t i=0;i<image.rgba.size();i+=4){assert(abs(int(image.rgba[i])-64)<=1);assert(abs(int(image.rgba[i+1])-128)<=1);
            assert(abs(int(image.rgba[i+2])-191)<=1);assert(abs(int(image.rgba[i+3])-128)<=1);}
        delete job;ui_layer_capture::jobs[0]=nullptr;ui_layer_capture::remaining=0;
    }
    // Same-size target at multiple draw ordinals: preserve four different snapshots.
    assert(ui_layer_capture::Arm());
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=33;desc.Height=17;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> source;CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&source)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> heap;CHECK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),nullptr,rtv);
    ComPtr<ID3D12CommandAllocator> allocator;CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    for(unsigned ordinal=1;ordinal<=128;++ordinal){
        float color[4]={ordinal/255.f,0,0,.5f};list->ClearRenderTargetView(rtv,color,0,nullptr);
        auto hr=ui_layer_capture::Record(list.Get(),source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,true);
        assert(hr==(ui_layer_capture::SampleDraw(ordinal)?S_OK:S_FALSE));
    }
    assert(ui_layer_capture::remaining==0);
    CHECK(list->Close());ID3D12CommandList* submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);
    ui_layer_capture::Submitted(queue.Get(),1,submitted);
    unsigned expected[]={1,8,32,128};
    for(unsigned i=0;i<4;++i){auto job=ui_layer_capture::jobs[i];assert(job && job->submitted);
        HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(done);CHECK(job->fence->SetEventOnCompletion(1,done));
        assert(WaitForSingleObject(done,5000)==WAIT_OBJECT_0);CloseHandle(done);
        capture::Image image;CHECK(ui_layer_capture::Decode(*job,image));
        for(size_t p=0;p<image.rgba.size();p+=4){assert(abs(int(image.rgba[p])-int(expected[i]))<=1);assert(abs(int(image.rgba[p+3])-128)<=1);}
        delete job;ui_layer_capture::jobs[i]=nullptr;
    }
    puts("PASS UI capture: typed/typeless mip chains, RT/PS restoration, exact-list fences, four same-size draw snapshots with distinct pixels, alpha and row pitch");
}
