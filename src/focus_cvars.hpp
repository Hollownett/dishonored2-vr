#pragma once
// Background input (user, Death of the Outsider headset session 2026-11-22: sticks and Menu reached nothing while the game
// window was not the foreground window, and the game paused itself when it lost focus).
// The launcher (GameKeys.PrepareConfig) and the proxy append in_controlInactiveWindow "1" / win_pauseOnFocusLoss "0" to the
// game's config before each start, but the game owns that file (its next write drops both lines) and nothing confirmed the
// values took. The pad paths that ignore window focus need both: the serializer reads in_controlInactiveWindow right after the
// window-active call the joystick gate wraps (D2 0x3a4b8, after native_pad_focus.hpp's 0x3a4ab), and a paused game takes no
// stick at all. So the mod reads both cvars live and, while either is wrong, sets it through the engine console on the game
// thread (NativeInteractionAim::request_console -> native_console.hpp buffer_command).
//
// idCVar layout (constructor 0x1008f0 -> init 0x101640, both games' registrations identical): +0x28 int value (the field
// every focus check reads: cmp dword [cvar+0x28],0), +0x30 name pointer, +0x38 default string, +0x40 description,
// +0x48 flags. Objects: in_controlInactiveWindow registered at 0x1353220 (lea rcx -> 0x25adf50), win_pauseOnFocusLoss at
// 0x1359610 (lea rcx -> 0x31393e0); tools/doto/cvar_static.py prints both games' registrations.
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
struct FocusCvars {
    static constexpr std::uintptr_t inactive_window_rva=0x25adf50,pause_on_focus_loss_rva=0x31393e0;
    static constexpr const char* inactive_window_name="in_controlInactiveWindow";
    static constexpr const char* pause_on_focus_loss_name="win_pauseOnFocusLoss";
    // The live int value, or nothing when the object at rva is not that cvar (its name pointer must name it).
    template<class Read> static std::optional<int> value(Read read,std::uintptr_t base,std::uintptr_t rva,const char* name) {
        std::uintptr_t name_ptr{};char text[40]{};int v{};
        const auto len=std::strlen(name);
        if(!base || len+1>sizeof(text) || !read(base+rva+0x30,&name_ptr,sizeof(name_ptr)) || !name_ptr ||
           !read(name_ptr,text,len+1) || std::memcmp(text,name,len+1)!=0 || !read(base+rva+0x28,&v,sizeof(v))) return std::nullopt;
        return v;
    }
    template<class Read> static std::optional<int> inactive_window(Read read,std::uintptr_t base) {return value(read,base,inactive_window_rva,inactive_window_name);}
    template<class Read> static std::optional<int> pause_on_focus_loss(Read read,std::uintptr_t base) {return value(read,base,pause_on_focus_loss_rva,pause_on_focus_loss_name);}
};

// When to ask the console: at most every 2 s while a value is wrong, 6 times a session (a console that refuses must not spam).
class FocusCvarGuard {
    std::uint64_t last_{};unsigned sets_{};
public:
    static constexpr unsigned max_sets=6;static constexpr std::uint64_t retry_ms=2000;
    // Returns the console lines to queue now (none while both are right, the console is not ready, or the budget is spent).
    template<class Out> void update(std::optional<int> inactive,std::optional<int> pause,bool console_ready,std::uint64_t now,Out out) {
        if(!console_ready || sets_>=max_sets || (last_ && now-last_<retry_ms)) return;
        const bool fix_inactive=inactive && *inactive!=1,fix_pause=pause && *pause!=0;
        if(!fix_inactive && !fix_pause) return;
        last_=now;++sets_;
        if(fix_inactive) out("in_controlInactiveWindow 1");
        if(fix_pause) out("win_pauseOnFocusLoss 0");
    }
    unsigned sets() const {return sets_;}
    void reset() {last_=0;sets_=0;}
};
} // namespace d2vr
