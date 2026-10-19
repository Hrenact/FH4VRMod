#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include "ui_isolated_capture.h"
#include <cassert>
using capture::ComPtr;
#define CHECK(x) do{HRESULT hr=(x);if(FAILED(hr)){printf("FAIL line %d hr=%08lx\n",__LINE__,hr);return 1;}}while(0)
int main(){
    ComPtr<ID3D12Device> device;CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;CHECK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=64;rd.Height=32;rd.DepthOrArraySize=1;rd.MipLevels=1;
    rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> scene;CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&scene)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> heap;CHECK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(scene.Get(),nullptr,rtv);
    const char* shader="float4 vs(uint i:SV_VertexID):SV_Position {return float4(i==1?3:-1,i==2?3:-1,0,1);} float4 ps():SV_Target{return float4(1,0,0,1);}";
    ComPtr<ID3DBlob> vs,ps,rootBlob,error;
    CHECK(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error));
    CHECK(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error));
    D3D12_ROOT_SIGNATURE_DESC rsd{};CHECK(D3D12SerializeRootSignature(&rsd,D3D_ROOT_SIGNATURE_VERSION_1,&rootBlob,&error));
    ComPtr<ID3D12RootSignature> root;CHECK(device->CreateRootSignature(0,rootBlob->GetBufferPointer(),rootBlob->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    pd.DepthStencilState.DepthEnable=FALSE;pd.DepthStencilState.StencilEnable=FALSE;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=rd.Format;pd.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState> pso;CHECK(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)));
    ComPtr<ID3D12CommandAllocator> allocator;CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso.Get(),IID_PPV_ARGS(&list)));
    list->SetGraphicsRootSignature(root.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT viewport{0,0,64,32,0,1};list->RSSetViewports(1,&viewport);
    D3D12_RECT left{0,0,32,32},right{32,0,64,32};list->RSSetScissorRects(1,&left);
    const float blue[4]={0,0,1,1};list->ClearRenderTargetView(rtv,blue,0,nullptr);list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    unsigned draws=0;auto draw=[&](){++draws;list->DrawInstanced(3,1,0,0);};
    assert(!ui_isolated_capture::Try(list.Get(),scene.Get(),rtv,draw) && draws==0);
    ui_layer_capture::queueHookReady=true;assert(ui_layer_capture::Arm());
    assert(ui_isolated_capture::Try(list.Get(),scene.Get(),rtv,draw) && draws==1);
    assert(ui_layer_capture::remaining==0 && ui_layer_capture::jobs[0]->retainedRtvHeap);
    // No rebind here: a real draw must hit the restored scene RTV.
    list->RSSetScissorRects(1,&right);list->DrawInstanced(3,1,0,0);
    // Test-only second readback alongside the pending isolation job.
    ui_layer_capture::remaining=1;ui_layer_capture::seenCount=0;
    CHECK(ui_layer_capture::Record(list.Get(),scene.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET));
    CHECK(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);ui_layer_capture::Submitted(queue.Get(),1,lists);
    for(unsigned i=0;i<2;++i){auto job=ui_layer_capture::jobs[i];assert(job && job->submitted);
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(job->fence->SetEventOnCompletion(1,event));assert(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);
        capture::Image image;CHECK(ui_layer_capture::Decode(*job,image));
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto p=&image.rgba[(y*64+x)*4];
            if(i==0){if(x<32)assert(p[0]==255 && p[1]==0 && p[2]==0 && p[3]==255);else assert(p[0]==0 && p[1]==0 && p[2]==0 && p[3]==0);}
            else {if(x<32)assert(p[0]==0 && p[1]==0 && p[2]==255 && p[3]==255);else assert(p[0]==255 && p[1]==0 && p[2]==0 && p[3]==255);}
        }
        delete job;ui_layer_capture::jobs[i]=nullptr;
    }
    puts("PASS isolated draw: transparent UI target, original scene preserved, subsequent draw restores scene RTV, exactly-once callback, fenced heap lifetime");
}
