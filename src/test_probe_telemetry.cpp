#include "probe_telemetry.h"
#include <cstdio>
int main() {
    wchar_t name[80];
    swprintf_s(name,L"Local\\FH4VR_CameraProbe_%lu",GetCurrentProcessId());
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ProbeTelemetry),name);
    if (!mapping) return 1;
    auto state=static_cast<ProbeTelemetry*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ProbeTelemetry)));
    if (!state) return 2;
    ZeroMemory(state,sizeof(*state));
    state->enabled=1; state->pid=GetCurrentProcessId(); state->calls=17;
    state->camera=0x123456789abcdef0ULL; state->tick=1234;
    state->position[0]=1.25f; state->fov=0.75f; state->yaw=-1.5f;
    state->entered=19; state->returned=17; state->matched=16; state->readFailures=2;
    state->lastEntry=0x123456789abcdef0ULL;
    state->target=0x140123456; state->trampoline=0x140124000; state->detour=0x180012300;
    state->targetProtection=PAGE_EXECUTE_READ; state->phase=3;
    for (unsigned i=0;i<32;++i) { state->originalBytes[i]=static_cast<unsigned char>(i); state->patchedBytes[i]=static_cast<unsigned char>(i+32); state->trampolineBytes[i]=static_cast<unsigned char>(i+64); }
    InterlockedExchange(&state->sequence,2);
    Sleep(8000);
    UnmapViewOfFile(state); CloseHandle(mapping);
    return 0;
}
