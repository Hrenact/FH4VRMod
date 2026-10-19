#define CINTERFACE
#include <dxgi1_6.h>
#include <cstddef>
static_assert(offsetof(IDXGIFactory2Vtbl,CreateSwapChain)==10*sizeof(void*));
static_assert(offsetof(IDXGIFactory2Vtbl,CreateSwapChainForHwnd)==15*sizeof(void*));
static_assert(offsetof(IDXGIFactory2Vtbl,CreateSwapChainForCoreWindow)==16*sizeof(void*));
static_assert(offsetof(IDXGIFactory2Vtbl,CreateSwapChainForComposition)==24*sizeof(void*));
static_assert(offsetof(IDXGISwapChain1Vtbl,Present)==8*sizeof(void*));
static_assert(offsetof(IDXGISwapChain1Vtbl,ResizeBuffers)==13*sizeof(void*));
static_assert(offsetof(IDXGISwapChain1Vtbl,Present1)==22*sizeof(void*));
