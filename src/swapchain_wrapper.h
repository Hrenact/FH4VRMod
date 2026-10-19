#pragma once
#include "camera_pair_recovery.h"
#include <new>
#ifdef FH4VR_FRAME_CAPTURE
#include "frame_capture.h"
#endif
#ifdef FH4VR_GPU_COPY
#include "gpu_copy.h"
#endif
#ifdef FH4VR_LIVE_OVERLAY
#include "live_overlay.h"
#endif
#ifdef FH4VR_AER_PROBE
#include "aer_probe.h"
#ifdef FH4VR_AER_DEFERRED
#include "gpu_copy.h"
#endif
#endif
#ifdef FH4VR_STEREO_TRIAL
#include "gpu_copy.h"
#include "live_overlay.h"
#ifdef FH4VR_LIVE_SCENE
#include "live_scene.h"
#endif
#include "stereo_trial.h"
#ifdef FH4VR_STEREO_HEAD
#include "stereo_head.h"
#endif
#endif
// Included by graphics_probe.cpp after Log/Describe. No native vtable writes.
class SwapchainWrapper final : public IDXGISwapChain4 {
    std::atomic<ULONG> references{1};
    IDXGISwapChain4* real;
    std::atomic<unsigned long long> presents{0};
#ifdef FH4VR_FRAME_CAPTURE
    capture::ComPtr<ID3D12CommandQueue> captureQueue;
    bool captureHeld=true,captureDisabled=false;
    unsigned captureCount=0;
    void MaybeCapture(UINT flags) {
        DWORD saved=GetLastError();
        if(!(flags&DXGI_PRESENT_TEST) && captureQueue && !captureDisabled && captureCount<3) {
            DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            bool pressed=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
            if(foreground!=GetCurrentProcessId()) captureHeld=true;
            else {
                bool trigger=pressed && !captureHeld;captureHeld=pressed;
                if(trigger) {
                    ++captureCount;auto started=GetTickCount64();
                    capture::ComPtr<ID3D12Resource> buffer;
                    HRESULT hr=real->GetBuffer(real->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer));
                    capture::Image image;bool retained=false;
                    if(SUCCEEDED(hr)) hr=capture::Readback(captureQueue.Get(),buffer.Get(),image,retained);
                    buffer.Reset();
                    wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);
                    wchar_t* tail=wcsrchr(path,L'\\');
                    if(SUCCEEDED(hr) && tail) {
                        swprintf_s(tail+1,MAX_PATH-(tail+1-path),L"fh4vr-capture-%lu-%u.png",GetCurrentProcessId(),captureCount);
                        hr=capture::SavePng(path,image);
                    } else if(SUCCEEDED(hr)) hr=E_FAIL;
                    if(FAILED(hr)) captureDisabled=true;
                    Log("capture_result number=%u hr=%08lx size=%ux%u format=%u retained=%d elapsed_ms=%llu path=%ls",
                        captureCount,hr,image.width,image.height,image.format,retained,GetTickCount64()-started,path);
                }
            }
        }
        SetLastError(saved);
    }
