// dinput8.dll proxy for Dishonored 2 under Proton. It forwards DirectInput8Create to the system dinput8 and, on a
// worker thread, does in-process what launcher/Probe.cs Session does: wait for the game window and a start trigger,
// write build\dxgi-probe.cfg, load the unchanged mod DLL from the mod home and call StartProbe. A stop-watcher thread
// turns build\linux-stop into StopProbe. Fail safe: any failure is logged and the proxy idles; DirectInput8Create
// keeps forwarding.
#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "proxy_core.hpp"

using namespace d2vr_linux;

// Exported as DirectInput8Create by dinput8.def.
extern "C" HRESULT WINAPI proxy_DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

namespace {

using ProbeFn = DWORD(WINAPI*)(void*);
using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

HMODULE g_self = nullptr;
// Read through a volatile pointer so the marker stays a contiguous string in .rdata (modhome.is_our_proxy greps it).
const char* volatile g_marker = kProxyMarker;
const char* volatile g_sim_marker = kSimMarker;

// Paths below are written by the worker before it starts the stop watcher and are read-only afterwards.
std::wstring g_build_dir;
std::atomic<ProbeFn> g_stop_probe{nullptr};
// True from a start trigger (or settle timeout) until that attempt ends, StartProbe included.
std::atomic<bool> g_attempt_active{false};

// ---------------- text and files

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring num(unsigned long v) { return std::to_wstring(v); }

std::wstring module_path(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(module, &path[0], static_cast<DWORD>(path.size()));
        if (n == 0) return std::wstring();
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring parent(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool is_dir(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool read_file(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[4096];
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof buf, &got, nullptr)) {
            ok = false;
            break;
        }
        if (got == 0) break;
        out.append(buf, got);
    }
    CloseHandle(h);
    return ok;
}

// CREATE_ALWAYS replaces the file; OPEN_ALWAYS + FILE_APPEND_DATA appends.
bool write_file(const std::wstring& path, const std::string& data, bool append) {
    HANDLE h = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &put, nullptr) && put == data.size();
    CloseHandle(h);
    return ok;
}

// ---------------- log: <modhome>\artifacts\linux-proxy.log, one ISO-8601-stamped line per event

SRWLOCK g_log_lock = SRWLOCK_INIT;
struct LogLock {
    LogLock() { AcquireSRWLockExclusive(&g_log_lock); }
    ~LogLock() { ReleaseSRWLockExclusive(&g_log_lock); }
    LogLock(const LogLock&) = delete;
    LogLock& operator=(const LogLock&) = delete;
};
std::wstring g_log_path;  // empty until the worker opens the log; guarded by g_log_lock
std::string g_log_pending;  // lines logged before that (e.g. by DirectInput8Create); guarded by g_log_lock

void log_line(const std::wstring& msg) noexcept {
    try {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[40];
        swprintf_s(stamp, L"%04u-%02u-%02uT%02u:%02u:%02u.%03u ", static_cast<unsigned>(t.wYear),
                   static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay), static_cast<unsigned>(t.wHour),
                   static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond),
                   static_cast<unsigned>(t.wMilliseconds));
        const std::wstring line = stamp + msg + L"\r\n";
        OutputDebugStringW((L"d2vr-linux proxy: " + line).c_str());
        const std::string bytes = to_utf8(line);  // allocate before taking the lock
        LogLock lock;
        if (g_log_path.empty())
            g_log_pending += bytes;
        else
            write_file(g_log_path, bytes, true);
    } catch (...) {
    }
}

// Rotates any earlier log to linux-proxy.previous.log, then writes first_line and whatever was logged before.
void open_log(const std::wstring& artifacts, const std::wstring& first_line) {
    const std::wstring path = artifacts + L"\\" + kLogName;
    if (exists(path)) MoveFileExW(path.c_str(), (artifacts + L"\\linux-proxy.previous.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    std::wstring log_path = path;  // copied outside the lock; swapped in without allocating
    std::string pending;
    {
        LogLock lock;
        g_log_path.swap(log_path);
        pending.swap(g_log_pending);
    }
    log_line(first_line);
    if (!pending.empty()) {
        LogLock lock;
        write_file(path, pending, true);
    }
}

// ---------------- DirectInput8Create forward

INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;
DirectInput8CreateFn g_real_create = nullptr;

BOOL CALLBACK resolve_real_dinput8(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH + 16];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        log_line(L"DirectInput8Create: GetSystemDirectoryW failed (error " + num(GetLastError()) + L")");
        return TRUE;
    }
    wcscat_s(path, L"\\dinput8.dll");
    HMODULE real = LoadLibraryW(path);
    if (real == nullptr) {
        log_line(L"DirectInput8Create: cannot load " + std::wstring(path) + L" (error " + num(GetLastError()) + L")");
        return TRUE;
    }
    if (real == g_self) {
        log_line(L"DirectInput8Create: " + std::wstring(path) + L" resolves to this proxy");
        FreeLibrary(real);
        return TRUE;
    }
    auto fn = reinterpret_cast<DirectInput8CreateFn>(GetProcAddress(real, "DirectInput8Create"));
    if (fn == nullptr || fn == &proxy_DirectInput8Create) {
        log_line(L"DirectInput8Create: no usable DirectInput8Create in " + std::wstring(path));
        return TRUE;
    }
    g_real_create = fn;
    return TRUE;
}

