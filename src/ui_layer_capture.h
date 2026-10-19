#pragma once
#include "frame_capture.h"
#include <mutex>
#include <atomic>
#include <cstdio>
namespace ui_layer_capture {
using capture::ComPtr;
struct Job {
    ComPtr<ID3D12Resource> source,readback;
    ComPtr<ID3D12DescriptorHeap> retainedRtvHeap;
    ComPtr<ID3D12CommandList> list;
    ComPtr<ID3D12Fence> fence;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes=0;UINT width=0,height=0;unsigned number=0;
    bool submitted=false,failed=false;
};
inline std::mutex lock;
inline Job* jobs[4]{}; // Unconfirmed submissions remain alive until process exit.
inline std::atomic<ID3D12CommandList*> pendingLists[4]{};
inline std::atomic<unsigned> remaining{0};
inline std::atomic<bool> queueHookReady{false};
inline UINT64 deadline=0,seenSizes[4]{};
inline unsigned seenCount=0,serial=0;
inline std::atomic<unsigned> callbackCount{0},diagnosticCount{0};
inline unsigned recordCount=0;
inline unsigned eligibleCount=0;
inline bool SampleDraw(unsigned ordinal) {return ordinal==1 || ordinal==8 || ordinal==32 || ordinal==128;}
inline bool held=false;
inline HMODULE owner=nullptr;
inline bool Path(wchar_t* path,const wchar_t* name) {
    DWORD n=GetModuleFileNameW(owner,path,MAX_PATH);if(!n||n>=MAX_PATH)return false;
    auto tail=wcsrchr(path,L'\\');return tail && !wcscpy_s(tail+1,MAX_PATH-(tail+1-path),name);
}
inline void Log(const char* event,unsigned n,HRESULT hr,UINT w=0,UINT h=0) {
    if(!owner)return;wchar_t path[MAX_PATH]{};if(!Path(path,L"ui-layer-capture.log"))return;
    FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){fprintf(f,"pid=%lu tick=%llu event=%s number=%u hr=%08lx size=%ux%u\n",GetCurrentProcessId(),GetTickCount64(),event,n,hr,w,h);fclose(f);}
}
inline void Gate(const char* reason,unsigned state=0) {
    if(diagnosticCount.fetch_add(1)<16)Log(reason,callbackCount.load(),S_OK,state);
}
inline void EndCollection(const char* reason) {
    if(remaining.exchange(0))Log(reason,callbackCount.load(),S_OK,recordCount,seenCount);
}
inline void Describe(const D3D12_RESOURCE_DESC& d) {
    if(!owner || diagnosticCount.fetch_add(1)>=16)return;
    wchar_t path[MAX_PATH]{};if(!Path(path,L"ui-layer-capture.log"))return;
    FILE* f=nullptr;if(!_wfopen_s(&f,path,L"a")&&f){
        fprintf(f,"pid=%lu tick=%llu event=resource_desc format=%u dimension=%u size=%llux%u array=%u mips=%u samples=%u flags=%u\n",
            GetCurrentProcessId(),GetTickCount64(),unsigned(d.Format),unsigned(d.Dimension),d.Width,d.Height,
            unsigned(d.DepthOrArraySize),unsigned(d.MipLevels),d.SampleDesc.Count,unsigned(d.Flags));fclose(f);
    }
}
inline bool Arm() {
    std::lock_guard<std::mutex> guard(lock);
    for(auto job:jobs)if(job)return false;
    if(!queueHookReady.load())return false;
    seenCount=0;recordCount=0;eligibleCount=0;callbackCount=0;diagnosticCount=0;
    deadline=GetTickCount64()+10000;remaining=4;Log("armed",0,S_OK);return true;
}
// Called while the game's list is recording, after native RT -> pixel-resource transition.
inline HRESULT Record(ID3D12GraphicsCommandList* list,ID3D12Resource* source,
    D3D12_RESOURCE_STATES sourceState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,bool staged=false,
    ID3D12DescriptorHeap* retainedRtvHeap=nullptr) {
    if(!remaining.load())return S_FALSE;
    std::lock_guard<std::mutex> guard(lock);
    if(!remaining.load())return S_FALSE;
    if(GetTickCount64()>deadline){EndCollection("expired_in_record");return S_FALSE;}
    ++recordCount;
    if(!list || !source || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return E_INVALIDARG;
    auto desc=source->GetDesc();
    Describe(desc);
    // A typeless RGBA8 resource has the same byte layout as its UNORM RTV.
    bool rgba8=desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS;
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || !rgba8 ||
       desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1 || !desc.MipLevels || !desc.Width || !desc.Height || desc.Width>4096 || desc.Height>4096){Gate("skip_descriptor");return S_FALSE;}
    UINT64 size=(desc.Width<<32)|desc.Height;
    if(staged){if(!SampleDraw(++eligibleCount))return S_FALSE;}
    else for(unsigned i=0;i<seenCount;++i)if(seenSizes[i]==size){Gate("skip_duplicate_size");return S_FALSE;}
    unsigned index=4;for(unsigned i=0;i<4;++i)if(!jobs[i]){index=i;break;}if(index==4)return S_FALSE;
    ComPtr<ID3D12Device> device,other;HRESULT hr=source->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr))return hr;
    hr=list->GetDevice(IID_PPV_ARGS(&other));if(FAILED(hr)||device.Get()!=other.Get())return E_INVALIDARG;
    std::unique_ptr<Job> job(new(std::nothrow) Job);if(!job)return E_OUTOFMEMORY;
    job->source=source;job->list=list;job->width=static_cast<UINT>(desc.Width);job->height=desc.Height;
    job->retainedRtvHeap=retainedRtvHeap;
    device->GetCopyableFootprints(&desc,0,1,0,&job->footprint,nullptr,nullptr,&job->bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=job->bytes;
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&job->readback));if(FAILED(hr))return hr;
    hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&job->fence));if(FAILED(hr))return hr;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=source;
    // Capture mip 0 only; leave the game's lower mip states untouched.
    b.Transition.Subresource=0;b.Transition.StateBefore=sourceState;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=job->readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=job->footprint;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    job->number=++serial;seenSizes[seenCount++]=size;--remaining;jobs[index]=job.release();
    pendingLists[index]=jobs[index]->list.Get();Log("recorded",jobs[index]->number,S_OK,jobs[index]->width,jobs[index]->height);
    if(staged)Log("draw_sample",jobs[index]->number,S_OK,eligibleCount,static_cast<UINT>(sourceState));return S_OK;
}
// Fence only after the exact command list containing the copy was submitted.
inline void Submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists) {
    bool relevant=false;for(auto& pending:pendingLists){auto p=pending.load();if(p)for(UINT i=0;i<count;++i)if(lists[i]==p)relevant=true;}
    if(!relevant)return;
    std::lock_guard<std::mutex> guard(lock);
    for(unsigned index=0;index<4;++index){auto job=jobs[index];if(job && !job->submitted && !job->failed){
        bool found=false;for(UINT i=0;i<count;++i)if(lists[i]==job->list.Get())found=true;
        if(found){auto hr=queue->Signal(job->fence.Get(),1);job->submitted=SUCCEEDED(hr);job->failed=FAILED(hr);pendingLists[index]=nullptr;Log("submitted",job->number,hr);}
    }}
}
inline HRESULT Decode(Job& job,capture::Image& image) {
    if(!job.submitted || job.fence->GetCompletedValue()<1)return E_PENDING;
    if(job.fence->GetCompletedValue()==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
    image.width=job.width;image.height=job.height;image.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    try{image.rgba.resize(size_t(job.width)*job.height*4);}catch(...){return E_OUTOFMEMORY;}
    void* pixels=nullptr;D3D12_RANGE read{0,static_cast<SIZE_T>(job.bytes)};
    auto hr=job.readback->Map(0,&read,&pixels);if(FAILED(hr))return hr;
    for(UINT y=0;y<job.height;++y)memcpy(image.rgba.data()+size_t(y)*job.width*4,
        static_cast<BYTE*>(pixels)+job.footprint.Offset+size_t(y)*job.footprint.Footprint.RowPitch,size_t(job.width)*4);
    D3D12_RANGE written{0,0};job.readback->Unmap(0,&written);return S_OK;
}
inline void Poll(bool foreground) {
    DWORD saved=GetLastError();bool key=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    if(foreground && key && !held)Arm();held=key;
    std::lock_guard<std::mutex> guard(lock);
    if(!foreground)EndCollection("foreground_lost");
    else if(GetTickCount64()>deadline)EndCollection("expired");
    for(auto& job:jobs)if(job && job->submitted && !job->failed){
        auto completed=job->fence->GetCompletedValue();if(completed==UINT64_MAX){job->failed=true;Log("device_removed",job->number,DXGI_ERROR_DEVICE_REMOVED);continue;}
        if(completed<1)continue;
        capture::Image image;auto hr=Decode(*job,image);
        wchar_t name[96]{},path[MAX_PATH]{};swprintf_s(name,L"fh4vr-ui-layer-%lu-%u.png",GetCurrentProcessId(),job->number);
        if(SUCCEEDED(hr))hr=Path(path,name)?capture::SavePng(path,image):E_FAIL;
        Log("saved",job->number,hr,job->width,job->height);delete job;job=nullptr;
    }
    SetLastError(saved);
}
}