#endif
#ifdef FH4VR_GPU_COPY
#ifdef FH4VR_LIVE_OVERLAY
    gpu_copy::Ring gpuRing{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    LiveOverlay liveOverlay;
#else
    gpu_copy::Ring gpuRing;
#endif
    bool gpuHeld=true,gpuDisabled=false,gpuActive=false;
    ULONGLONG gpuStarted=0;
    void StopGpu(const char* reason) {
        if(!gpuActive) return;
        gpuActive=false;
#ifdef FH4VR_LIVE_OVERLAY
        liveOverlay.Stop(); // Runtime releases all submitted textures before ring destruction.
#endif
        HRESULT hr=gpuRing.Drain();if(FAILED(hr)) gpuDisabled=true;
        Log("gpu_copy_stop reason=%s hr=%08lx submitted=%llu completed=%llu skipped=%llu retained=%d",
            reason,hr,gpuRing.submitted,gpuRing.completed,gpuRing.skipped,gpuRing.retained);
    }
    void MaybeGpu(UINT flags) {
        DWORD saved=GetLastError();
        if(!(flags&DXGI_PRESENT_TEST)) {
            DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            bool pressed=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
            if(foreground!=GetCurrentProcessId()) { gpuHeld=true;StopGpu("focus"); }
            else {
                bool trigger=pressed && !gpuHeld;gpuHeld=pressed;
                if(trigger && !gpuDisabled && !gpuActive && captureQueue) {
#ifdef FH4VR_LIVE_OVERLAY
                    HRESULT startHr=liveOverlay.Start();
                    if(FAILED(startHr)) {gpuDisabled=true;Log("overlay_activation_failed hr=%08lx",startHr);SetLastError(saved);return;}
#endif
                    gpuActive=true;gpuStarted=GetTickCount64();Log("gpu_copy_start duration_ms=10000");
                }
                if(gpuActive && GetTickCount64()-gpuStarted>=10000) StopGpu("timeout");
                if(gpuActive) {
                    capture::ComPtr<ID3D12Resource> source;
                    HRESULT hr=real->GetBuffer(real->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&source));
                    if(SUCCEEDED(hr)) hr=gpuRing.Copy(captureQueue.Get(),source.Get());
#ifdef FH4VR_LIVE_OVERLAY
                    if(hr==S_OK) {
                        HRESULT submitHr=liveOverlay.Submit(gpuRing.Latest(),captureQueue.Get());
                        HRESULT sealHr=gpuRing.SealLatest();
                        hr=FAILED(submitHr)?submitHr:sealHr;
                    }
#endif
                    if(FAILED(hr)) { gpuDisabled=true;Log("gpu_copy_error hr=%08lx",hr);StopGpu("error"); }
                }
            }
        }
        SetLastError(saved);
    }
#endif
#ifdef FH4VR_AER_PROBE
    bool aerHeld=true,aerActive=false;
    unsigned aerCaptured=0;
#ifdef FH4VR_AER_DEFERRED
    std::unique_ptr<gpu_copy::Ring> aerFrames[aer_probe::PhaseCount];
    bool aerFrameReady[aer_probe::PhaseCount]{};
    void SaveAerFrames() {
        // All eye commands have been stopped before any CPU readback/encoding.
        for(unsigned i=0;i<aer_probe::PhaseCount;++i) if(aerFrames[i]) {
            capture::Image image;bool retained=false;
            HRESULT hr=E_FAIL;
            if(aerFrameReady[i] && aerFrames[i]->Latest()) hr=capture::Readback(captureQueue.Get(),aerFrames[i]->Latest(),image,retained);
            wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);auto tail=wcsrchr(path,L'\\');
            if(SUCCEEDED(hr) && tail) {
                swprintf_s(tail+1,MAX_PATH-(tail+1-path),L"fh4vr-aer-%lu-phase-%u.png",GetCurrentProcessId(),i+1);
                hr=capture::SavePng(path,image);
            } else if(SUCCEEDED(hr)) hr=E_FAIL;
            HRESULT drain=aerFrames[i]->Drain();
            Log("aer_deferred_saved phase=%u hr=%08lx drain=%08lx retained=%d path=%ls",i+1,hr,drain,retained || aerFrames[i]->retained,path);
            aerFrames[i].reset();
        }
    }