// ---------------- stop watcher: build\linux-stop -> StopProbe

DWORD WINAPI stop_watcher(void*) {
    try {
        const std::wstring stop = g_build_dir + L"\\" + kStopRequest;
        bool delete_error_logged = false;
        bool retry_logged = false;  // one "retrying" line per pending request
        // Consumes the request once it is handled, so it acts only once per request file.
        auto consume = [&](const std::wstring& message) {
            retry_logged = false;
            if (!DeleteFileW(stop.c_str())) {
                if (!delete_error_logged) log_line(L"cannot remove linux-stop (error " + num(GetLastError()) + L")");
                delete_error_logged = true;
                return;
            }
            log_line(message);
        };
        for (;;) {
            Sleep(500);
            if (!exists(stop)) {
                retry_logged = false;
                continue;
            }
            // Read before calling StopProbe: a request seen while no attempt runs is for no session and is dropped,
            // so it cannot stop the next session the moment it starts.
            const bool attempt_active = g_attempt_active.load();
            const ProbeFn stop_probe = g_stop_probe.load();
            if (stop_probe == nullptr && !attempt_active) {
                consume(L"StopProbe returned none (no session)");
                continue;
            }
            if (stop_probe != nullptr) {
                const DWORD code = stop_probe(nullptr);
                if (code == 0 || !attempt_active) {
                    consume(L"StopProbe returned " + num(code));
                    continue;
                }
                if (!retry_logged)
                    log_line(L"StopProbe returned " + num(code) + L"; linux-stop kept, retrying while the session starts");
            } else if (!retry_logged) {
                log_line(L"linux-stop kept until the mod DLL is loaded");
            }
            retry_logged = true;
        }
    } catch (const std::exception& e) {
        log_line(L"stop watcher stopped: " + from_utf8(e.what()));
    } catch (...) {
        log_line(L"stop watcher stopped: unexpected exception");
    }
    return 0;
}

// ---------------- worker

BOOL CALLBACK find_game_window_proc(HWND hwnd, LPARAM found) {
    try {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
        wchar_t title[256];
        // InternalGetWindowText does not send WM_GETTEXT, so a busy game thread cannot block the worker here.
        int n = InternalGetWindowText(hwnd, title, 256);
        if (n > 0 && is_game_window_title(std::wstring(title, static_cast<size_t>(n)))) {
            *reinterpret_cast<HWND*>(found) = hwnd;
            return FALSE;
        }
    } catch (...) {
    }
    return TRUE;
}

HWND find_game_window() {
    HWND found = nullptr;
    EnumWindows(find_game_window_proc, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool game_window_present() { return find_game_window() != nullptr; }

// WM_KEYDOWN, 80 ms, WM_KEYUP to the game window (launcher GameKeys.Post).
bool post_key(int vk) {
    HWND hwnd = find_game_window();
    if (hwnd == nullptr) return false;
    const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), 0));
    PostMessageW(hwnd, WM_KEYDOWN, static_cast<WPARAM>(vk), 1 | (scan << 16));
    Sleep(80);
    PostMessageW(hwnd, WM_KEYUP, static_cast<WPARAM>(vk), 1 | (scan << 16) | (3LL << 30));
    return true;
}

