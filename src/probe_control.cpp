#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

static uintptr_t ModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W module{sizeof(module)};
    uintptr_t result = 0;
    if (Module32FirstW(snapshot, &module)) do {
        if (!_wcsicmp(module.szModule, name)) { result = reinterpret_cast<uintptr_t>(module.modBaseAddr); break; }
    } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    return result;
}

static bool Invoke(HANDLE process, uintptr_t function, void* argument, DWORD& result) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function), argument, 0, nullptr);
    if (!thread) { printf("CreateRemoteThread failed: %lu\n", GetLastError()); return false; }
    const DWORD wait = WaitForSingleObject(thread, 10000);
    bool ok = wait == WAIT_OBJECT_0 && GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    if (!ok) printf("Remote call did not finish successfully (wait=%lu); leaving remote allocations intact.\n", wait);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) { puts("probe_control <pid> <absolute camera_probe.dll path> start|snapshot|stop|load-test"); return 2; }
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    const bool loadTest = !_wcsicmp(argv[3], L"load-test");
    const char* action = !_wcsicmp(argv[3], L"start") ? "StartProbe" : (!_wcsicmp(argv[3], L"snapshot") || loadTest) ? "SnapshotProbe" : !_wcsicmp(argv[3], L"stop") ? "StopProbe" : nullptr;
    if (!action || !ModuleBase(pid, loadTest ? L"probe_host.exe" : L"ForzaHorizon4.exe")) { puts("Invalid action or target executable."); return 2; }
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) { printf("OpenProcess failed: %lu\n", GetLastError()); return 3; }
    wchar_t full[MAX_PATH];
    if (!GetFullPathNameW(argv[2], MAX_PATH, full, nullptr)) { CloseHandle(process); return 4; }
    const wchar_t* name = wcsrchr(full, L'\\');
    name = name ? name + 1 : full;
    uintptr_t remote = ModuleBase(pid, name);
    if (!remote && strcmp(action, "StartProbe") && !loadTest) { puts("Probe is not loaded."); CloseHandle(process); return 5; }
    if (!remote) {
        const SIZE_T bytes = (wcslen(full) + 1) * sizeof(wchar_t);
        void* path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        SIZE_T written = 0;
        if (!path || !WriteProcessMemory(process, path, full, bytes, &written) || written != bytes) { CloseHandle(process); return 6; }
        const auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        MEMORY_BASIC_INFORMATION info{};
        VirtualQuery(reinterpret_cast<void*>(load), &info, sizeof(info));
        wchar_t ownerPath[MAX_PATH];
        GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), ownerPath, MAX_PATH);
        const wchar_t* ownerName = wcsrchr(ownerPath, L'\\');
        uintptr_t owner = ModuleBase(pid, ownerName ? ownerName + 1 : ownerPath);
        if (!owner) { CloseHandle(process); return 7; }
        uintptr_t remoteLoad = owner + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(info.AllocationBase);
        DWORD result;
        if (!Invoke(process, remoteLoad, path, result)) { CloseHandle(process); return 8; }
        printf("LoadLibrary remote-thread exit=0x%08lx process_wait=%lu\n", result, WaitForSingleObject(process, 0));
        VirtualFreeEx(process, path, 0, MEM_RELEASE);
        remote = ModuleBase(pid, name);
        if (!remote) { puts("DLL was not loaded."); CloseHandle(process); return 9; }
    }
    HMODULE local = LoadLibraryExW(full, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) { CloseHandle(process); return 10; }
    auto exported = GetProcAddress(local, action);
    if (!exported) { FreeLibrary(local); CloseHandle(process); return 11; }
    uintptr_t remoteExport = remote + reinterpret_cast<uintptr_t>(exported) - reinterpret_cast<uintptr_t>(local);
    DWORD result = 0;
    bool ok = Invoke(process, remoteExport, nullptr, result);
    printf("%s result=%lu remote_module=0x%llx\n", action, result, static_cast<unsigned long long>(remote));
    FreeLibrary(local);
    CloseHandle(process);
    return ok && result == 0 ? 0 : 12;
}
