#pragma once
#include <windows.h>
#include <MinHook.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "menu_patches.hpp"
#include "game_art.hpp"
#include "native_options_page.hpp"
#include <cstdlib>

namespace d2vr {
// Native "VR Settings" item after Options in the pause and main menus, drawn by the game's own menu
// movies (artifacts/menu-re). The item lists are static AS3 in ui/{corvo,emily}_pause_menu.iggy and
// ui/main_menu.iggy, so a same-size AS3 patch (menu_patches.hpp) repurposes an entry the retail game
// never shows (pause: Stats; main: the console-only Quit) as "VR Settings" whose click only sends the
// notification VR_SETTINGS_*. Hooks:
//  0x2E4D0 arkIggyFilesManager file read (m, name, token): after it returns the whole movie is in
//          *(m+0xA708), size *(m+0xA710), path at m+0xA608; matching movies are patched in place before
//          the player is created from that buffer (idempotent; any byte mismatch leaves the file alone).
//  0x34800 the Iggy external-call hub (self, player, call): *(const char**)call is the function name;
//          VR_SETTINGS_* is answered here (returns 1) and raises `requested`, everything else passes.
class NativeMenuEntry {
public:
    using OwnedMoviePatch=bool (*)(const char*,unsigned char*,std::size_t) noexcept;
    inline static std::atomic<OwnedMoviePatch> owned_movie_patch{nullptr};
private:
    using Load=void (__fastcall*)(void*,void*,void*);
    using Hub=unsigned long long (__fastcall*)(void*,void*,void*);
    static constexpr std::uintptr_t load_rva=0x2E4D0,hub_rva=0x34800;
    static constexpr std::array<unsigned char,12> load_code{0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0x70,0x01,0x00,0x00,0x48};
    static constexpr std::array<unsigned char,12> hub_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89};
    inline static Load load_original_{};inline static Hub hub_original_{};inline static std::array<void*,2> target_{};inline static std::uintptr_t base_{};
    static bool ends_with(const char* s,std::size_t n,const char* suffix) noexcept {
        const auto m=std::strlen(suffix);return n>=m && _strnicmp(s+n-m,suffix,m)==0;
    }
    static void patch(void* manager) noexcept {
        __try {
            auto* base=static_cast<unsigned char*>(manager);
            auto* data=*reinterpret_cast<unsigned char**>(base+0xA708);
            const auto size=*reinterpret_cast<std::size_t*>(base+0xA710);
            const char* path=reinterpret_cast<const char*>(base+0xA608);
            const auto length=strnlen(path,0x100);
            if(!data || !length || length==0x100) return;
            if(ends_with(path,length,"corvo_pause_menu.iggy")) game_character=1; // the playable character, for its portrait
            else if(ends_with(path,length,"emily_pause_menu.iggy")) game_character=2;
            // VR Settings menu edits first (pinned to the shipped movie), then the owned art/glyph rows, some of which are
            // pinned to the menu-patched hash. Borrow the buffer only during this verified loader callback; never retain it.
            [&] {for(const auto& movie:menu_patches::movies) {
                if(!ends_with(path,length,movie.suffix)) continue;
                if(size!=movie.size) {++mismatched;return;}
                bool all_new=true,all_old=true;
                for(std::size_t i=0;i<movie.count;++i) {
                    const auto& e=movie.edits[i];
                    if(e.offset>size || e.size>size-e.offset) {++mismatched;return;}
                    if(std::memcmp(data+e.offset,e.new_bytes,e.size)) all_new=false;
                    if(std::memcmp(data+e.offset,e.old_bytes,e.size)) all_old=false;
                }
                std::array<unsigned char,32> digest{};
                if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,data,static_cast<ULONG>(size),digest.data(),static_cast<ULONG>(digest.size()))<0) {++mismatched;return;}
                if(all_new) return; // cached buffer, already ours (its hash may also carry the later glyph row)
                if(!all_old || digest!=movie.original_sha256) {++mismatched;return;}
                for(std::size_t i=0;i<movie.count;++i) std::memcpy(data+movie.edits[i].offset,movie.edits[i].new_bytes,movie.edits[i].size);
                ++patched;return;
            }}();
            if(const auto callback=owned_movie_patch.load(std::memory_order_acquire)) callback(path,data,size);
        } __except(EXCEPTION_EXECUTE_HANDLER) {++mismatched;}
    }
    static void __fastcall load_hook(void* manager,void* name,void* token) {
        load_original_(manager,name,token);
        if(enabled.load()) patch(manager);
    }
    // The external call's name, copied under SEH (empty on a bad pointer).
    static bool call_name(void* call,char (&out)[64]) noexcept {
        out[0]=0;
        __try {const char* name=*static_cast<const char* const*>(call);if(!name) return false;
            for(std::size_t i=0;i<sizeof(out)-1;++i) {out[i]=name[i];if(!name[i]) return true;}out[sizeof(out)-1]=0;return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {out[0]=0;return false;}
    }
    // The VR options page events are queued on the engine's deferred external-call queue (docs/native-vr-options-deferred.md):
    // inside this call AS3 holds the Iggy lock, so the page is written and shown from the drain, like the engine's own pages.
    enum Event : std::uintptr_t {open_page=1,changed_value,back_page,restore_page,action_row=0x100};
    static void __fastcall on_deferred(void* owner,void* file,void*) {
        void* iggy=nullptr;
        __try {iggy=*reinterpret_cast<void* const*>(static_cast<const char*>(file)+0x1608);} __except(EXCEPTION_EXECUTE_HANDLER) {iggy=nullptr;}
        if(!iggy) {if(reinterpret_cast<std::uintptr_t>(owner)==open_page) requested=true;return;}
        switch(const auto event=reinterpret_cast<std::uintptr_t>(owner)) {
            case open_page:
                if(NativeOptionsPage::open(base_,iggy) && NativeOptionsPage::show(base_,iggy)) ++native_pages;
                else {NativeOptionsPage::closed();requested=true;} // the drawn panel instead
                break;
            case changed_value:NativeOptionsPage::changed(base_,iggy);break;
            case restore_page:if(NativeOptionsPage::restore(base_,iggy)) NativeOptionsPage::show(base_,iggy);break;
            case back_page:NativeOptionsPage::closed();break; // lib_options navigates back itself for sections >= 100
            default:if(event>=action_row && event<action_row+64) NativeOptionsPage::action(static_cast<int>(event-action_row));
        }
    }
    static bool enqueue(void* player,void* call,Event event) noexcept {
        static const bool verified=IggyApi::verify(base_);
        if(!verified || !NativeOptionsPage::enabled.load()) return false;
        __try {
            void* file=*reinterpret_cast<void* const*>(static_cast<const char*>(player)+0x78);
            if(!file || *reinterpret_cast<void* const*>(static_cast<const char*>(file)+0x1608)!=player) return false;
            const IggyApi::CallTarget target{file,reinterpret_cast<void*>(event),&on_deferred};
            IggyApi::fn<IggyApi::RegisterCall>(base_,IggyApi::register_call)(file,&target,call);
            ++queued;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static unsigned long long __fastcall hub_hook(void* self,void* player,void* call) {
        char name[64];
        if(enabled.load() && call && player && call_name(call,name)) {
            if(!std::strcmp(name,"VR_SETTINGS_EVT") || !std::strcmp(name,"VR_SETTINGS_OPEN_REQUEST")) {
                ++clicks;if(!enqueue(player,call,open_page)) requested=true;
                return 1;
            }
            if(!std::strcmp(name,"VR_SETTING_CHANGED")) {enqueue(player,call,changed_value);return 1;}
            if(!std::strncmp(name,"VR_SETTING_ACTION_",18)) {
                const int row=std::atoi(name+18);if(row>=0 && row<64) enqueue(player,call,static_cast<Event>(action_row+row));
                return 1;
            }
            const bool back=!std::strcmp(name,"OnBackFromSettingsList"),restore=!std::strcmp(name,"OnRestoreSettings");
            if((back || restore) && NativeOptionsPage::showing.load()) {
                const auto index=NativeOptionsPage::number_argument(call);
                if(index && *index==NativeOptionsPage::instance_index) {enqueue(player,call,back?back_page:restore_page);return 1;}
            }
        }
        return hub_original_(self,player,call);
    }
    template<class Fn,std::size_t N> static bool hook(std::uintptr_t base,std::uintptr_t rva,const std::array<unsigned char,N>& code,void* detour,Fn& original,void*& slot) noexcept {
        if(slot) {
            const auto status=MH_EnableHook(slot);
            return status==MH_OK || status==MH_ERROR_ENABLED;
        }
        void* target=reinterpret_cast<void*>(base+rva);std::array<unsigned char,N> live{};SIZE_T read{};
        if(!ReadProcessMemory(GetCurrentProcess(),target,live.data(),N,&read) || read!=N || live!=code) return false;
        if(MH_CreateHook(target,detour,reinterpret_cast<void**>(&original))!=MH_OK) return false;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return false;}
        slot=target;return true;
    }
public:
    inline static std::atomic<bool> enabled{false},requested{false};
    inline static std::atomic<std::uint64_t> patched{},mismatched{},clicks{},native_pages{},queued{};
    static bool install(std::uintptr_t base) noexcept {
        requested=false;base_=base;
        const bool hub=hook(base,hub_rva,hub_code,reinterpret_cast<void*>(&hub_hook),hub_original_,target_[1]);
        const bool load=hub && hook(base,load_rva,load_code,reinterpret_cast<void*>(&load_hook),load_original_,target_[0]);
        enabled=hub && load;return enabled.load();
    }
    // Hooks stay installed until the game exits: a patched movie may outlive the session, and its
    // item must keep answering (the click then only sets `requested`).
    static void disable() noexcept {requested=false;}
};
} // namespace d2vr
