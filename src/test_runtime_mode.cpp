#include <windows.h>
#include <dxgi1_6.h>
#include <cassert>
#include <cstdio>
#include "runtime_mode.h"
template<class T> void Test() {
    T native{};native.Width=1920;native.Height=1080;native.RefreshRate={60,1};
    auto fetch=[&](UINT* n,T* dest)->HRESULT {
        if(!n)return E_INVALIDARG;
        if(!dest){*n=1;return S_OK;}
        if(*n<1){*n=1;return DXGI_ERROR_MORE_DATA;}
        *n=1;*dest=native;return S_OK;
    };
    UINT n=0;assert(runtime_mode::Enumerate<T>(fetch,2400,2400,&n,nullptr)==S_OK && n==2);
    T out[3]{};out[2].Width=12345;
    n=1;assert(runtime_mode::Enumerate<T>(fetch,2400,2400,&n,out)==DXGI_ERROR_MORE_DATA && n==2 && out[0].Width==0);
    n=3;assert(runtime_mode::Enumerate<T>(fetch,2400,2400,&n,out)==S_OK && n==2);
    assert(out[0].Width==1920 && out[1].Width==2400 && out[1].Height==2400 && out[2].Width==12345);
    n=0;assert(runtime_mode::Enumerate<T>(fetch,1920,1080,&n,nullptr)==S_OK && n==1);
    n=0;assert(runtime_mode::Enumerate<T>(fetch,9000,9000,&n,nullptr)==S_OK && n==1);
    assert(runtime_mode::Enumerate<T>(fetch,2400,2400,nullptr,nullptr)==E_INVALIDARG);
    auto fail=[](UINT*,T*)->HRESULT{return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;};
    assert(runtime_mode::Enumerate<T>(fail,2400,2400,&n,out)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
    auto empty=[](UINT* c,T*)->HRESULT{*c=0;return S_OK;};
    n=1;assert(runtime_mode::Enumerate<T>(empty,2400,2400,&n,out)==S_OK && n==0);
}
int main(){Test<DXGI_MODE_DESC>();Test<DXGI_MODE_DESC1>();puts("PASS runtime mode count/list, capacity, duplicate, bounds, errors and empty list (both APIs)");}
