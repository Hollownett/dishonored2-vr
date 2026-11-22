// Host check for src/focus_cvars.hpp: the live cvar read refuses an object whose name pointer names another cvar, and the guard
// asks the console only while a value is wrong, at most every 2 s and 6 times.
#include "../src/focus_cvars.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

static std::map<std::uintptr_t, std::vector<unsigned char>> memory;
static bool fake_read(std::uintptr_t at, void* out, std::size_t n) {
    for (const auto& [start, bytes] : memory)
        if (at >= start && at + n <= start + bytes.size()) { std::memcpy(out, bytes.data() + (at - start), n); return true; }
    return false;
}
static void put_cvar(std::uintptr_t object, std::uintptr_t name_at, const char* name, int value) {
    std::vector<unsigned char> obj(0x50, 0);
    std::memcpy(obj.data() + 0x28, &value, 4);
    std::memcpy(obj.data() + 0x30, &name_at, 8);
    memory[object] = obj;
    memory[name_at] = std::vector<unsigned char>(name, name + std::strlen(name) + 1);
}

int main() {
    const std::uintptr_t base = 0x140000000;
    put_cvar(base + d2vr::FocusCvars::inactive_window_rva, 0x10000, "in_controlInactiveWindow", 0);
    put_cvar(base + d2vr::FocusCvars::pause_on_focus_loss_rva, 0x20000, "win_pauseOnFocusLoss", 1);
    auto inactive = d2vr::FocusCvars::inactive_window(&fake_read, base);
    auto pause = d2vr::FocusCvars::pause_on_focus_loss(&fake_read, base);
    expect(inactive && *inactive == 0 && pause && *pause == 1, "reads both live values");
    put_cvar(base + d2vr::FocusCvars::inactive_window_rva, 0x10000, "in_controlInactiveWindoX", 1);
    expect(!d2vr::FocusCvars::inactive_window(&fake_read, base), "refuses an object naming another cvar");
    expect(!d2vr::FocusCvars::inactive_window(&fake_read, 0), "refuses a null base");

    d2vr::FocusCvarGuard guard;
    std::vector<std::string> lines;
    auto out = [&](const char* l) { lines.push_back(l); };
    guard.update(0, 1, false, 1000, out);
    expect(lines.empty(), "waits for the console");
    guard.update(0, 1, true, 1000, out);
    expect(lines.size() == 2 && lines[0] == "in_controlInactiveWindow 1" && lines[1] == "win_pauseOnFocusLoss 0", "sets both wrong values");
    lines.clear(); guard.update(0, 1, true, 2500, out);
    expect(lines.empty(), "no retry inside 2 s");
    guard.update(1, 1, true, 3000, out);
    expect(lines.size() == 1 && lines[0] == "win_pauseOnFocusLoss 0", "retries only the wrong one");
    lines.clear(); guard.update(1, 0, true, 9000, out);
    expect(lines.empty(), "quiet once both are right");
    guard.update(std::nullopt, std::nullopt, true, 12000, out);
    expect(lines.empty(), "unreadable values are left alone");
    for (std::uint64_t t = 20000; t < 60000; t += 2000) guard.update(0, 0, true, t, out);
    expect(guard.sets() == d2vr::FocusCvarGuard::max_sets, "budget of 6 sets");
    guard.reset(); lines.clear(); guard.update(0, 0, true, 100000, out);
    expect(lines.size() == 1 && guard.sets() == 1, "reset restores the budget");

    std::printf(failures ? "focus_cvars_check: %d FAILED\n" : "focus_cvars_check: PASS\n", failures);
    return failures ? 1 : 0;
}
