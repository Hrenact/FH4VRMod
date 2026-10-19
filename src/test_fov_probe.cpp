#define FH4VR_FOV_PROBE
#define FH4VR_EYE_OFFSET
#define FH4VR_EYE_POSITION
#include "vtable_probe.cpp"
#include <thread>
#include <vector>
static float __fastcall FovFixture(void* camera) {SetLastError(2468);return *static_cast<float*>(camera);}
int main() {
    ProbeTelemetry state{};telemetry=&state;originalFov=FovFixture;
    std::atomic<unsigned> errors{0};std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t) workers.emplace_back([&,t] {
        float value=.7f+t*.1f;
        for(unsigned n=0;n<10000;++n) {
            float result=ObserveFov(&value);
            if(memcmp(&result,&value,sizeof(float)) || GetLastError()!=2468) ++errors;
        }
    });
    for(auto& worker:workers) worker.join();
    if(errors || fovCalls.load()!=40000 || state.sequence%2) return 1;
    puts("PASS 40000 FOV callback calls preserve exact float return and last error");
}
