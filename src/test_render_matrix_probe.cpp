#define FH4VR_FRAME_ORDER_TRACE
#include "render_matrix_probe.h"
#include <thread>
#include <vector>
#include <atomic>
struct Target {float matrix[16];unsigned count=0;};
inline std::atomic<bool> failed{false};
void Original(void* object,const float* matrix){
    if(GetLastError()!=1234)failed=true;
    auto& target=*static_cast<Target*>(object);
    memcpy(target.matrix,matrix,64);++target.count;SetLastError(5678);
}
int main(){
    render_matrix_probe::original[0]=Original;
    render_matrix_probe::original[1]=Original;
    if(!frame_order_trace::recorder.Start(100))return 1;
    std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t)workers.emplace_back([t]{
        Target target{};float matrix[16]{};
        for(unsigned i=0;i<10000;++i){
            matrix[12]=float(i+t);
            SetLastError(1234);
            if(i%2)render_matrix_probe::Observe<0>(&target,matrix);
            else render_matrix_probe::Observe<1>(&target,matrix);
            if(GetLastError()!=5678||memcmp(matrix,target.matrix,64))failed=true;
        }
        if(target.count!=10000)failed=true;
    });
    for(auto& w:workers)w.join();
    frame_order_trace::recorder.Stop();
    if(failed||frame_order_trace::recorder.Count()!=40000||frame_order_trace::recorder.Dropped())return 2;
    Target target{};float matrix[16]{};
    SetLastError(1234);render_matrix_probe::Observe<0>(&target,matrix);
    if(failed||target.count!=1||GetLastError()!=5678||frame_order_trace::recorder.Count()!=40000)return 3;
    puts("PASS 40000 concurrent matrix setter calls; exact passthrough; LastError; trace disabled");
}
