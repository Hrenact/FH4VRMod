#include "vtable_probe.cpp"
#include <thread>
#include <vector>
static void* __fastcall Fixture(void* camera,void* output) {
    double* values=static_cast<double*>(output);
    for (unsigned i=0;i<4;++i) values[i]=static_cast<float>(i)+(camera ? 100.0f : 0.0f);
    SetLastError(1234);
    return output;
}
int main() {
    ProbeTelemetry state{}; telemetry=&state; original=&Fixture;
    auto table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!table) return 1;
    table[51]=reinterpret_cast<void*>(0x1234); table[52]=reinterpret_cast<void*>(&Fixture); table[53]=reinterpret_cast<void*>(0x5678);
    DWORD old=0; if (!VirtualProtect(table,4096,PAGE_READONLY,&old)) return 2;
    slot=table+52;
    if (ReplaceSlot(slot,nullptr,reinterpret_cast<void*>(&ObserveMatrix))!=ERROR_REVISION_MISMATCH) return 3;
    if (ReplaceSlot(slot,reinterpret_cast<void*>(&Fixture),reinterpret_cast<void*>(&ObserveMatrix))) return 4;
    std::atomic<unsigned> errors{0};
    std::vector<std::thread> threads;
    for (unsigned t=0;t<4;++t) threads.emplace_back([&] {
        for (unsigned j=0;j<10000;++j) {
            alignas(16) double output[4]{};
            auto fn=reinterpret_cast<MatrixFunction>(*slot);
            void* result=fn(table,output);
            if (result!=output || GetLastError()!=1234) ++errors;
            for (unsigned i=0;i<4;++i) if(output[i]!=static_cast<float>(i)+100.0f) ++errors;
        }
    });
    for(auto& thread:threads) thread.join();
    if (errors || state.entered!=40000 || state.returned!=40000) return 5;
    if (ReplaceSlot(slot,reinterpret_cast<void*>(&ObserveMatrix),reinterpret_cast<void*>(&Fixture))) return 6;
    MEMORY_BASIC_INFORMATION info{}; VirtualQuery(table,&info,sizeof(info));
    if(info.Protect!=PAGE_READONLY || table[51]!=reinterpret_cast<void*>(0x1234) || table[53]!=reinterpret_cast<void*>(0x5678) || table[52]!=reinterpret_cast<void*>(&Fixture)) return 7;
    if (!VirtualProtect(table,4096,PAGE_EXECUTE_READWRITE,&old)) return 8;
    if (ReplaceSlot(slot,reinterpret_cast<void*>(&Fixture),reinterpret_cast<void*>(&ObserveMatrix))) return 9;
    alignas(16) double output[4]{};
    if (reinterpret_cast<MatrixFunction>(*slot)(table,output)!=output || output[3]!=103.0f) return 10;
    if (ReplaceSlot(slot,reinterpret_cast<void*>(&ObserveMatrix),reinterpret_cast<void*>(&Fixture))) return 11;
    VirtualQuery(table,&info,sizeof(info));
    if (info.Protect!=PAGE_EXECUTE_READWRITE || table[51]!=reinterpret_cast<void*>(0x1234) || table[53]!=reinterpret_cast<void*>(0x5678)) return 12;
    VirtualFree(table,0,MEM_RELEASE);
    puts("PASS: 40000 concurrent calls preserve four-double position, return pointer and last error; slot restoration and neighbors verified; READONLY and EXECUTE_READWRITE permissions preserved.");
    return 0;
}
