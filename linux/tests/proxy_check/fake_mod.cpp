// Fake d2vr_render_probe_arm_v999.dll for run_proxy_check.py: the four exports of src/dxgi_probe.cpp with the same
// signatures. StartProbe records "<own pid> <cfg pid>" in <root>\artifacts\fake-start.txt and blocks until StopProbe.
#include <windows.h>

#include <atomic>
#include <fstream>
#include <string>

#ifndef FAKE_CAPS
#define FAKE_CAPS 524287
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

std::atomic<bool> g_in_start{false};
std::atomic<HANDLE> g_stop_event{nullptr};

std::wstring parent(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring module_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), &path[0], static_cast<DWORD>(path.size()));
        if (n == 0) return std::wstring();
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI ProbeStatus(void*) { return 0; }

extern "C" __declspec(dllexport) DWORD WINAPI ProbeCapabilities(void*) { return FAKE_CAPS; }

extern "C" __declspec(dllexport) DWORD WINAPI StopProbe(void*) {
    if (!g_in_start.load()) return 2;
    SetEvent(g_stop_event.load());
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI StartProbe(void*) {
    if (g_in_start.exchange(true)) return 2;
    HANDLE ev = g_stop_event.load();
    if (ev == nullptr) {
        ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_stop_event.store(ev);
    }
    ResetEvent(ev);

    const std::wstring root = parent(parent(module_path()));
    std::string cfg_pid;
    {
        std::ifstream cfg(root + L"\\build\\dxgi-probe.cfg");
        cfg >> cfg_pid;
    }
    // Write then rename, so the test never reads a half-written marker.
    const std::wstring marker = root + L"\\artifacts\\fake-start.txt";
    {
        std::ofstream out(marker + L".tmp", std::ios::binary | std::ios::trunc);
        out << GetCurrentProcessId() << ' ' << cfg_pid << '\n';
    }
    MoveFileExW((marker + L".tmp").c_str(), marker.c_str(), MOVEFILE_REPLACE_EXISTING);

    DWORD rc = WaitForSingleObject(ev, 60000) == WAIT_OBJECT_0 ? 0 : 4;
    g_in_start.store(false);
    return rc;
}
