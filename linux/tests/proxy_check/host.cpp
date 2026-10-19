// Stand-in game exe for run_proxy_check.py: statically imports DINPUT8 (so Wine loads the proxy from the exe dir),
// calls DirectInput8Create, then shows a "Dishonored" window until <exe dir>\host-exit appears or the timeout passes.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::wstring exe_dir() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
        if (n == 0) return L".";
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

int main(int argc, char** argv) {
    const unsigned long timeout_s = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 90;

    IDirectInput8W* input = nullptr;
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8W,
                                    reinterpret_cast<void**>(&input), nullptr);
    std::printf("DirectInput8Create hr=0x%08lx iface=%d pid=%lu\n", static_cast<unsigned long>(hr),
                input != nullptr ? 1 : 0, GetCurrentProcessId());
    std::fflush(stdout);
    if (FAILED(hr) || input == nullptr) return 2;

    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"D2VRProxyCheckHost";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Dishonored 2 proxy_check", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 320, 200, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        std::printf("CreateWindowExW failed %lu\n", GetLastError());
        std::fflush(stdout);
        input->Release();
        return 4;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    const std::wstring exit_file = exe_dir() + L"\\host-exit";
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout_s) * 1000;
    SetTimer(nullptr, 0, 200, nullptr);
    int rc = 3;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER && msg.hwnd == nullptr) {
            if (GetFileAttributesW(exit_file.c_str()) != INVALID_FILE_ATTRIBUTES) {
                rc = 0;
                break;
            }
            if (GetTickCount64() >= deadline) break;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    std::printf("host exit %d\n", rc);
    std::fflush(stdout);
    DestroyWindow(hwnd);
    input->Release();
    return rc;
}
