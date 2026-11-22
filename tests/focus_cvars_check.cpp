// Host check for src/focus_cvars.hpp: the live cvar read refuses an object whose name pointer names another cvar, and the guard
// writes the int and float value of whichever cvar is wrong (and nothing when both are right or the object is not that cvar).
#include "../src/focus_cvars.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

static std::map<std::uintptr_t, std::vector<unsigned char>> memory;
static int writes = 0;
static bool fake_read(std::uintptr_t at, void* out, std::size_t n) {
    for (const auto& [start, bytes] : memory)
        if (at >= start && at + n <= start + bytes.size()) { std::memcpy(out, bytes.data() + (at - start), n); return true; }
    return false;
}
static bool fake_write(std::uintptr_t at, const void* in, std::size_t n) {
    for (auto& [start, bytes] : memory)
        if (at >= start && at + n <= start + bytes.size()) { std::memcpy(bytes.data() + (at - start), in, n); ++writes; return true; }
    return false;
}
static void put_cvar(std::uintptr_t object, std::uintptr_t name_at, const char* name, int value) {
    std::vector<unsigned char> obj(0x50, 0);
    const float f = static_cast<float>(value);
    std::memcpy(obj.data() + 0x28, &value, 4);
    std::memcpy(obj.data() + 0x2c, &f, 4);
    std::memcpy(obj.data() + 0x30, &name_at, 8);
    memory[object] = obj;
    memory[name_at] = std::vector<unsigned char>(name, name + std::strlen(name) + 1);
}
static float float_at(std::uintptr_t object) { float f{}; fake_read(object + 0x2c, &f, 4); return f; }

int main() {
    const std::uintptr_t base = 0x140000000;
    const auto inactive_obj = base + d2vr::FocusCvars::inactive_window_rva, pause_obj = base + d2vr::FocusCvars::pause_on_focus_loss_rva;
    put_cvar(inactive_obj, 0x10000, "in_controlInactiveWindow", 0);
    put_cvar(pause_obj, 0x20000, "win_pauseOnFocusLoss", 1);
    auto inactive = d2vr::FocusCvars::inactive_window(&fake_read, base);
    auto pause = d2vr::FocusCvars::pause_on_focus_loss(&fake_read, base);
    expect(inactive && *inactive == 0 && pause && *pause == 1, "reads both live values");
    expect(!d2vr::FocusCvars::inactive_window(&fake_read, 0), "refuses a null base");

    d2vr::FocusCvarGuard guard;
    guard.update(&fake_read, &fake_write, base, false);
    expect(writes == 0, "waits for the game");
    guard.update(&fake_read, &fake_write, base, true);
    expect(*d2vr::FocusCvars::inactive_window(&fake_read, base) == 1 && float_at(inactive_obj) == 1.f, "in_controlInactiveWindow -> 1 (int and float)");
    expect(*d2vr::FocusCvars::pause_on_focus_loss(&fake_read, base) == 0 && float_at(pause_obj) == 0.f, "win_pauseOnFocusLoss -> 0 (int and float)");
    expect(guard.sets() == 2 && writes == 4, "two sets, four writes");
    guard.update(&fake_read, &fake_write, base, true);
    expect(guard.sets() == 2 && writes == 4, "quiet once both are right");
    put_cvar(pause_obj, 0x20000, "win_pauseOnFocusLoss", 1);   // the game puts its own value back
    guard.update(&fake_read, &fake_write, base, true);
    expect(guard.sets() == 3 && *d2vr::FocusCvars::pause_on_focus_loss(&fake_read, base) == 0, "rewrites a value the game restored");
    put_cvar(inactive_obj, 0x10000, "in_controlInactiveWindoX", 0);   // not that cvar
    const auto before = writes;
    guard.update(&fake_read, &fake_write, base, true);
    expect(writes == before, "never writes an object naming another cvar");

    std::printf(failures ? "focus_cvars_check: %d FAILED\n" : "focus_cvars_check: PASS\n", failures);
    return failures ? 1 : 0;
}