#endif
    void StopAer(const char* reason) {
        if(!aerActive) return;
        aerActive=false;aer_probe::state.Stop();
        Log("aer_stop reason=%s captures=%u",reason,aerCaptured);
#ifdef FH4VR_AER_DEFERRED
        SaveAerFrames();
#endif
    }
    unsigned CaptureAer(UINT flags) {
        if(flags&DXGI_PRESENT_TEST) return 0;
        DWORD saved=GetLastError(),foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        bool pressed=(GetAsyncKeyState(VK_F3)&0x8000)!=0;
        if(foreground!=GetCurrentProcessId()) {aerHeld=true;StopAer("focus");SetLastError(saved);return 0;}
        bool trigger=pressed && !aerHeld;aerHeld=pressed;
        if(trigger && !aerActive && captureQueue && aer_probe::state.Start(GetTickCount64())) {
            aerActive=true;Log("aer_start phases=%u timeout_ms=10000",aer_probe::PhaseCount);
        }
        if(!aerActive) {SetLastError(saved);return 0;}
        unsigned phase=aer_probe::state.Read(true,GetTickCount64());
        if(!phase) {StopAer("timeout");SetLastError(saved);return 0;}
        auto callsBefore=aer_probe::state.calls[phase].load();
        auto appliedBefore=aer_probe::state.applied[phase].load();
        auto rejectedBefore=aer_probe::state.rejected[phase].load();
        auto started=GetTickCount64();
        capture::ComPtr<ID3D12Resource> buffer;
#ifndef FH4VR_AER_DEFERRED
        capture::Image image;bool retained=false;
#endif
        HRESULT hr=real->GetBuffer(real->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer));
#ifdef FH4VR_AER_DEFERRED
        if(SUCCEEDED(hr)) {
            aerFrames[phase-1].reset(new(std::nothrow) gpu_copy::Ring(D3D12_RESOURCE_STATE_COMMON,1));
            hr=aerFrames[phase-1]?aerFrames[phase-1]->Copy(captureQueue.Get(),buffer.Get()):E_OUTOFMEMORY;
            aerFrameReady[phase-1]=hr==S_OK;
        }
        buffer.Reset();
        ++aerCaptured;
        Log("aer_copy_queued phase=%u requested_eye=%.3f calls_before=%llu applied_before=%llu rejected_before=%llu calls_after=%llu hr=%08lx elapsed_ms=%llu",
            phase,aer_probe::Eye(phase),callsBefore,appliedBefore,rejectedBefore,aer_probe::state.calls[phase].load(),hr,GetTickCount64()-started);
#else
        if(SUCCEEDED(hr)) hr=capture::Readback(captureQueue.Get(),buffer.Get(),image,retained);
        buffer.Reset();
        wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);wchar_t* tail=wcsrchr(path,L'\\');
        if(SUCCEEDED(hr) && tail) {
            swprintf_s(tail+1,MAX_PATH-(tail+1-path),L"fh4vr-aer-%lu-phase-%u.png",GetCurrentProcessId(),phase);
            hr=capture::SavePng(path,image);
        } else if(SUCCEEDED(hr)) hr=E_FAIL;
        ++aerCaptured;
        Log("aer_capture phase=%u requested_eye=%.3f calls_before=%llu applied_before=%llu rejected_before=%llu calls_after=%llu hr=%08lx retained=%d elapsed_ms=%llu path=%ls",
            phase,aer_probe::Eye(phase),callsBefore,appliedBefore,rejectedBefore,aer_probe::state.calls[phase].load(),hr,retained,GetTickCount64()-started,path);
#endif
        if(FAILED(hr)) {StopAer("capture_error");phase=0;}
        SetLastError(saved);return phase;
    }
    void AdvanceAer(unsigned phase,HRESULT presentResult) {
        if(!phase || !aerActive) return;
        if(presentResult!=S_OK) {StopAer("present_not_ok");return;}
        Log("aer_present phase=%u camera_calls=%llu applied=%llu rejected=%llu",
            phase,aer_probe::state.calls[phase].load(),aer_probe::state.applied[phase].load(),aer_probe::state.rejected[phase].load());
        if(!aer_probe::state.Advance(phase,GetTickCount64())) StopAer("complete_or_timeout");
    }
#endif
#ifdef FH4VR_STEREO_TRIAL
    gpu_copy::Ring stereoRing{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,1};
#ifdef FH4VR_LIVE_SCENE
    LiveScene stereoOverlay;
#else
    LiveOverlay stereoOverlay;
