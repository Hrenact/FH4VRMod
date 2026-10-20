#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <utility>
#include <atomic>

namespace vr_timing {
enum class Stage : unsigned { NativePresent, WaitPoses, GetBuffer, CopyEnqueue, SubmitLeft, SubmitRight, Handoff, Seal,
    PoseAge, PairInterval, Wrapper, OutsideGap, Count };
inline constexpr const char* Names[]={"present","wait_poses","get_buffer","copy_cpu","submit_left","submit_right","handoff","seal",
    "pose_age","pair_interval","wrapper","outside_gap"};
inline double Now() {
    const DWORD saved=GetLastError();
    static const double frequency=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return static_cast<double>(f.QuadPart);}();
    LARGE_INTEGER q{};QueryPerformanceCounter(&q);SetLastError(saved);
    return frequency>0?static_cast<double>(q.QuadPart)*1000.0/frequency:0;
}
struct Metric {uint64_t count=0;double sum=0,max=0;void Add(double ms){if(ms<0)return;++count;sum+=ms;if(ms>max)max=ms;}};
struct Window {
    std::atomic_flag busy=ATOMIC_FLAG_INIT;
    Metric metrics[static_cast<unsigned>(Stage::Count)]{};
    double start=0,lastEnd=0;uint64_t frames=0,vrFrames=0;
    void Add(Stage s,double ms){metrics[static_cast<unsigned>(s)].Add(ms);}
    void Reset(){for(auto& m:metrics)m={};frames=vrFrames=0;start=0;lastEnd=0;}
    template<class Sink> void Finish(double begin,double end,bool vr,Sink sink){
        if(!start)start=begin;++frames;if(vr)++vrFrames;Add(Stage::Wrapper,end-begin);lastEnd=end;
        if(end-start<1000)return;
        char line[3072]{};int used=snprintf(line,sizeof(line),"timing_window frames=%llu vr_frames=%llu span_ms=%.3f",
            static_cast<unsigned long long>(frames),static_cast<unsigned long long>(vrFrames),end-start);
        for(unsigned i=0;i<static_cast<unsigned>(Stage::Count) && used>0 && used<static_cast<int>(sizeof(line));++i){const auto& m=metrics[i];
            int added=snprintf(line+used,sizeof(line)-static_cast<size_t>(used)," %s_n=%llu %s_sum_ms=%.6f %s_max_ms=%.6f",Names[i],
                static_cast<unsigned long long>(m.count),Names[i],m.sum,Names[i],m.max);
            if(added<0)break;used+=added;
        }
        sink(line);for(auto& m:metrics)m={};frames=vrFrames=0;start=end;
    }
};
inline thread_local Window* current=nullptr;
inline void Record(Stage s,double ms){if(current)current->Add(s,ms);}
class Timer {
    Window* window;Stage stage;double begin;
public:
    explicit Timer(Stage s):window(current),stage(s),begin(window?Now():0){}
    ~Timer(){const DWORD saved=GetLastError();if(window)window->Add(stage,Now()-begin);SetLastError(saved);}
};
template<class F> decltype(auto) Measure(Stage stage,F&& call){
#ifdef FH4VR_TIMING_PROBE
    Timer timer(stage);
#else
    (void)stage;
#endif
    return std::forward<F>(call)();
}
class Frame {
    Window& window;Window* previous;double begin;bool vr=false;
    bool owned;
    using Sink=void(*)(const char*);Sink sink;
public:
    Frame(Window& w,Sink out):window(w),previous(current),begin(Now()),owned(!w.busy.test_and_set(std::memory_order_acquire)),sink(out){
        const DWORD saved=GetLastError();if(owned && w.lastEnd)w.Add(Stage::OutsideGap,begin-w.lastEnd);current=owned?&w:nullptr;SetLastError(saved);
    }
    void Active(bool value){vr=value;}
    ~Frame(){const DWORD saved=GetLastError();current=previous;if(owned){window.Finish(begin,Now(),vr,sink);window.lastEnd=Now();window.busy.clear(std::memory_order_release);}SetLastError(saved);}
};
} // namespace vr_timing