// Opt-in (build\linux-autocontinue): as launcher/Probe.cs Launch does for a game it started, post Enter (title -> Campaign ->
// Continue, then any load prompt) once the session is up, until the mod reports gameplay. Ends with the attempt.
std::wstring g_artifacts_dir;
DWORD WINAPI autocontinue_thread(void* arg) {
    try {
        const unsigned delay = static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(arg));
        for (unsigned waited = 0; waited < delay * 10; ++waited) {
            if (!g_attempt_active.load()) return 0;
            Sleep(100);
        }
        // Menus animate and Wine may drop an early key: keep posting every 4 s (up to 12) until the mod reports gameplay.
        for (int i = 0; i < 12; ++i) {
            if (!g_attempt_active.load()) return 0;
            std::string status;
            read_file(g_artifacts_dir + L"\\xr-live-status.json", status);
            if (status_says_gameplay(status)) {
                log_line(L"autocontinue: gameplay reached; no more keys sent");
                return 0;
            }
            if (!post_key(VK_RETURN)) {
                log_line(L"autocontinue: game window not found");
                return 0;
            }
            const auto mask_at = status.find("\"logic_mask\":");
            std::wstring mask = L"?";
            if (mask_at != std::string::npos) {
                mask.clear();
                for (size_t k = mask_at + 13; k < status.size() && status[k] >= '0' && status[k] <= '9'; ++k) mask += static_cast<wchar_t>(status[k]);
            }
            log_line(L"autocontinue: posted Enter " + num(static_cast<unsigned long>(i + 1)) + L" (logic_mask " + mask + L")");
            for (int w = 0; w < 40; ++w) {
                if (!g_attempt_active.load()) return 0;
                Sleep(100);
            }
        }
    } catch (...) {
        log_line(L"autocontinue stopped: unexpected exception");
    }
    return 0;
}