#endif
    stereo_trial::ToggleKey stereoKey;
    bool stereoActive=false,stereoPairLeft=false;
    CameraPairRecovery cameraRecovery;
    uint64_t stereoFrame=0,stereoEyes[2]{};
    void StopStereo(const char* reason) {
        if(!stereoActive) return;
        stereoActive=false;stereo_trial::command.store(0);
        stereoOverlay.Stop();
        // Capture only the normal timed stop, after the compositor releases its texture.
        // Keep readback and encoding outside the alternating-eye sequence.
        if(strcmp(reason,"timeout")==0 && stereoRing.StereoReady()) {
            capture::Image proof;bool retained=false;
            HRESULT saved=capture::Readback(captureQueue.Get(),stereoRing.Latest(),proof,retained,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);
            wchar_t* tail=wcsrchr(path,L'\\');
            if(SUCCEEDED(saved) && tail) {
                swprintf_s(tail+1,MAX_PATH-(tail+1-path),L"fh4vr-stereo-%lu.png",GetCurrentProcessId());
                saved=capture::SavePng(path,proof);
            } else if(SUCCEEDED(saved)) saved=E_FAIL;
            Log("stereo_proof hr=%08lx size=%ux%u retained=%d path=%ls",saved,proof.width,proof.height,retained,path);
            Log("stereo_alpha pixels=%llu zero=%llu opaque=%llu zero_top_half=%llu",
                static_cast<UINT64>(proof.width)*proof.height,proof.alphaZero,proof.alphaOpaque,proof.alphaZeroTop);
        }
        HRESULT hr=stereoRing.Drain();
#ifdef FH4VR_STEREO_HEAD
        Log("stereo_head_stop applied=%llu rejected=%llu",stereo_head::applied.load(),stereo_head::rejected.load());
#endif
        Log("stereo_stop reason=%s hr=%08lx frames=%llu left=%llu right=%llu submitted=%u skipped=%llu retained=%d applied=%llu rejected=%llu",
            reason,hr,stereoFrame,stereoEyes[0],stereoEyes[1],stereoOverlay.submitted,stereoRing.skipped,stereoRing.retained,stereo_trial::applied.load(),stereo_trial::rejected.load());
    }
    bool BeforeStereo(UINT flags) {
        if(flags&DXGI_PRESENT_TEST) return false;
        DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        bool pressed=(GetAsyncKeyState(VK_F2)&0x8000)!=0;
        bool focused=foreground==GetCurrentProcessId();
        bool trigger=stereoKey.Press(focused,pressed);
        if(!focused) {StopStereo("focus");return false;}
        if(trigger && stereoActive){StopStereo("toggle");return false;}
        if(trigger && !stereoActive && captureQueue && !stereoRing.retained) {
            HRESULT hr=stereoOverlay.Start(true);
#ifdef FH4VR_LIVE_SCENE
            DXGI_SWAP_CHAIN_DESC1 sourceDesc{};
            if(SUCCEEDED(hr))hr=real->GetDesc1(&sourceDesc);
            if(SUCCEEDED(hr))hr=stereoOverlay.ConfigureSource(sourceDesc.Width,sourceDesc.Height);
            if(FAILED(hr))stereoOverlay.Stop();
#endif
            if(FAILED(hr)) {stereoOverlay.Stop();Log("stereo_start_error hr=%08lx",hr);return false;}
            stereoActive=true;stereoFrame=0;stereoEyes[0]=stereoEyes[1]=0;stereoPairLeft=false;
            cameraRecovery={};
            stereo_trial::command.store(0);stereo_trial::deadline.store(0);
            stereo_trial::applied.store(0);stereo_trial::rejected.store(0);
#ifdef FH4VR_STEREO_HEAD
            stereo_head::applied.store(0);stereo_head::rejected.store(0);
#endif
            Log("stereo_start toggle=1 duration_ms=unlimited hold_frames=%llu paired_submit=1",stereo_trial::holdFrames);
            // Leave this activation frame centered. Publish command 1 after Present.
        }
        if(!stereoActive) return false;
        uint64_t headRejected=0;
#ifdef FH4VR_STEREO_HEAD
        headRejected=stereo_head::rejected.load();
#endif
        const auto now=GetTickCount64();
        if(cameraRecovery.Observe(stereoFrame,stereo_trial::rejected.load(),headRejected,now,stereo_trial::holdFrames*2)){
            stereoPairLeft=false;
            Log("stereo_drop_camera_pair frame=%llu through=%llu camera_rejected=%llu head_rejected=%llu",stereoFrame,cameraRecovery.dropThrough,cameraRecovery.camera,cameraRecovery.head);
        }
        if(cameraRecovery.Expired(now)){StopStereo("camera_recovery_timeout");return false;}
        if(cameraRecovery.Drop(stereoFrame))return true; // Advance schedule without capturing either eye from this pair.
        int eye=stereo_trial::CapturedEye(stereoFrame);
        if(eye>=0) {
            capture::ComPtr<ID3D12Resource> source;
            HRESULT hr=real->GetBuffer(real->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&source));
            if(SUCCEEDED(hr)) hr=stereoRing.Copy(captureQueue.Get(),source.Get(),eye);
            if(hr==S_OK) {
                ++stereoEyes[eye];
                if(eye==0) stereoPairLeft=true;
                if(eye==1 && stereoPairLeft && stereoRing.StereoReady()) {
                    HRESULT submit=stereoOverlay.Submit(stereoRing.Latest(),captureQueue.Get());
                    if(submit==S_OK)cameraRecovery.Submitted();
                    HRESULT seal=stereoRing.SealLatest();hr=FAILED(submit)?submit:seal;
                    stereoPairLeft=false;
                }
            }
            if(FAILED(hr)) {Log("stereo_copy_error hr=%08lx",hr);StopStereo("copy_error");return false;}
        }
        return true;
    }
    void AfterStereo(bool participating,HRESULT hr) {
        if(!participating || !stereoActive) return;
        if(hr!=S_OK) {StopStereo("present_not_ok");return;}
        ++stereoFrame;
#ifdef FH4VR_STEREO_HEAD
        if((stereoFrame-1)%(stereo_trial::holdFrames*2)==0) {
#ifdef FH4VR_LIVE_SCENE
            HeadPose gamePose{};
            if(FAILED(stereoOverlay.BeginPair(gamePose))){StopStereo("pose_error");return;}
            AcquireSRWLockExclusive(&stereo_head::lock);
            stereo_head::pairPose=gamePose;
            ReleaseSRWLockExclusive(&stereo_head::lock);
#else
            stereo_head::Latch();
#endif
        }
#endif
        stereo_trial::command.store(stereoFrame);
        if(stereoFrame%300==0) Log("stereo_progress frames=%llu left=%llu right=%llu submitted=%u",stereoFrame,stereoEyes[0],stereoEyes[1],stereoOverlay.submitted);
    }
