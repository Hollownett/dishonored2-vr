#pragma once
#include <windows.h>
#include <Xinput.h>
#include <dbt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include "menu_tap.hpp"

namespace d2vr {
struct ControllerInput {
    bool active{};
    float move_x{},move_y{},turn_x{},turn_y{},left_trigger{},right_trigger{},left_grip{},right_grip{};
    bool a{},b{},x{},y{},left_click{},right_click{},menu{};
    bool journal{},suppress_pause{},menu_modifier{};
    bool native_y_quick_action{};
    bool lean_controlled{},lean_modifier{},lean_suspended{};
    std::array<bool,4> dpad{}; // right, up, left, down (native QUICK_0..3).
};
inline bool native_directional_stick(const ControllerInput& in) noexcept {
    return !in.menu_modifier && (in.lean_suspended || in.lean_modifier || in.left_grip>.55f);
}

class PadComposer {
    MenuTap menu_tap_;
    bool previous_gameplay_=false,previous_active_=false,previous_modifier_=false,previous_directional_=false;
    WORD blocked_buttons_{};
    unsigned blocked_analog_{};
    std::array<bool,2> shoulder_held_{},shoulder_tap_{};std::array<std::uint64_t,2> shoulder_since_{};
    static SHORT stick(float value) {
        return std::isfinite(value)?static_cast<SHORT>(std::lround(std::clamp(value,-1.f,1.f)*32767.f)):0;
    }
    static BYTE trigger(float value) {
        return std::isfinite(value)?static_cast<BYTE>(std::lround(std::clamp(value,0.f,1.f)*255.f)):0;
    }
public:
    inline static constexpr std::uint64_t menu_shoulder_tap_ms=120; // well under the menus' 225 ms repeat delay
    XINPUT_GAMEPAD compose(const ControllerInput& in,bool gameplay,std::uint64_t now=GetTickCount64()) {
        const bool pause=menu_tap_.update(in.menu,in.active && !in.suppress_pause,now);
        if(!in.active) {
            previous_active_=previous_modifier_=previous_directional_=false;blocked_buttons_=0;blocked_analog_=0;shoulder_held_={};
            return {};
        }
        WORD buttons=0;
        if(in.a) buttons|=XINPUT_GAMEPAD_A;
        if(in.b) buttons|=XINPUT_GAMEPAD_B;
        if(in.x) buttons|=XINPUT_GAMEPAD_X;
        if(in.y && (!gameplay || in.native_y_quick_action)) buttons|=XINPUT_GAMEPAD_Y;
        if(in.left_click) buttons|=XINPUT_GAMEPAD_LEFT_THUMB;
        if(in.right_click) buttons|=XINPUT_GAMEPAD_RIGHT_THUMB;
        if(pause) buttons|=XINPUT_GAMEPAD_START;
        if(in.journal) buttons|=XINPUT_GAMEPAD_BACK;
        constexpr std::array<WORD,4> dpad_bits{XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_DOWN};
        for(unsigned i=0;i<4;++i) if(in.dpad[i]) buttons|=dpad_bits[i];
        unsigned analog=0;
        if(trigger(in.left_trigger)>20) analog|=1;
        if(trigger(in.right_trigger)>20) analog|=2;
        if(trigger(in.left_grip)>140) analog|=4;
        if(trigger(in.right_grip)>140) analog|=8;
        if(std::abs(stick(in.move_x))>5000 || std::abs(stick(in.move_y))>5000) analog|=16;
        if(std::abs(stick(in.turn_x))>5000 || std::abs(stick(in.turn_y))>5000) analog|=32;
        // Fresh activation or leaving a menu requires releasing any held input.
        if(!previous_active_ || (gameplay && !previous_gameplay_)) {
            blocked_buttons_|=buttons;blocked_analog_|=analog;
        }
        if(previous_modifier_ && !in.menu_modifier) blocked_analog_|=analog;
        if(previous_directional_ && !native_directional_stick(in) && gameplay) blocked_analog_|=analog&32;
        previous_active_=true;previous_gameplay_=gameplay;previous_modifier_=in.menu_modifier;
        previous_directional_=native_directional_stick(in) && gameplay;
        blocked_buttons_&=buttons;blocked_analog_&=analog;
        buttons&=static_cast<WORD>(~blocked_buttons_);
        // A grip squeezed in a menu is one LB/RB tap: the game's menus repeat a held shoulder after 225 ms every 75 ms
        // (lib_options Input.DELAY_BEFORE_REPEAT/REPEAT_INTERVAL), so a held grip cycled tabs a random number of times
        // (sim v395). A squeeze begun in gameplay keeps holding (LB opens the power wheel only while held).
        for(unsigned side=0;side<2;++side) {
            const unsigned bit=side?8u:4u;
            const bool held=!in.menu_modifier && (analog&bit) && !(blocked_analog_&bit);
            if(held && !shoulder_held_[side]) {shoulder_since_[side]=now;shoulder_tap_[side]=!gameplay;}
            shoulder_held_[side]=held;
            if(held && (!shoulder_tap_[side] || now-shoulder_since_[side]<menu_shoulder_tap_ms))
                buttons|=side?XINPUT_GAMEPAD_RIGHT_SHOULDER:XINPUT_GAMEPAD_LEFT_SHOULDER;
        }
        // Raw held modifiers can reserve RX before reaching this composer.
        // If focus guards blocked their actual native buttons, neither axis
        // may escape as ordinary LOOK until the modifier is released.
        const bool blocked_directional=gameplay && native_directional_stick(in) &&
            !(buttons&(XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_LEFT_SHOULDER));
        XINPUT_GAMEPAD pad{};pad.wButtons=buttons;
        if(gameplay && !in.menu_modifier) {
            if(!(blocked_analog_&1)) pad.bLeftTrigger=trigger(in.left_trigger);
            if(!(blocked_analog_&2)) pad.bRightTrigger=trigger(in.right_trigger);
        }
        if(!in.menu_modifier && !(blocked_analog_&16)) {
            pad.sThumbLX=stick(in.move_x);pad.sThumbLY=stick(in.move_y);
        }
        if(!in.menu_modifier && !blocked_directional && !(blocked_analog_&32)) {
            pad.sThumbRX=stick(in.turn_x);
            // Head pitch remains physical; right-stick Y navigates menus/wheels.
            if(!gameplay || (buttons&XINPUT_GAMEPAD_LEFT_SHOULDER)) pad.sThumbRY=stick(in.turn_y);
        }
        return pad;
    }
};

// Patch only the exact executable's GetState import. Runtime/other DLL callers
// keep their own imports. The bridge object and disabled DLL stay until exit.
class XInputBridge {
    using GetState=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
    inline static std::atomic<XInputBridge*> instance_{nullptr};
    GetState original_{};
    void** slot_{};
    std::atomic<bool> installed_{false};
    std::mutex mutex_;
    PadComposer composer_;
    XINPUT_STATE state_{};
    ULONGLONG updated_{};
    bool virtual_active_{};
    std::atomic<UINT64> reads_{0},virtual_reads_{0},nonzero_reads_{0},pause_reads_{0},lb_reads_{0},x_reads_{0},lb_inputs_{0},lb_published_{0};
    HWND game_window_{};
    DEV_BROADCAST_DEVICEINTERFACE_W device_notice_{};
    std::atomic<bool> connection_attempted_{false},connection_notified_{false};
    void notify_connection(bool connected) noexcept {
        if(!game_window_) return;
        DWORD_PTR result{};
        // Native Win32 input only probes disconnected pads on a device-change
        // notification. Keep this payload alive with the disabled DLL; a GUI
        // timeout must never leave it pointing into a sender's stack.
        const auto delivered=SendMessageTimeoutW(game_window_,WM_DEVICECHANGE,
            connected?DBT_DEVICEARRIVAL:DBT_DEVICEREMOVECOMPLETE,
            reinterpret_cast<LPARAM>(&device_notice_),SMTO_ABORTIFHUNG,50,&result)!=0;
        if(connected) connection_notified_.store(delivered,std::memory_order_release);
    }
    static DWORD WINAPI get_state(DWORD index,XINPUT_STATE* out) noexcept {
        auto& self=*instance_.load(std::memory_order_acquire);
        ++self.reads_;
        if(index==0 && out && self.installed_.load(std::memory_order_acquire)) {
            try {
                std::lock_guard<std::mutex> lock(self.mutex_);
                if(self.virtual_active_) {
                    // A stalled renderer must stop driving gameplay even if XR
                    // cannot deliver its next focus event.
                    const XINPUT_GAMEPAD neutral{};
                    if(GetTickCount64()-self.updated_>250 &&
                       std::memcmp(&self.state_.Gamepad,&neutral,sizeof(XINPUT_GAMEPAD))!=0) {
                        self.state_.Gamepad={};++self.state_.dwPacketNumber;
                    }
                    *out=self.state_;++self.virtual_reads_;
                    if(self.state_.Gamepad.wButtons&XINPUT_GAMEPAD_START) ++self.pause_reads_;
                    if(self.state_.Gamepad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER) ++self.lb_reads_;
                    if(self.state_.Gamepad.wButtons&XINPUT_GAMEPAD_X) ++self.x_reads_;
                    if(std::memcmp(&self.state_.Gamepad,&neutral,sizeof(neutral))) ++self.nonzero_reads_;
                    return ERROR_SUCCESS;
                }
            } catch(...) {return ERROR_DEVICE_NOT_CONNECTED;}
        }
        return self.original_(index,out);
    }
public:
    void install(std::uintptr_t base,std::uintptr_t import_rva) {
        if(installed_) return;
        const auto prior_instance=instance_.load();
        if(prior_instance && prior_instance!=this) throw std::runtime_error("Another controller bridge owns this module");
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || nt->Signature!=IMAGE_NT_SIGNATURE ||
           nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || import_rva%8 ||
           import_rva+sizeof(void*)>nt->OptionalHeader.SizeOfImage)
            throw std::runtime_error("Controller import lies outside the supported image");
        auto library=GetModuleHandleW(L"XINPUT9_1_0.dll");
        auto symbol=library?GetProcAddress(library,"XInputGetState"):nullptr;
        if(!symbol) throw std::runtime_error("Native XInput 9.1.0 export is unavailable");
        static_assert(sizeof(symbol)==sizeof(original_));std::memcpy(&original_,&symbol,sizeof(original_));
        void* expected{};std::memcpy(&expected,&original_,sizeof(expected));
        slot_=reinterpret_cast<void**>(base+import_rva);
        if(*slot_!=expected) throw std::runtime_error("Game controller import is already modified");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            composer_={};state_={};updated_=0;virtual_active_=false;
            reads_=0;virtual_reads_=0;nonzero_reads_=0;pause_reads_=0;lb_reads_=0;x_reads_=0;
            connection_attempted_=false;connection_notified_=false;
        }
        device_notice_={};device_notice_.dbcc_size=sizeof(device_notice_);device_notice_.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;
        // Same HID interface GUID as the native RegisterDeviceNotification.
        device_notice_.dbcc_classguid={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};
        instance_.store(this,std::memory_order_release);
        void* replacement{};auto hook=&get_state;std::memcpy(&replacement,&hook,sizeof(replacement));
        DWORD protection{};
        if(!VirtualProtect(slot_,sizeof(void*),PAGE_READWRITE,&protection))
            throw std::runtime_error("Cannot protect the controller import");
        auto prior=InterlockedCompareExchangePointer(slot_,replacement,expected);
        DWORD unused{};const bool protected_again=VirtualProtect(slot_,sizeof(void*),protection,&unused)!=0;
        if(prior!=expected || !protected_again) {
            if(prior==expected) InterlockedCompareExchangePointer(slot_,expected,replacement);
            throw std::runtime_error("Controller import changed during installation");
        }
        installed_.store(true,std::memory_order_release);
    }
    void publish(const ControllerInput& input,bool gameplay) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto pad=composer_.compose(input,gameplay);
            if(input.left_grip>.55f) ++lb_inputs_;
            if(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER) ++lb_published_;
            if(std::memcmp(&pad,&state_.Gamepad,sizeof(pad))!=0) {state_.Gamepad=pad;++state_.dwPacketNumber;}
            virtual_active_|=input.active;updated_=GetTickCount64();
        }
        // The GUI callback immediately calls our GetState import. Do not hold
        // its state mutex while notifying that thread.
        if(input.active && installed_.load() && game_window_ && !connection_attempted_.exchange(true)) notify_connection(true);
    }
    void set_game_window(HWND window) {
        DWORD owner{};
        if(!window || !GetWindowThreadProcessId(window,&owner) || owner!=GetCurrentProcessId())
            throw std::runtime_error("Controller notification window is not owned by the game");
        game_window_=window;
    }
    bool disable() {
        installed_.store(false,std::memory_order_release);
        if(!slot_) return true;
        void* original{},*replacement{};auto hook=&get_state;
        std::memcpy(&original,&original_,sizeof(original));std::memcpy(&replacement,&hook,sizeof(replacement));
        DWORD protection{},unused{};
        if(!VirtualProtect(slot_,sizeof(void*),PAGE_READWRITE,&protection)) return false;
        const auto prior=InterlockedCompareExchangePointer(slot_,original,replacement);
        const bool restored=VirtualProtect(slot_,sizeof(void*),protection,&unused)!=0;
        const auto clean=restored && (prior==replacement || prior==original);
        if(clean && connection_notified_.exchange(false)) notify_connection(false);
        return clean;
    }
    UINT64 reads() const {return reads_.load();}
    UINT64 virtual_reads() const {return virtual_reads_.load();}
    UINT64 nonzero_reads() const {return nonzero_reads_.load();}
    UINT64 lb_reads() const {return lb_reads_.load();}
    UINT64 x_reads() const {return x_reads_.load();}
    UINT64 lb_inputs() const {return lb_inputs_.load();}
    UINT64 lb_published() const {return lb_published_.load();}
    UINT64 pause_reads() const {return pause_reads_.load();}
    bool connection_attempted() const {return connection_attempted_.load();}
    bool connection_notified() const {return connection_notified_.load();}
};
} // namespace d2vr