std::vector<std::wstring> mod_dll_candidates(const std::wstring& build) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((build + L"\\d2vr_render_probe_arm_v*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return names;
}

std::wstring expand_env(const wchar_t* text) {
    DWORD n = ExpandEnvironmentStringsW(text, nullptr, 0);
    if (n == 0) return std::wstring();
    std::wstring out(n, L'\0');
    n = ExpandEnvironmentStringsW(text, &out[0], n);
    if (n == 0) return std::wstring();
    out.resize(n - 1);
    return out;
}

// GameKeys.PrepareConfig (launcher/Probe.cs:546-558), best effort.
void append_focus_cvars() {
    const std::wstring cfg_path =
        expand_env(L"%USERPROFILE%\\Saved Games\\Arkane Studios\\Dishonored2\\base\\dishonored2Config.cfg");
    std::string cfg;
    if (cfg_path.empty() || !exists(cfg_path) || !read_file(cfg_path, cfg)) {
        log_line(L"dishonored2Config.cfg not found");
        return;
    }
    const std::string add = focus_cvars_to_append(cfg);
    if (add.empty())
        log_line(L"focus cvars present");
    else if (write_file(cfg_path, add, true))
        log_line(L"focus cvars appended");
    else
        log_line(L"focus cvars not appended (error " + num(GetLastError()) + L")");
}

struct ModExports {
    ProbeFn status = nullptr, capabilities = nullptr, start = nullptr, stop = nullptr;
    bool complete() const { return status && capabilities && start && stop; }
};

void run_worker() {
    const std::wstring exe = module_path(nullptr);
    std::string ini;
    read_file(parent(exe) + L"\\" + kIniName, ini);  // a missing ini means the default mod home
    const std::wstring home = mod_home_from_ini(from_utf8(ini));
    if (!is_dir(home)) {
        OutputDebugStringW((L"d2vr-linux proxy: mod home " + home + L" does not exist; idle\n").c_str());
        return;
    }
    const std::wstring build = home + L"\\build";
    const std::wstring artifacts = home + L"\\artifacts";
    CreateDirectoryW(artifacts.c_str(), nullptr);
    const DWORD pid = GetCurrentProcessId();
    open_log(artifacts, L"proxy " + from_utf8(g_marker) + L" " + from_utf8(g_sim_marker) + L" pid=" + num(pid) + L" exe=" + exe + L" modhome=" + home);

    append_focus_cvars();

    const std::wstring start_trigger = build + L"\\" + kStartTrigger;
    if (DeleteFileW(start_trigger.c_str())) log_line(L"removed stale linux-start");
    if (DeleteFileW((build + L"\\" + kStopRequest).c_str())) log_line(L"removed stale linux-stop");

    g_build_dir = build;
    HANDLE watcher = CreateThread(nullptr, 0, stop_watcher, nullptr, 0, nullptr);
    if (watcher != nullptr)
        CloseHandle(watcher);
    else
        log_line(L"cannot start the stop watcher (error " + num(GetLastError()) + L")");

    std::string knob;
    read_file(build + L"\\" + kSettleKnob, knob);
    const unsigned settle = settle_seconds_from_knob(knob);

    bool first_session = true;  // only the first session may start on the settle timeout
    HMODULE mod = nullptr;
    std::wstring loaded_name;
    ModExports ex;
    for (;;) {
        g_attempt_active.store(false);
        while (!game_window_present()) Sleep(500);
        const ULONGLONG found_at = GetTickCount64();
        if (first_session)
            log_line(L"game window found; waiting for start (trigger or " + num(settle) + L" s)");
        else
            log_line(L"waiting for start (trigger)");
        for (;;) {
            if (DeleteFileW(start_trigger.c_str())) {
                log_line(L"start trigger");
                break;
            }
            if (first_session && GetTickCount64() - found_at >= static_cast<ULONGLONG>(settle) * 1000) {
                log_line(L"settle timeout");
                break;
            }
            Sleep(500);
        }
        first_session = false;
        g_attempt_active.store(true);

        // Probe.Session order (launcher/Probe.cs:225-247): every refusal comes before the cfg is written and the
        // previous session's outputs are deleted, so a refused start keeps that session's failure evidence.
        std::wstring why;
        const std::optional<std::wstring> pick = select_mod_dll(mod_dll_candidates(build), why);
        if (!pick) {
            log_line(why);
            continue;
        }
        if (mod != nullptr && *pick != loaded_name) {
            log_line(L"restart the game to load " + *pick);
            continue;
        }
        const std::wstring dll = build + L"\\" + *pick;
        if (mod == nullptr) {
            mod = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (mod == nullptr) {
                log_line(L"cannot load " + dll + L" (error " + num(GetLastError()) + L")");
                continue;
            }
            loaded_name = *pick;  // stays loaded until the game exits, as with the Windows launcher
            ex.status = reinterpret_cast<ProbeFn>(GetProcAddress(mod, "ProbeStatus"));
            ex.capabilities = reinterpret_cast<ProbeFn>(GetProcAddress(mod, "ProbeCapabilities"));
            ex.start = reinterpret_cast<ProbeFn>(GetProcAddress(mod, "StartProbe"));
            ex.stop = reinterpret_cast<ProbeFn>(GetProcAddress(mod, "StopProbe"));
            if (ex.complete()) g_stop_probe.store(ex.stop);
        }
        if (!ex.complete()) {
            log_line(loaded_name + L" does not export ProbeStatus, ProbeCapabilities, StartProbe and StopProbe");
            continue;
        }
        const DWORD status = ex.status(nullptr);
        if (status != 0) {
            log_line(L"ProbeStatus=" + num(status) + L": a session is still active or restoring");
            continue;
        }
        const DWORD caps = ex.capabilities(nullptr);
        if (!capabilities_ok(caps)) {
            log_line(L"ProbeCapabilities=" + num(caps) + L" lacks required " + num(kRequiredCapabilities));
            continue;
        }
        std::string config = probe_config(pid);
        {
            std::string override_text;
            if (read_file(build + L"\\" + kProbeOverride, override_text)) {
                if (const auto custom = probe_config_from_override(override_text, pid)) {
                    config = *custom;
                    log_line(L"probe config override: " + from_utf8(config.substr(0, config.size() - 1)));
                } else {
                    log_line(L"probe config override ignored (empty file)");
                }
            }
        }
        if (!write_file(build + L"\\dxgi-probe.cfg", config, false)) {
            log_line(L"cannot write build\\dxgi-probe.cfg (error " + num(GetLastError()) + L")");
            continue;
        }
        for (const wchar_t* stale : {L"dxgi-probe-failure.txt", L"dxgi-probe-result.json", L"xr-live-status.json"})
            DeleteFileW((artifacts + L"\\" + stale).c_str());

        log_line(L"StartProbe begin " + dll);
        {
            std::string knob_text;
            if (read_file(build + L"\\" + kAutoContinueKnob, knob_text)) {
                const unsigned delay = autocontinue_seconds_from_knob(knob_text);
                log_line(L"autocontinue armed: Enter after " + num(delay) + L" s");
                g_artifacts_dir = artifacts;
                HANDLE t = CreateThread(nullptr, 0, autocontinue_thread, reinterpret_cast<void*>(static_cast<ULONG_PTR>(delay)), 0, nullptr);
                if (t != nullptr) CloseHandle(t);
            }
        }
        const DWORD code = ex.start(nullptr);
        std::wstring result = L"StartProbe returned " + num(code);
        if (code == 3) result += L" (" + home + L"\\.git missing)";
        if (code == 1) result += L" (see artifacts\\dxgi-probe-failure.txt)";
        log_line(result);
        if (DeleteFileW(start_trigger.c_str())) log_line(L"removed linux-start written during the session");
    }
}

DWORD WINAPI worker(void*) {
    try {
        run_worker();
    } catch (const std::exception& e) {
        log_line(L"worker stopped: " + from_utf8(e.what()));
    } catch (...) {
        log_line(L"worker stopped: unexpected exception");
    }
    return 0;
}

}  // namespace

extern "C" HRESULT WINAPI proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out,
                                                   LPUNKNOWN outer) {
    InitOnceExecuteOnce(&g_real_once, resolve_real_dinput8, nullptr, nullptr);
    if (g_real_create == nullptr) return E_FAIL;
    return g_real_create(inst, version, riid, out, outer);
}

// Loader lock: only remember the module and start the worker.
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        g_self = instance;
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    }
    return TRUE;
}