#endif
    struct CallScope {
        inline static thread_local CallScope* top=nullptr;
        const void* object;CallScope* previous;bool accepted=true;
        explicit CallScope(const void* value):object(value),previous(top) {
            unsigned depth=0;
            for(auto p=top;p;p=p->previous) if(p->object==value || ++depth>=16) { accepted=false;break; }
            if(accepted) top=this;
        }
        ~CallScope() { if(accepted) top=previous; }
    };
public:
    explicit SwapchainWrapper(IDXGISwapChain4* owned,IUnknown* device):real(owned) {
        (void)device;
#ifdef FH4VR_FRAME_CAPTURE
        if(device) device->QueryInterface(IID_PPV_ARGS(&captureQueue));
#endif
    }
    ~SwapchainWrapper() {
#ifdef FH4VR_STEREO_TRIAL
        StopStereo("destroy");
#endif
#ifdef FH4VR_AER_PROBE
        StopAer("destroy");
#endif
#ifdef FH4VR_GPU_COPY
        StopGpu("destroy");
#endif
        real->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IDXGIObject) || iid==__uuidof(IDXGIDeviceSubObject) ||
           iid==__uuidof(IDXGISwapChain) || iid==__uuidof(IDXGISwapChain1) || iid==__uuidof(IDXGISwapChain2) ||
           iid==__uuidof(IDXGISwapChain3) || iid==__uuidof(IDXGISwapChain4)) {
            *out=static_cast<IDXGISwapChain4*>(this);AddRef();return S_OK;
        }
        Log("wrapper_unknown_iid %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            iid.Data1,iid.Data2,iid.Data3,iid.Data4[0],iid.Data4[1],iid.Data4[2],iid.Data4[3],iid.Data4[4],iid.Data4[5],iid.Data4[6],iid.Data4[7]);
        return E_NOINTERFACE; // Do not leak a second controlling IUnknown identity.
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=--references;if(!n) delete this;return n; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override { return real->SetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override { return real->SetPrivateDataInterface(a,b); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override { return real->GetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID a,void** b) override { return real->GetParent(a,b); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID a,void** b) override { return real->GetDevice(a,b); }
    HRESULT STDMETHODCALLTYPE Present(UINT sync,UINT flags) override {
        CallScope call(this);
        if(!call.accepted) { Log("wrapper_present_reentry wrapper=%p",this);return DXGI_ERROR_INVALID_CALL; }
        auto n=++presents;
#ifdef FH4VR_UI_LAYER_CAPTURE
        if(!(flags&DXGI_PRESENT_TEST)){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);ui_layer_capture::Poll(pid==GetCurrentProcessId());}
#endif
#ifdef FH4VR_STEREO_TRIAL
        DWORD stereoSavedError=GetLastError();bool stereoParticipating=BeforeStereo(flags);SetLastError(stereoSavedError);
#endif
#ifdef FH4VR_AER_PROBE
        unsigned aerPhase=CaptureAer(flags);
#endif
#ifdef FH4VR_FRAME_CAPTURE
        MaybeCapture(flags);
#ifdef FH4VR_GPU_COPY
        MaybeGpu(flags);
#endif
#endif
        if(n==1 || n%600==0) Describe(real,"wrapper_present_before");
        HRESULT hr=real->Present(sync,flags);DWORD saved=GetLastError();
#ifdef FH4VR_STEREO_TRIAL
        AfterStereo(stereoParticipating,hr);
#endif
#ifdef FH4VR_AER_PROBE
        AdvanceAer(aerPhase,hr);
#endif
        if(n==1 || n%600==0 || FAILED(hr)) Log("wrapper_present_return wrapper=%p native=%p count=%llu hr=%08lx",this,real,n,hr);
        SetLastError(saved);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT a,REFIID b,void** c) override { return real->GetBuffer(a,b,c); }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL a,IDXGIOutput* b) override { return real->SetFullscreenState(a,b); }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* a,IDXGIOutput** b) override { return real->GetFullscreenState(a,b); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* a) override { return real->GetDesc(a); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT a,UINT b,UINT c,DXGI_FORMAT d,UINT e) override {
#ifdef FH4VR_GPU_COPY
        StopGpu("resize");
#endif
#ifdef FH4VR_AER_PROBE
        StopAer("resize");
#endif
#ifdef FH4VR_STEREO_TRIAL
        StopStereo("resize");
#endif
        HRESULT hr=real->ResizeBuffers(a,b,c,d,e);DWORD saved=GetLastError();
        Log("wrapper_resize hr=%08lx size=%ux%u",hr,b,c);if(SUCCEEDED(hr)) Describe(real,"wrapper_resize_after");SetLastError(saved);return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* a) override { return real->ResizeTarget(a); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** a) override { return real->GetContainingOutput(a); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a) override { return real->GetFrameStatistics(a); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* a) override { return real->GetLastPresentCount(a); }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* a) override { return real->GetDesc1(a); }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a) override { return real->GetFullscreenDesc(a); }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* a) override { return real->GetHwnd(a); }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID a,void** b) override { return real->GetCoreWindow(a,b); }
    HRESULT STDMETHODCALLTYPE Present1(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* params) override {
        CallScope call(this);
        if(!call.accepted) { Log("wrapper_present1_reentry wrapper=%p",this);return DXGI_ERROR_INVALID_CALL; }
        auto n=++presents;
#ifdef FH4VR_UI_LAYER_CAPTURE
        if(!(flags&DXGI_PRESENT_TEST)){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);ui_layer_capture::Poll(pid==GetCurrentProcessId());}
