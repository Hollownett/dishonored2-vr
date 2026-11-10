#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_menu_state.hpp"
#include "menu_input.hpp"
#include "crash_observer.hpp"
#include <atomic>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace d2vr {
// Runs on Iggy's own cursor-update thread, after its unchanged native callback.
class NativeMenu {
    using Update=void (__fastcall*)(void*,void*,unsigned char);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using SetCursor=bool (__fastcall*)(void*,const float*,const float*);
    using MoveEvent=void (__fastcall*)(void*,int,int);
    using Dispatch=bool (__fastcall*)(void*,const void*,void*);
    using Button=void (__fastcall*)(void*,void*,const void*);
    inline static NativeMenu* instance_{};
    static constexpr std::uintptr_t update_rva=0x33530;
    static constexpr std::string_view update_code="48896c2418564883ec60488b0db7372502410fb6e8488bf2488b01ff90b80000";
    Update original_{};Read read_{};Logic logic_{};
    SetCursor set_cursor_{};MoveEvent move_event_{};Dispatch dispatch_{};Button button_{};
    std::uintptr_t base_{};
    bool enabled_{},pointing_{};
    std::atomic<bool> active_{false},failed_{false};
    std::atomic<UINT> depth_{0};
    std::atomic<UINT64> calls_{0},samples_{0},rejected_{0},moves_{0},buttons_{0},pending_buttons_{0};
    std::atomic<UINT64> interactive_tick_{0};
    std::mutex mutex_;
    struct Publication {NativeMenuInput input;UINT width{},height{};} publication_;
    struct FileInput {std::uintptr_t wrapper{},file{},player{};MenuClick click;};
    std::array<FileInput,64> file_inputs_{};
    std::ofstream log_;
    std::string error_;
    unsigned evict_{};
    // Last refusal kept for error(); failed() stays for the unexpected (a thrown exception), which still turns the pointer off.
    // v1129 diagnostic counters (journal wheel_pointer): calls that found a published pointer, and where each was lost.
    std::atomic<UINT64> offered_{0},offered_unknown_movie_{0},offered_gated_{0},offered_stale_{0},refused_{0};std::atomic<UINT> last_logic_{0};
    void note_rejection(const char* why) noexcept {++refused_;try {std::lock_guard<std::mutex> lock(mutex_);error_=why;} catch(...) {}}
    bool matches(std::uintptr_t rva,std::string_view hex) {
        std::array<unsigned char,32> bytes{};
        if(hex.size()!=64 || !read_(base_+rva,bytes.data(),bytes.size())) return false;
        const auto digit=[](char c) {return c<='9'?c-'0':c-'a'+10;};
        for(unsigned i=0;i<32;++i) if(bytes[i]!=(digit(hex[2*i])*16+digit(hex[2*i+1]))) return false;
        return true;
    }
    void observe(void* manager,void* wrapper,unsigned char fresh,UINT64 call) noexcept {
        if(!active_) return;
        try {
            Publication publication;
            {std::lock_guard<std::mutex> lock(mutex_);publication=publication_;}
            const bool offered=publication.input.active;if(offered) ++offered_;
            const auto state=native_menu_state(read_,base_,reinterpret_cast<std::uintptr_t>(manager),reinterpret_cast<std::uintptr_t>(wrapper));
            if(!state) {++rejected_;if(offered) ++offered_unknown_movie_;return;}
            const auto logic=logic_();const auto now=GetTickCount64();last_logic_=logic;
            auto input=publication.input;
            if(logic==2 || !logic || fresh || !pointing_) {input.active=false;if(offered) ++offered_gated_;}
            else if(offered && !(now>=input.tick && now-input.tick<=250)) ++offered_stale_;
            // A frame the pointer cannot place is skipped, never the session's pointer: the hand wheel (dual wield) selects only through it.
            const int slot=menu_input_slot(file_inputs_,state->wrapper,evict_);
            if(slot<0) {note_rejection("Native menu file input slots all hold a click");return;}
            auto* file_state=&file_inputs_[static_cast<std::size_t>(slot)];
            if(file_state->wrapper!=state->wrapper || file_state->file!=state->file || file_state->player!=state->player) {
                if(file_state->click.down()) --pending_buttons_; // The former Iggy owner was replaced.
                *file_state={state->wrapper,state->file,state->player,{}};
            }
            // Fresh snapshot creation is not the interactive dispatch phase.
            if(fresh) return;
            interactive_tick_.store(now,std::memory_order_release);
            const auto event=file_state->click.update(input,now);
            const bool live=input.active && now>=input.tick && now-input.tick<=250;
            if(live) [&] { // a refused move skips this frame's move; the click below still goes so down/up stay paired
                const auto mx=rig::value<int>(read_,base_+0x3309cb8),my=rig::value<int>(read_,base_+0x3309cbc);
                const double w=publication.width,h=publication.height;
                if(!mx || !my || *mx<0 || *my<0 || w<=2.*(*mx) || h<=2.*(*my)) return note_rejection("Native menu viewport margins differ");
                const int x=static_cast<int>(std::lround(input.u*w-*mx)),y=static_cast<int>(std::lround(input.v*h-*my));
                const float sx=static_cast<float>(x*1920./(w-2.*(*mx))),sy=static_cast<float>(y*1080./(h-2.*(*my)));
                CrashOperation operation("native Iggy controller pointer");
                if(!set_cursor_(reinterpret_cast<void*>(state->wrapper+0x860),&sx,&sy)) return note_rejection("Native Iggy cursor setter rejected input");
                std::array<std::uint32_t,6> move{};std::array<std::byte,16> result{};
                move_event_(move.data(),x,y);
                if(!dispatch_(reinterpret_cast<void*>(state->file),move.data(),result.data())) return note_rejection("Native Iggy cursor event rejected input");
                ++moves_;
            }();
            if(event!=MenuClick::Event::none) {
                struct Mouse {int code=0xe0;float value{};std::uint32_t unused{};std::uint32_t flags{};} mouse;
                const bool down=event==MenuClick::Event::down;mouse.value=down?1.f:0.f;mouse.flags=down?3:2;
                CrashOperation operation("native Iggy controller click");button_(manager,wrapper,&mouse);++buttons_;
                if(down) ++pending_buttons_;else --pending_buttons_;
            }
            // Keep the bounded pointer trace for the visible test, not minutes of hidden frames.
            if(log_.is_open() && ((samples_<4000 && (live || !pointing_ || samples_<64)) || event!=MenuClick::Event::none)) {   // v1046: not logging = nothing to format
                log_<<call<<','<<GetCurrentThreadId()<<','<<now<<','<<logic<<','<<state->wrapper<<','<<state->file<<','
                    <<state->cached_stage_cursor[0]<<','<<state->cached_stage_cursor[1]<<','<<live<<','<<input.u<<','<<input.v<<','
                    <<static_cast<int>(event)<<'\n';++samples_;if(event!=MenuClick::Event::none) log_.flush();
            }
        } catch(const std::exception& error) {
            try {std::lock_guard<std::mutex> lock(mutex_);error_=error.what();} catch(...) {}
            failed_=true;active_=false;
        } catch(...) {failed_=true;active_=false;}
    }
    static void __fastcall update(void* manager,void* wrapper,unsigned char fresh) {
        auto& self=*instance_;self.depth_.fetch_add(1,std::memory_order_acq_rel);const auto call=++self.calls_;
        __try {self.original_(manager,wrapper,fresh);self.observe(manager,wrapper,fresh,call);}
        __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
public:
    void install(std::uintptr_t base,Read read,Logic logic,bool pointing,const std::filesystem::path& file) {
        if(enabled_ || (instance_ && instance_!=this)) throw std::runtime_error("Native menu input is already owned");
        base_=base;read_=read;logic_=logic;pointing_=pointing;
        if(!matches(update_rva,update_code) ||
           !matches(0x33450,"48895c2408574881ecd0000000418b08498bd8488bfa81e9e00000007428ffc9") ||
           !matches(0x2a8b0,"40534883ec208b4120f3410f1010f30f100a25ffffff00ffc84c8d0c80488b41") ||
           !matches(0x2cb20,"4883ec28488b8908160000e8b0fbfd0085c07513488d0db50ea901e8f09c1300") ||
           !matches(0x102c3e0,"4885c9741a33c04889014889410848894110c701070000008951084489410cf3"))
            throw std::runtime_error("Native Iggy menu functions differ from the pinned executable");
        set_cursor_=reinterpret_cast<SetCursor>(base+0x2a8b0);move_event_=reinterpret_cast<MoveEvent>(base+0x102c3e0);
        dispatch_=reinterpret_cast<Dispatch>(base+0x2cb20);button_=reinterpret_cast<Button>(base+0x33450);
        if(!file.empty()) {log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native menu trace");}  // v1046: an empty path (diagnostics off) is not an error
        log_<<std::setprecision(9)<<"call,thread,tick_ms,logic,wrapper,file,cached_x,cached_y,pointing,u,v,button_event\n";log_.flush();
        publication_={};file_inputs_={};evict_=0;calls_=0;samples_=0;rejected_=0;moves_=0;buttons_=0;pending_buttons_=0;interactive_tick_=0;error_.clear();failed_=false;instance_=this;
        if(!original_ && MH_CreateHook(reinterpret_cast<void*>(base+update_rva),reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native Iggy cursor adapter");
        active_=true;if(MH_EnableHook(reinterpret_cast<void*>(base+update_rva))!=MH_OK) {active_=false;throw std::runtime_error("Cannot enable native menu adapter");}
        enabled_=true;
    }
    void publish(NativeMenuInput input,UINT width,UINT height) {
        // v1130: D3D11's limit, not 4096 (Pimax Crystal-class eyes render ~4312x5104; 4096 silently dropped every wheel pointer).
        if(width<64 || width>16384 || height<64 || height>16384 ||!std::isfinite(input.u) || !std::isfinite(input.v) ||
           input.u<0 || input.u>1 || input.v<0 || input.v>1) input.active=false;
        std::lock_guard<std::mutex> lock(mutex_);publication_={input,width,height};
    }
    bool interactive_ready(UINT64 now) const noexcept {
        const auto tick=interactive_tick_.load(std::memory_order_acquire);
        return active_.load(std::memory_order_acquire) && !failed_ && tick && now>=tick && now-tick<=250;
    }
    bool disable() noexcept {
        interactive_tick_=0;
        if(!read_) return true;
        try {std::lock_guard<std::mutex> lock(mutex_);publication_.input.active=false;} catch(...) {return false;}
        const auto release_deadline=GetTickCount64()+500;
        while(pending_buttons_ && active_ && GetTickCount64()<release_deadline) Sleep(1);
        active_=false;
        if(enabled_) {if(MH_DisableHook(reinterpret_cast<void*>(base_+update_rva))!=MH_OK) return false;enabled_=false;}
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        try {
            bool released=true;
            const auto root=rig::value<std::uintptr_t>(read_,base_+0x2274270);
            for(const auto& file:file_inputs_) if(file.click.down() && root && native_menu_state(read_,base_,*root+0x18,file.wrapper)) released=false;
            std::lock_guard<std::mutex> lock(mutex_);log_.close();return released && (!original_ || matches(update_rva,update_code));
        } catch(...) {return false;}
    }
    bool failed() const {return failed_.load();}
    UINT64 calls() const {return calls_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 moves() const {return moves_.load();}
    UINT64 buttons() const {return buttons_.load();}
    std::string error() {std::lock_guard<std::mutex> lock(mutex_);return error_;}
    // v1129: {calls, offered, unknown movie, gated (logic/fresh/pointing), stale, moves, refused moves, clicks}, and the last logic seen.
    std::array<UINT64,8> wheel_counters() const noexcept {
        return {calls_.load(),offered_.load(),offered_unknown_movie_.load(),offered_gated_.load(),offered_stale_.load(),moves_.load(),refused_.load(),buttons_.load()};}
    UINT last_logic() const noexcept {return last_logic_.load();}
    bool pointing() const noexcept {return pointing_;}
};
} // namespace d2vr
