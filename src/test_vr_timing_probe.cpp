#define FH4VR_TIMING_PROBE
#include "vr_timing_probe.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <thread>
static std::string emitted;
static void Sink(const char* text){emitted=text;SetLastError(0xabcd);}
int main(){using namespace vr_timing;
    unsigned calls=0;SetLastError(123);
    auto result=Measure(Stage::GetBuffer,[&]{++calls;assert(GetLastError()==123);SetLastError(456);return HRESULT(0x81234567);});
    assert(calls==1 && result==HRESULT(0x81234567) && GetLastError()==456 && !current);
    Window w;SetLastError(321);
    {Frame frame(w,Sink);assert(GetLastError()==321 && current==&w);frame.Active(true);
        Measure(Stage::CopyEnqueue,[&]{++calls;SetLastError(654);});assert(GetLastError()==654);
        bool caught=false;try{Measure(Stage::SubmitLeft,[&]()->void{++calls;SetLastError(789);throw std::runtime_error("native");});}
        catch(const std::runtime_error&){caught=true;assert(GetLastError()==789);}assert(caught);
        {Frame nested(w,Sink);assert(!current);Measure(Stage::GetBuffer,[&]{++calls;});}
        assert(current==&w);
        // Another thread skips timing rather than contending for a live frame.
        std::thread t([&]{Frame concurrent(w,Sink);assert(!current);Measure(Stage::GetBuffer,[]{return S_OK;});});t.join();
        SetLastError(987);
    }
    assert(!current && GetLastError()==987 && w.frames==1 && w.vrFrames==1 && calls==4);
    assert(w.metrics[static_cast<unsigned>(Stage::CopyEnqueue)].count==1);
    assert(w.metrics[static_cast<unsigned>(Stage::SubmitLeft)].count==1);
    assert(!w.metrics[static_cast<unsigned>(Stage::GetBuffer)].count);
    w.Reset();w.Add(Stage::NativePresent,2);w.Add(Stage::NativePresent,3);w.Add(Stage::NativePresent,-1);
    w.Finish(100,1101,true,Sink);assert(emitted.find("present_n=2 present_sum_ms=5.000000 present_max_ms=3.000000")!=std::string::npos);
    assert(emitted.find("frames=1 vr_frames=1")!=std::string::npos && !w.frames);
    {Frame again(w,Sink);assert(current==&w);}assert(!current);
    puts("PASS timing: exactly-once calls, HRESULT/LastError/native exceptions, nested/concurrent skip, scope restore, window aggregation");
}