#endif
#ifdef FH4VR_STEREO_TRIAL
        DWORD stereoSavedError=GetLastError();bool stereoParticipating=BeforeStereo(flags);SetLastError(stereoSavedError);
#endif
#ifdef FH4VR_AER_PROBE
        unsigned aerPhase=CaptureAer(flags);
#endif
#ifdef FH4VR_FRAME_CAPTURE
        MaybeCapture(flags);
#ifdef FH4VR_GPU_COPY
        MaybeGpu(flags);
#endif
#endif
        if(n==1 || n%600==0) Describe(real,"wrapper_present1_before");
        HRESULT hr=real->Present1(sync,flags,params);DWORD saved=GetLastError();
#ifdef FH4VR_STEREO_TRIAL
        AfterStereo(stereoParticipating,hr);
#endif
#ifdef FH4VR_AER_PROBE
        AdvanceAer(aerPhase,hr);
#endif
        if(n==1 || n%600==0 || FAILED(hr)) Log("wrapper_present1_return wrapper=%p native=%p count=%llu hr=%08lx",this,real,n,hr);
        SetLastError(saved);return hr;
    }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return real->IsTemporaryMonoSupported(); }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** a) override { return real->GetRestrictToOutput(a); }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* a) override { return real->SetBackgroundColor(a); }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* a) override { return real->GetBackgroundColor(a); }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION a) override { return real->SetRotation(a); }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* a) override { return real->GetRotation(a); }
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT a,UINT b) override { return real->SetSourceSize(a,b); }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* a,UINT* b) override { return real->GetSourceSize(a,b); }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT a) override { return real->SetMaximumFrameLatency(a); }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* a) override { return real->GetMaximumFrameLatency(a); }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return real->GetFrameLatencyWaitableObject(); }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* a) override { return real->SetMatrixTransform(a); }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* a) override { return real->GetMatrixTransform(a); }
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return real->GetCurrentBackBufferIndex(); }
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE a,UINT* b) override { return real->CheckColorSpaceSupport(a,b); }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE a) override { return real->SetColorSpace1(a); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT a,UINT b,UINT c,DXGI_FORMAT d,UINT e,const UINT* f,IUnknown* const* g) override {
#ifdef FH4VR_GPU_COPY
        StopGpu("resize");
#endif
#ifdef FH4VR_AER_PROBE
        StopAer("resize");
#endif
#ifdef FH4VR_STEREO_TRIAL
        StopStereo("resize");
#endif
        HRESULT hr=real->ResizeBuffers1(a,b,c,d,e,f,g);DWORD saved=GetLastError();
#ifdef FH4VR_FRAME_CAPTURE
        if(SUCCEEDED(hr) && g) {
            DXGI_SWAP_CHAIN_DESC1 desc{};real->GetDesc1(&desc);
            bool same=desc.BufferCount>0;
            for(UINT i=1;i<desc.BufferCount;++i) if(g[i]!=g[0]) same=false;
            captureQueue.Reset();if(same && g[0]) g[0]->QueryInterface(IID_PPV_ARGS(&captureQueue));
        }
#endif
        Log("wrapper_resize1 hr=%08lx size=%ux%u",hr,b,c);if(SUCCEEDED(hr)) Describe(real,"wrapper_resize1_after");SetLastError(saved);return hr;
    }
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE a,UINT b,void* c) override { return real->SetHDRMetaData(a,b,c); }
};
template<class T> static void WrapChain(T** output,IUnknown* device) {
    if(!output || !*output) return;
    IDXGISwapChain4* native=nullptr;
    if(FAILED((*output)->QueryInterface(IID_PPV_ARGS(&native)))) { Log("wrapper_requires_swapchain4");return; }
    auto wrapper=new(std::nothrow) SwapchainWrapper(native,device);
    if(!wrapper) { native->Release();return; }
    (*output)->Release();*output=wrapper;
    Log("wrapper_created wrapper=%p native=%p",wrapper,native);
}
