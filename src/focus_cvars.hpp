#pragma once
// Background input (user, Death of the Outsider headset session 2026-11-22: sticks and Menu reached nothing while the game
// window was not the foreground window, and the game paused itself when it lost focus).
// The launcher (GameKeys.PrepareConfig) and the proxy append in_controlInactiveWindow "1" / win_pauseOnFocusLoss "0" to the
// game's config before each start, but the live values stayed at their defaults (0 / 1) in both a DOTO headset session and
// after six engine-console sets (v145722: every line "queued", nothing changed): the engine does not take these from the
// config or the console after startup. Every real session so far simply kept window focus.
// The pad paths that ignore window focus read the cvars' int fields directly: the serializer tests in_controlInactiveWindow
// right after the window-active call the joystick gate wraps (D2 0x3a4b8 cmp dword [obj+0x28],0, after native_pad_focus.hpp's
// 0x3a4ab), and the focus-loss handler tests win_pauseOnFocusLoss (D2 0xea34e cmp dword [obj+0x28],0). So the mod writes
// those fields itself (int +0x28 and its float twin +0x2c, the pair the engine's GetInteger 0x1015f4 / GetFloat 0x1015b7 read),
// after checking the object's name pointer, and writes them again whenever the game puts its own value back.
//
// idCVar layout (constructor 0x1008f0 -> init 0x101640, both games' registrations identical): +0x28 int value, +0x2c float
// value, +0x30 name pointer, +0x38 default string, +0x40 description, +0x48 flags. Objects: in_controlInactiveWindow
// registered at 0x1353220 (lea rcx -> 0x25adf50), win_pauseOnFocusLoss at 0x1359610 (lea rcx -> 0x31393e0);
// tools/doto/cvar_static.py prints both games' registrations, tools/doto/cvar_probe.py reads them live.
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
struct FocusCvars {
    static constexpr std::uintptr_t inactive_window_rva=0x25adf50,pause_on_focus_loss_rva=0x31393e0;
    static constexpr const char* inactive_window_name="in_controlInactiveWindow";
    static constexpr const char* pause_on_focus_loss_name="win_pauseOnFocusLoss";
    static constexpr int inactive_window_wanted=1,pause_on_focus_loss_wanted=0;
    template<class Read> static bool named(Read read,std::uintptr_t base,std::uintptr_t rva,const char* name) {
        std::uintptr_t name_ptr{};char text[40]{};
        const auto len=std::strlen(name);
        return base && len+1<=sizeof(text) && read(base+rva+0x30,&name_ptr,sizeof(name_ptr)) && name_ptr &&
            read(name_ptr,text,len+1) && std::memcmp(text,name,len+1)==0;
    }
    // The live int value, or nothing when the object at rva is not that cvar (its name pointer must name it).
    template<class Read> static std::optional<int> value(Read read,std::uintptr_t base,std::uintptr_t rva,const char* name) {
        int v{};
        if(!named(read,base,rva,name) || !read(base+rva+0x28,&v,sizeof(v))) return std::nullopt;
        return v;
    }
    // Writes the int and float value of the named cvar; false when the object is not that cvar or a write faults.
    template<class Read,class Write> static bool set(Read read,Write write,std::uintptr_t base,std::uintptr_t rva,const char* name,int v) {
        const float f=static_cast<float>(v);
        return named(read,base,rva,name) && write(base+rva+0x28,&v,sizeof(v)) && write(base+rva+0x2c,&f,sizeof(f));
    }
    template<class Read> static std::optional<int> inactive_window(Read read,std::uintptr_t base) {return value(read,base,inactive_window_rva,inactive_window_name);}
    template<class Read> static std::optional<int> pause_on_focus_loss(Read read,std::uintptr_t base) {return value(read,base,pause_on_focus_loss_rva,pause_on_focus_loss_name);}
};

// Keeps both cvars at the background-input values: writes whichever is wrong (counts writes and failures).
class FocusCvarGuard {
    unsigned sets_{},failures_{};
public:
    template<class Read,class Write> void update(Read read,Write write,std::uintptr_t base,bool game_ready) {
        if(!game_ready) return;
        const auto fix=[&](std::uintptr_t rva,const char* name,int wanted) {
            const auto now=FocusCvars::value(read,base,rva,name);
            if(!now || *now==wanted) return;
            if(FocusCvars::set(read,write,base,rva,name,wanted)) ++sets_;else ++failures_;
        };
        fix(FocusCvars::inactive_window_rva,FocusCvars::inactive_window_name,FocusCvars::inactive_window_wanted);
        fix(FocusCvars::pause_on_focus_loss_rva,FocusCvars::pause_on_focus_loss_name,FocusCvars::pause_on_focus_loss_wanted);
    }
    unsigned sets() const {return sets_;}
    unsigned failures() const {return failures_;}
    void reset() {sets_=0;failures_=0;}
};
} // namespace d2vr
