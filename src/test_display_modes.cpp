#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
using FactoryFn=HRESULT(WINAPI*)(UINT,REFIID,void**);
struct Snapshot {
    HRESULT countHr, listHr, smallHr, invalidHr, count1Hr, list1Hr;
    UINT count=0, shortCount=1, count1=0;
    std::vector<DXGI_MODE_DESC> modes;
    std::vector<DXGI_MODE_DESC1> modes1;
};
static Snapshot Read(IDXGIOutput1* output) {
    Snapshot s{};
    s.countHr=output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM,0,&s.count,nullptr);
    s.modes.resize(s.count);
    s.listHr=output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM,0,&s.count,s.modes.data());
    DXGI_MODE_DESC one{};
    s.smallHr=output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM,0,&s.shortCount,&one);
    UINT invalid=0;s.invalidHr=output->GetDisplayModeList(DXGI_FORMAT_UNKNOWN,0,&invalid,nullptr);
    s.count1Hr=output->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM,0,&s.count1,nullptr);
    s.modes1.resize(s.count1);
    s.list1Hr=output->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM,0,&s.count1,s.modes1.data());
    return s;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 && argc!=4) return 2;
    UINT targetW=argc==4?static_cast<UINT>(_wtoi(argv[2])):0;
    UINT targetH=argc==4?static_cast<UINT>(_wtoi(argv[3])):0;
    wchar_t path[MAX_PATH];GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\dxgi.dll");
    auto system=LoadLibraryW(path);if(!system)return 3;
    auto create=reinterpret_cast<FactoryFn>(GetProcAddress(system,"CreateDXGIFactory2"));
    ComPtr<IDXGIFactory2> factory;if(FAILED(create(0,IID_PPV_ARGS(&factory))))return 4;
    ComPtr<IDXGIAdapter1> adapter;if(FAILED(factory->EnumAdapters1(0,&adapter)))return 5;
    ComPtr<IDXGIOutput> output;if(FAILED(adapter->EnumOutputs(0,&output)))return 6;
    ComPtr<IDXGIOutput1> output1;if(FAILED(output.As(&output1)))return 7;
    auto before=Read(output1.Get());if(FAILED(before.listHr)||FAILED(before.list1Hr))return 8;
    auto proxy=LoadLibraryW(argv[1]);if(!proxy)return 9;
    create=reinterpret_cast<FactoryFn>(GetProcAddress(proxy,"CreateDXGIFactory2"));
    // Repeated attachment must not create callback recursion.
    for(unsigned repeat=0;repeat<3;++repeat) {
        ComPtr<IDXGIFactory2> hooked;if(FAILED(create(0,IID_PPV_ARGS(&hooked))))return 10;
        auto after=Read(output1.Get());
        UINT extra=targetW?1:0;
        printf("counts %u->%u small %u->%u extended %u->%u hr=%08lx/%08lx invalid=%08lx/%08lx\n",
            before.count,after.count,before.shortCount,after.shortCount,before.count1,after.count1,
            before.smallHr,after.smallHr,before.invalidHr,after.invalidHr);
        if(before.countHr!=after.countHr||before.listHr!=after.listHr||before.smallHr!=after.smallHr||
           before.invalidHr!=after.invalidHr||before.count1Hr!=after.count1Hr||before.list1Hr!=after.list1Hr||
           before.count+extra!=after.count||(extra?before.count+extra:before.shortCount)!=after.shortCount||before.count1+extra!=after.count1||
           memcmp(before.modes.data(),after.modes.data(),before.modes.size()*sizeof(DXGI_MODE_DESC))||
           memcmp(before.modes1.data(),after.modes1.data(),before.modes1.size()*sizeof(DXGI_MODE_DESC1)))return 11;
        if(extra && (after.modes.back().Width!=targetW || after.modes.back().Height!=targetH ||
            after.modes1.back().Width!=targetW || after.modes1.back().Height!=targetH))return 12;
    }
    printf("PASS display mode passthrough: %u legacy, %u extended modes; shortCount buffers, invalid format, repeated attachment.\n",before.count,before.count1);
    return 0;
}

