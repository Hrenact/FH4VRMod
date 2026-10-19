#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
struct ProbeTelemetry {
    volatile LONG sequence;
    DWORD enabled;
    DWORD pid;
    DWORD status;
    unsigned long long calls;
    unsigned long long camera;
    unsigned long long tick;
    float position[4];
    float fov;
    float yaw;
    volatile LONG64 entered;
    volatile LONG64 returned;
    volatile LONG64 matched;
    volatile LONG64 readFailures;
    volatile LONG64 lastEntry;
    unsigned long long target;
    unsigned long long trampoline;
    unsigned long long detour;
    DWORD targetProtection;
    DWORD phase; // 1=mapping ready, 2=trampoline ready, 3=enabled, 4=disabled
    unsigned char originalBytes[32];
    unsigned char patchedBytes[32];
    unsigned char trampolineBytes[32];
};
static_assert(sizeof(ProbeTelemetry)==232);
static_assert(offsetof(ProbeTelemetry,calls)==16);
static_assert(offsetof(ProbeTelemetry,position)==40);
static_assert(offsetof(ProbeTelemetry,entered)==64);
static_assert(offsetof(ProbeTelemetry,originalBytes)==136);
