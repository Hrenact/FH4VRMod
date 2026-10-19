#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <DirectXMath.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "../src/frame_capture.h"
#include "../src/ui_clip_projection.h"
#include "../src/ui_affine_clip.h"
#include "../src/ui_material_clip_math.h"
using capture::ComPtr;
using Matrix=std::array<float,16>;
#define CHECK(x) do { HRESULT hr_=(x); if(FAILED(hr_)){printf("FAIL line=%d hr=%08lx\n",__LINE__,hr_);return false;} } while(0)
static Matrix Identity(){Matrix m{};for(int i=0;i<4;++i)m[5*i]=1;return m;}
static Matrix Multiply(const Matrix& a,const Matrix& b){Matrix m{};
    // Match the native binder's SSE association, including cancellation.
    if(!ui_material_clip_math::detail::Product(a.data(),b.data(),m.data())){puts("Nonfinite matrix product");std::exit(3);}return m;}
static Matrix Inverse(const Matrix& m){DirectX::XMFLOAT4X4 src{},dst{};memcpy(&src,m.data(),64);auto inv=DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&src));DirectX::XMStoreFloat4x4(&dst,inv);Matrix r;memcpy(r.data(),&dst,64);return r;}
static Matrix Projection(float yaw,float pitch,float roll,float eye){
    Matrix ry=Identity(),rx=Identity(),rz=Identity();
    ry[0]=ry[10]=cosf(yaw);ry[2]=sinf(yaw);ry[8]=-sinf(yaw);
    rx[5]=rx[10]=cosf(pitch);rx[6]=sinf(pitch);rx[9]=-sinf(pitch);
    rz[0]=rz[5]=cosf(roll);rz[1]=sinf(roll);rz[4]=-sinf(roll);
    auto rotation=Multiply(Multiply(ry,rx),rz);
    Matrix p=Identity();p[0]=2.f/256;p[5]=-2.f/256;p[10]=2.f/256;p[12]=-1;p[13]=1;
    auto result=p;const float tangent=tanf(.9f),half=2*tanf(25*3.14159265359f/180);
    for(int r=0;r<4;++r){float pos[3]{};for(int j=0;j<3;++j)pos[j]=p[4*r]*half*rotation[j]+p[4*r+1]*half*rotation[4+j]+p[4*r+3]*2*rotation[8+j];
        result[4*r]=(pos[0]-eye*p[4*r+3])/(2*tangent);result[4*r+1]=pos[1]/(2*tangent);result[4*r+3]=pos[2]/2;}
    return result;
}
static Matrix NativePlanes(const Matrix& matrix,const float* rect){
    // Reconstructed from 0x37f9d20, independently validated by restricted SSE interpretation.
    auto inverse=Inverse(matrix);Matrix result{};
    const float p[4][4]={{0,1,0,rect[1]},{1,0,0,rect[0]},{0,-1,0,-rect[1]-rect[3]},{-1,0,0,-rect[0]-rect[2]}};
    for(int i=0;i<4;++i){float normal[3]{},point[3]{};
        for(int c=0;c<3;++c)for(int k=0;k<3;++k)normal[c]+=p[i][k]*inverse[4*c+k];
        for(int c=0;c<3;++c){point[c]=matrix[12+c];for(int k=0;k<3;++k)point[c]+=p[i][3]*p[i][k]*matrix[4*k+c];result[4*i+c]=normal[c];}
        for(int c=0;c<3;++c)result[4*i+3]+=point[c]*normal[c];}
    return result;
}
struct Vertex {float position[3],color[4],uv[4];};
struct Gpu {
    static constexpr UINT Size=512;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Resource> target,constants,vertices;ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    bool Buffer(UINT64 bytes,ComPtr<ID3D12Resource>& out){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&out)));return true;}
    bool Setup(const std::vector<char>& vs){
        ComPtr<ID3D12ShaderReflection> reflection;CHECK(D3DReflect(vs.data(),vs.size(),IID_PPV_ARGS(&reflection)));
        auto cb=reflection->GetConstantBufferByName("VSConstants");D3D12_SHADER_BUFFER_DESC bd{};CHECK(cb->GetDesc(&bd));
        if(bd.Size!=208){printf("Unexpected VS constant buffer size: %u\n",bd.Size);return false;}
        printf("Actual FH4 map VS loaded: VSConstants=%u bytes\n",bd.Size);
        CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC qd{};CHECK(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=td.Height=Size;td.DepthOrArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;CHECK(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.Get(),nullptr,rtv);
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameter.Descriptor.ShaderRegister=0;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> rootBytes,error,ps;CHECK(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error));CHECK(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
        const char* source="float4 main():SV_Target {return float4(1,1,1,1);}";
        CHECK(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error));
        D3D12_INPUT_ELEMENT_DESC inputs[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,28,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs.data(),vs.size()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.InputLayout={inputs,3};pd.SampleMask=UINT_MAX;pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.DepthStencilState.DepthEnable=FALSE;pd.DepthStencilState.StencilEnable=FALSE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=td.Format;pd.SampleDesc.Count=1;
        CHECK(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)));
        if(!Buffer(2048,constants)||!Buffer(64*sizeof(Vertex),vertices))return false;
        CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso.Get(),IID_PPV_ARGS(&list)));CHECK(list->Close());return true;
    }
    bool Draw(const Matrix& world,const Matrix& project,const Matrix& planes,const float* geometry,capture::Image& image,
              const Matrix* screen=nullptr,const std::vector<Vertex>* supplied=nullptr,const float* actualSize=nullptr){
        float cb[13][4]{};memcpy(cb,world.data(),64);memcpy(cb+4,project.data(),64);
        memcpy(cb+8,planes.data(),64);cb[12][0]=1;
        cb[12][1]=actualSize?actualSize[0]:Size;cb[12][2]=actualSize?actualSize[1]:Size;
        (void)screen;
        void* data=nullptr;D3D12_RANGE none{0,0};CHECK(constants->Map(0,&none,&data));memcpy(data,cb,sizeof(cb));constants->Unmap(0,nullptr);
        const float corners[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};Vertex v[6]{};
        for(int i=0;i<6;++i){v[i].position[0]=geometry[0]+corners[i][0]*geometry[2];v[i].position[1]=geometry[1]+corners[i][1]*geometry[3];v[i].position[2]=0;for(float& channel:v[i].color)channel=1;}
        UINT count=supplied?static_cast<UINT>(supplied->size()):6;if(count>64)return false;
        CHECK(vertices->Map(0,&none,&data));if(count)memcpy(data,supplied?supplied->data():v,count*sizeof(Vertex));vertices->Unmap(0,nullptr);
        CHECK(allocator->Reset());CHECK(list->Reset(allocator.Get(),pso.Get()));list->SetGraphicsRootSignature(root.Get());list->SetGraphicsRootConstantBufferView(0,constants->GetGPUVirtualAddress());
        D3D12_VERTEX_BUFFER_VIEW vb{vertices->GetGPUVirtualAddress(),64*sizeof(Vertex),sizeof(Vertex)};list->IASetVertexBuffers(0,1,&vb);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VIEWPORT vp{0,0,float(Size),float(Size),0,1};D3D12_RECT sc{0,0,Size,Size};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);const float black[4]{};list->ClearRenderTargetView(rtv,black,0,nullptr);list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);list->DrawInstanced(count,1,0,0);CHECK(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        bool retained=false;CHECK(capture::Readback(queue.Get(),target.Get(),image,retained,D3D12_RESOURCE_STATE_RENDER_TARGET));return true;
    }
};
static size_t Area(const capture::Image& image){size_t n=0;for(size_t i=0;i<image.rgba.size();i+=4)n+=image.rgba[i]>127;return n;}
static size_t Difference(const capture::Image& a,const capture::Image& b){size_t n=0;for(size_t i=0;i<a.rgba.size();i+=4)n+=(a.rgba[i]>127)!=(b.rgba[i]>127);return n;}
static bool Run(const std::filesystem::path& shader,const std::filesystem::path& output){
    std::ifstream file(shader,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});if(bytes.size()<32){puts("Could not read shader bytecode");return false;}
    Gpu gpu;if(!gpu.Setup(bytes))return false;std::filesystem::create_directories(output);
    struct Case {const char* name;float yaw,pitch,roll,eye,scale,tx,ty,tz;};
    const Case cases[]={
        {"front",0,0,0,.032f,1,0,0,0},
        {"yaw_right",.4f,0,0,.032f,1,0,0,0},
        {"yaw_left",-.4f,0,0,-.032f,1,0,0,0},
        {"pitch_up",0,.35f,0,.032f,1,0,0,0},
        {"pitch_down",0,-.35f,0,-.032f,1,0,0,0},
        {"combined_local_z_positive",.3f,-.25f,.15f,.032f,.8f,20,25,24},
        {"combined_local_z_negative",-.3f,.25f,-.15f,-.032f,1.1f,-10,-15,-20},
        {"front_local_z",0,0,0,.032f,.8f,20,25,24}};
    // Observed A/D from reports/ui-native-full-43764.json, first sample. Test data only.
    Matrix A=Identity();A[14]=4.167492866516113f;
    const Matrix D={4.167492866516113f,0,0,0,0,4.167492866516113f,0,0,0,0,1.0000399351119995f,1,0,0,-.3000119924545288f,0};
    const float rectangle[4]={40,48,176,160},full[4]={0,0,256,256};Matrix disabled{};for(int i=0;i<4;++i)disabled[4*i+3]=-1;
    std::ofstream report(output/"results.json");report<<"{\n  \"actual_game_vs\": true,\n  \"shader\":\""<<shader.filename().string()<<"\",\n  \"fixture\":\"actual captured A/D; synthetic inset source rect and local/projective UI transform including local z; constant white PS; UV zero (AA degenerate-direction fallback only); no captured map mesh\",\n  \"cases\": [\n";bool all=true;
    size_t nativeErrors=0,homogeneousErrors=0,affineErrors=0,affineWorldErrors=0;
    for(size_t i=0;i<std::size(cases);++i){const auto& c=cases[i];auto combined=Multiply(Projection(c.yaw,c.pitch,c.roll,c.eye),A);auto local=Identity();local[0]=local[5]=c.scale;local[12]=c.tx;local[13]=c.ty;local[14]=c.tz;auto draw=Multiply(local,combined);auto mvp=Multiply(draw,D);
        auto native=NativePlanes(combined,rectangle);Matrix homogeneous{},textAffine{};
        if(!ui_clip_projection::RectPlanes(combined.data(),rectangle,homogeneous.data())||!ui_affine_clip::ShaderPlanes(combined.data(),rectangle,textAffine.data()))return false;
        auto affineDraw=draw;affineDraw[3]=affineDraw[7]=affineDraw[11]=0;affineDraw[15]=1;
        const float cropped[4]={(rectangle[0]-c.tx)/c.scale,(rectangle[1]-c.ty)/c.scale,rectangle[2]/c.scale,rectangle[3]/c.scale};
        capture::Image reference,unclipped,original,homogeneousImg,affineImg,affineWorldImg,affineUnclipped;
        if(!gpu.Draw(draw,mvp,disabled,cropped,reference)||!gpu.Draw(draw,mvp,disabled,full,unclipped)||!gpu.Draw(draw,mvp,native,full,original)||!gpu.Draw(draw,mvp,homogeneous,full,homogeneousImg)||!gpu.Draw(draw,mvp,textAffine,full,affineImg)||!gpu.Draw(affineDraw,mvp,textAffine,full,affineWorldImg)||!gpu.Draw(affineDraw,mvp,disabled,full,affineUnclipped))return false;
        auto expected=Area(reference),total=Area(unclipped),nativeDiff=Difference(reference,original),hDiff=Difference(reference,homogeneousImg),aDiff=Difference(reference,affineImg),awDiff=Difference(reference,affineWorldImg);
        bool fixtureValid=expected>1000 && total>expected+1000 && awDiff<=8 && Difference(unclipped,affineUnclipped)==0 && (i!=0 || nativeDiff<=8);all&=fixtureValid;
        nativeErrors+=nativeDiff;homogeneousErrors+=hDiff;affineErrors+=aDiff;affineWorldErrors+=awDiff;
        printf("%s reference=%zu full=%zu native_diff=%zu full_inverse_diff=%zu text_affine_diff=%zu affine_world_diff=%zu %s\n",c.name,expected,total,nativeDiff,hDiff,aDiff,awDiff,fixtureValid?"FIXTURE_VALID":"FIXTURE_INVALID");
        report<<"    {\"name\":\""<<c.name<<"\",\"reference_pixels\":"<<expected<<",\"full_pixels\":"<<total<<",\"native_difference_pixels\":"<<nativeDiff<<",\"full_inverse_difference_pixels\":"<<hDiff<<",\"text_affine_difference_pixels\":"<<aDiff<<",\"affine_world_difference_pixels\":"<<awDiff<<",\"fixture_valid\":"<<(fixtureValid?"true":"false")<<"}"<<(i+1<std::size(cases)?",":"")<<"\n";
        struct Pic {const char* name;capture::Image* image;} pics[]={{"reference",&reference},{"unclipped",&unclipped},{"native",&original},{"full_inverse",&homogeneousImg},{"text_affine",&affineImg},{"affine_world",&affineWorldImg}};
        for(auto& p:pics){auto path=output/(std::string(c.name)+"_"+p.name+".png");CHECK(capture::SavePng(path.c_str(),*p.image));}
    }
    report<<"  ],\n  \"native_total_difference\": "<<nativeErrors<<",\n  \"full_inverse_total_difference\": "<<homogeneousErrors<<",\n  \"text_affine_total_difference\": "<<affineErrors<<",\n  \"affine_world_total_difference\": "<<affineWorldErrors<<",\n  \"fixture_valid\": "<<(all?"true":"false")<<"\n}\n";
    puts(all?"PASS fixture validity; candidate differences are measurements, not all candidates pass":"FAIL fixture validity");return all;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3){puts("Usage: test_ui_map_shader_clip.exe <MapRender-VS.dxbc> <output-dir>");return 2;}
    return Run(argv[1],argv[2])?0:1;
}
