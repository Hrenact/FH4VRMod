#include "frame_order_trace.h"
#include <thread>
#include <vector>
#include <memory>
#include <cstring>
int main(){
    auto r=std::make_unique<frame_order_trace::Recorder>();
    if(!r->Start(100) || r->Start(101))return 1;
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t)workers.emplace_back([&,t]{for(unsigned i=0;i<10000;++i){
        SetLastError(1234);r->Record(frame_order_trace::MatrixEnter,t+1);
        if(GetLastError()!=1234)failed=true;
    }});
    for(auto& w:workers)w.join();
    if(failed || r->Count()!=40000 || r->Dropped())return 2;
    r->Poll(2099);r->Record(frame_order_trace::Publish,5,nullptr,nullptr,1234567890123ull,9876543210987ull);
    r->Poll(2100);r->Record(frame_order_trace::Publish,6);
    if(r->Count()!=40001 || r->Start(2200))return 3; // Unsaved data cannot be erased.
    wchar_t path[128]{};swprintf_s(path,L"reports\\frame-order-test-%lu.csv",GetCurrentProcessId());
    if(r->Save(path)!=S_OK || r->Save(path)!=S_FALSE)return 4;
    FILE* f=nullptr;if(_wfopen_s(&f,path,L"r")||!f)return 5;
    char line[512];unsigned lines=0;bool payload=false;while(fgets(line,sizeof(line),f)){++lines;if(strstr(line,",1234567890123,9876543210987"))payload=true;}fclose(f);
    if(!payload)return 10;
    float m[16]{};auto h=frame_order_trace::MatrixFingerprint(m);m[12]=1;
    if(h==frame_order_trace::MatrixFingerprint(m))return 11;
    if(lines!=40003)return 6;
    DeleteFileW(path);
    if(!r->Start(3000))return 7;
    for(unsigned i=0;i<131080;++i)r->Record(frame_order_trace::UiEnter,7);
    r->Stop();if(r->Count()!=131080 || r->Dropped()!=8)return 8;
    if(r->Save(path)!=S_OK)return 9;DeleteFileW(path);
    puts("PASS concurrent trace records; LastError preservation; timed stop; bounded overflow; deferred CSV; rearm only after save");
}
