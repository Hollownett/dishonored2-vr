// External Toolkit owner, following KHARVOX's loader + CAPI approach.
// Never opens HID devices or synthesizes/remaps controller input.
#include <Windows.h>
#include <filesystem>
#include <string>

#include "Shared.hpp"
#include "Effects.hpp"

using namespace dvrpsvr2;
namespace {
struct Backend {
    HMODULE loader{};
    HMODULE capi{};
    int (*init)(){};
    void (*deinit)(){};
    bool (*active)(){};
    int (*set)(VRControllerType, const ScePadTriggerEffectCommand&){};
    bool initialized{};

    template<class T> bool resolve(T& fn, const char* name) {
        fn = reinterpret_cast<T>(GetProcAddress(capi, name));
        return fn != nullptr;
    }
    Status load() {
        wchar_t path[32768]{};
        auto length = GetModuleFileNameW(nullptr, path, 32768);
        if (!length || length >= 32768) return Status::LoaderMissing;
        auto file = std::filesystem::path(path).parent_path() / L"psvr2_toolkit_capi_loader.dll";
        loader = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!loader) return Status::LoaderMissing;
        auto get = reinterpret_cast<void* (*)()>(GetProcAddress(loader, "psvr2_toolkit_loader_get_module_handle"));
        if (!get) return Status::ExportMissing;
        capi = static_cast<HMODULE>(get());
        if (!capi) return Status::CapiMissing;
        if (!resolve(init, "psvr2_toolkit_init") || !resolve(deinit, "psvr2_toolkit_deinit")
            || !resolve(active, "psvr2_toolkit_get_driver_active") || !resolve(set, "psvr2_toolkit_set_trigger_effect"))
            return Status::ExportMissing;
        return Status::Ready;
    }
    // Protocol v2: each controller has its own profile (either hand can hold a gun, the crossbow or a power).
    int applyHand(VRControllerType hand, Profile profile) {
        const auto command = makeCommand(profile);
        return set(hand, command);
    }
    int off() {
        const int left = applyHand(VRControllerType::Left, Profile::Off);
        const int right = applyHand(VRControllerType::Right, Profile::Off);
        return left != 0 ? left : right;
    }
    ~Backend() {
        if (initialized) { off(); deinit(); }
        // Loader owns the returned CAPI module; process exit unloads both.
        // Avoid independently freeing its cached handle.
    }
};

bool focused(DWORD pid) {
#ifdef DVR_PSVR2_TESTING
    // Only the isolated test executable uses this, paired with the fake CAPI.
    // Allows IPC tests on a headless desktop. Production always checks Win32.
    return true;
#else
    DWORD foreground{};
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    return foreground == pid;
#endif
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || wcsncmp(argv[1], L"Local\\DVRPsvr2_", 15) != 0) return 2;
    wchar_t* end{};
    const auto pidValue = wcstoul(argv[2], &end, 10);
    if (!pidValue || !end || *end) return 2;
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pidValue);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, argv[1]);
    if (!parent || !mapping) return 2;
    auto shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared || shared->magicValue != magic || shared->version != protocolVersion) return 2;
    int exitCode = 0;
    {
        Backend backend;
        const auto loaded = backend.load();
        if (loaded != Status::Ready) { status(shared, loaded); return 3; }
        const auto result = backend.init();
        if (result != 0) { status(shared, Status::InitFailed, result); return 4; }
        backend.initialized = true;
        Desired previous{};
        bool havePrevious = false;
        bool wasActive = false;
        // Reconnection requires a fresh publish after observing driver activity.
        std::uint64_t activationWord{};
        while (WaitForSingleObject(parent, 0) == WAIT_TIMEOUT
            && InterlockedCompareExchange(&shared->stop, 0, 0) == 0) {
            const bool active = backend.active();
            const auto word = static_cast<std::uint64_t>(InterlockedCompareExchange64(&shared->desired, 0, 0));
            if (!active) {
                status(shared, Status::DriverInactive, -1);
                havePrevious = false; wasActive = false;
                Sleep(50);
                continue;
            }
            if (!wasActive) { activationWord = word; wasActive = true; }
            auto desired = decode(word, tick(), focused(pidValue));
            if (word == activationWord) desired = {};
            if (!havePrevious || desired != previous) {
                // Only the controller whose profile changed is re-sent (after a reconnection both are).
                int applied = 0;
                if (!havePrevious || desired.left != previous.left) applied = backend.applyHand(VRControllerType::Left, desired.left);
                if (applied == 0 && (!havePrevious || desired.right != previous.right)) applied = backend.applyHand(VRControllerType::Right, desired.right);
                if (applied != 0) {
                    status(shared, Status::CommandFailed, applied);
                    exitCode = 5; break; // fail closed; user explicitly reconnects
                }
                previous = desired; havePrevious = true;
                InterlockedIncrement(&shared->appliedCount);
            }
            status(shared, Status::Ready);
            Sleep(20);
        }
        const auto reset = backend.off();
        backend.deinit();
        backend.initialized = false;
        if (reset != 0) { status(shared, Status::ResetFailed, reset); exitCode = 6; }
        else if (exitCode == 0) status(shared, Status::Stopped);
    }
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    CloseHandle(parent);
    return exitCode;
}
