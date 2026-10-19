#pragma once
#include <vector>
#include <new>
namespace runtime_mode {
inline bool Valid(UINT w,UINT h) {return w>=256 && h>=256 && w<=4096 && h<=8192 && w%4==0 && h%4==0;}
template<class T,class Fetch> HRESULT Enumerate(Fetch fetch,UINT width,UINT height,UINT* count,T* destination) {
    if(!Valid(width,height) || !count)return fetch(count,destination);
    UINT available=0;HRESULT hr=fetch(&available,nullptr);
    if(FAILED(hr)||!available||available>4096)return fetch(count,destination);
    try {
        std::vector<T> native(available);
        hr=fetch(&available,native.data());
        if(FAILED(hr)||available>native.size()||!available)return fetch(count,destination);
        native.resize(available);
        bool exists=false;
        for(const auto& mode:native)if(mode.Width==width && mode.Height==height)exists=true;
        if(!exists) {
            T mode=native.front();mode.Width=width;mode.Height=height;
            mode.ScanlineOrdering=DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
            mode.Scaling=DXGI_MODE_SCALING_UNSPECIFIED;
            native.push_back(mode);
        }
        UINT required=static_cast<UINT>(native.size());
        if(!destination){*count=required;return S_OK;}
        UINT capacity=*count;*count=required;
        if(capacity<required)return DXGI_ERROR_MORE_DATA;
        for(UINT i=0;i<required;++i)destination[i]=native[i];
        return S_OK;
    } catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
}
}
