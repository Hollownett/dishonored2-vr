#pragma once
#include <windows.h>
#include <intrin.h>
#include "../third_party/minhook/include/MinHook.h"
#include "native_menu_state.hpp"
#include <atomic>
#include <cstring>
#include <string_view>

namespace d2vr {
enum class WorldUiKind {none,wheel,notes,journal};
inline WorldUiKind world_ui_logic(std::uint32_t logic) noexcept {
    // 512 = dialogue choices (ui/choice_*.iggy): a world panel like the journal, the world keeps rendering behind it
    // (as a flat menu it was a black screen with the choice bar, headset v349). 1024 = the black market (ui/*_blackmarket.iggy):
    // nothing showed in the headset (v361).
    return logic==256?WorldUiKind::wheel:logic==16?WorldUiKind::notes:logic==128 || logic==512 || logic==1024?WorldUiKind::journal:WorldUiKind::none;
}
inline WorldUiKind world_ui_kind(std::string_view path) noexcept {
    if(path=="ui/quick_access_wheel.iggy") return WorldUiKind::wheel;
    if(path=="ui/notes.iggy") return WorldUiKind::notes;
    if(path=="ui/journal.iggy" || path=="ui/choice_corvo.iggy" || path=="ui/choice_emily.iggy" ||
       path=="ui/corvo_blackmarket.iggy" || path=="ui/emily_blackmarket.iggy") return WorldUiKind::journal;
    return WorldUiKind::none;
}
struct NativeWorldUiOwner {
    NativeMenuState menu;
    WorldUiKind kind{};
    std::array<char,256> path{};
};
// 2C880 copies the requested movie path to file+1658 on successful creation;
// 1D9090 returns the first pointer of that idStr. 2BFB0 returns file+1608.
template<class Read> std::optional<NativeWorldUiOwner> native_world_ui_owner(Read& read,std::uintptr_t base,std::uintptr_t iggy) {
    using namespace rig;
    const auto root=value<std::uintptr_t>(read,base+0x2274270);
    if(!root || !pointer(*root)) return {};
    const auto manager=*root+0x18;
    const auto batches=list(read,manager+8,1024);
    if(!batches || !batches->packed_count) return {};
    const auto batch=batches->data+(batches->packed_count-1)*32;
    const auto files=list(read,batch+0x10,64);
    if(!files) return {};
    for(std::uint32_t i=0;i<files->packed_count;++i) {
        const auto wrapper=files->data+i*0xde8;
        const auto file=value<std::uintptr_t>(read,wrapper);
        if(!file || !pointer(*file) || value<std::uintptr_t>(read,*file+0x1608)!=iggy) continue;
        const auto state=native_menu_state(read,base,manager,wrapper);
        const auto name=value<std::uintptr_t>(read,*file+0x1658);
        if(!state || !name || !*name) return {};
        NativeWorldUiOwner owner{*state,WorldUiKind::none,{}};
        bool terminated=false;
        for(std::size_t j=0;j<owner.path.size();++j) {
            if(!read(*name+j,&owner.path[j],1)) return {};
            if(!owner.path[j]) {terminated=true;break;}
        }
        if(!terminated) return {};
        owner.kind=world_ui_kind(owner.path.data());
        const auto after=native_menu_state(read,base,manager,wrapper);
        if(owner.kind==WorldUiKind::none || value<std::uintptr_t>(read,*file+0x1658)!=name ||
           value<std::uintptr_t>(read,*file+0x1608)!=iggy || !after || after->file!=state->file || after->player!=state->player) return {};
        return owner;
    }
    return {};
}
struct NativeWorldUiDraw {
    WorldUiKind kind{};
    std::uintptr_t file{},iggy{},color_descriptor{},depth_descriptor{};
};
inline bool native_world_ui_target_allowed(const NativeWorldUiDraw* scope,std::uintptr_t base,std::uintptr_t caller) noexcept {
    return scope && scope->kind!=WorldUiKind::none && base && caller==base+0x2dd632;
}
// Per-movie native draw scope only; no native descriptor writes or extra native draws.
// Parent redirects a matching COM RTV through its D3D hooks and verifies actual draws
// occur during this scope before relying on the isolated surface.
class NativeWorldUi {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Begin=bool (*)(const NativeWorldUiDraw&) noexcept;
    using End=void (*)(const NativeWorldUiDraw&) noexcept;
    using Target=void* (*)(const NativeWorldUiDraw&,void* native_rtv) noexcept;
    using Observer=void (*)(std::uintptr_t iggy,UINT64 tick) noexcept;
    inline static std::atomic<Observer> observer{};
    inline static thread_local const NativeWorldUiDraw* current{};
    inline static std::atomic<std::uint64_t> calls{},identified{},redirected{};
    inline static std::atomic<std::uint64_t> target_calls{},target_redirected{};
    inline static std::atomic<std::uint64_t> target_scoped_calls{},target_last_caller{};
private:
    using Draw=void (__fastcall*)(void*,void*,void*,void*,unsigned char);
    using SetTarget=void (__fastcall*)(void*,void*,void*,std::uint32_t,std::uint32_t);
    static constexpr std::uintptr_t rva=0x2dd580;
    static constexpr std::uintptr_t target_rva=0x2e3840;
    inline static constexpr std::array<unsigned char,25> code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x80,0x7c,0x24,0x70,0x00};
    inline static constexpr std::array<unsigned char,26> target_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x40};
    inline static std::uintptr_t base_{};
    inline static Read read_{};
    inline static Begin begin_{};
    inline static End end_{};
    inline static Target target_callback_{};
    inline static Draw original_{};
    inline static SetTarget target_original_{};
    inline static void* target_{};
    inline static void* setter_target_{};
    inline static std::atomic<bool> active_{};
    inline static std::atomic<unsigned> depth_{};
    static NativeWorldUiDraw identify(void* iggy,void* color,void* depth) noexcept {
        try {
            const auto owner=native_world_ui_owner(read_,base_,reinterpret_cast<std::uintptr_t>(iggy));
            if(owner) return {owner->kind,owner->menu.file,reinterpret_cast<std::uintptr_t>(iggy),
                reinterpret_cast<std::uintptr_t>(color),reinterpret_cast<std::uintptr_t>(depth)};
        } catch(...) {}
        return {};
    }
    static void __fastcall draw(void* context,void* iggy,void* color,void* depth,unsigned char rect) {
        ++depth_;
        ++calls;
        if(active_.load()) if(const auto observe=observer.load(std::memory_order_acquire)) observe(reinterpret_cast<std::uintptr_t>(iggy),GetTickCount64());
        const auto scope=active_.load()?identify(iggy,color,depth):NativeWorldUiDraw{};
        const bool selected=scope.kind!=WorldUiKind::none;
        if(selected) ++identified;
        const bool active=selected && begin_ && begin_(scope);
        const auto previous=current;
        if(active) {current=&scope;++redirected;}
        __try {original_(context,iggy,color,depth,rect);}
        __finally {if(active) {if(end_) end_(scope);current=previous;}--depth_;}
    }
    // 2DD62D calls this with the cached COM RTV, DSV, null and two zero integers.
    // The setter's original stores the chosen RTV in Iggy's renderer. The parent
    // retains its replacement for later queued GPU execution; descriptors stay intact.
    static void __fastcall set_target(void* rtv,void* dsv,void* extra,std::uint32_t x,std::uint32_t y) {
        ++depth_;++target_calls;
        void* replacement{};
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(current) target_last_caller=caller>=base_?caller-base_:caller;
        if(active_.load() && target_callback_ && native_world_ui_target_allowed(current,base_,caller)) {
            ++target_scoped_calls;
            replacement=target_callback_(*current,rtv);
        }
        if(replacement) ++target_redirected;
        __try {target_original_(replacement?replacement:rtv,dsv,extra,x,y);}
        __finally {--depth_;}
    }
    template<std::size_t N> static bool matches(void* at,const std::array<unsigned char,N>& expected) noexcept {
        std::array<unsigned char,N> live{};SIZE_T count{};
        return ReadProcessMemory(GetCurrentProcess(),at,live.data(),live.size(),&count) && count==live.size() && live==expected;
    }
public:
    static bool install(std::uintptr_t base,Read read,Begin begin,End end,Target target_callback=nullptr) noexcept {
        if(!base || !read || !begin || !end) return false;
        if(target_) {
            if(base!=base_ || depth_) return false;
            begin_=begin;end_=end;target_callback_=target_callback;active_=true;
            const auto setter=MH_EnableHook(setter_target_);
            const auto draw_status=MH_EnableHook(target_);
            if((setter==MH_OK || setter==MH_ERROR_ENABLED) && (draw_status==MH_OK || draw_status==MH_ERROR_ENABLED)) return true;
            MH_DisableHook(target_);MH_DisableHook(setter_target_);
            active_=false;return false;
        }
        void* target=reinterpret_cast<void*>(base+rva);
        void* setter=reinterpret_cast<void*>(base+target_rva);
        if(!matches(target,code) || !matches(setter,target_code)) return false;
        if(MH_CreateHook(target,reinterpret_cast<void*>(&draw),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
        if(MH_CreateHook(setter,reinterpret_cast<void*>(&set_target),reinterpret_cast<void**>(&target_original_))!=MH_OK) {
            MH_RemoveHook(target);original_=nullptr;return false;
        }
        base_=base;read_=read;begin_=begin;end_=end;target_callback_=target_callback;
        target_=target;setter_target_=setter;
        if(MH_EnableHook(setter)!=MH_OK || MH_EnableHook(target)!=MH_OK) {
            MH_DisableHook(target);MH_DisableHook(setter);
            // Retain both trampolines even after a partial enable: an original-only
            // setter call may already be running on a renderer thread.
            return false;
        }
        active_=true;return true;
    }
    static bool disable() noexcept {
        active_=false;
        if(!target_) return true;
        const auto status=MH_DisableHook(target_);
        const auto setter_status=MH_DisableHook(setter_target_);
        if((status!=MH_OK && status!=MH_ERROR_DISABLED) || (setter_status!=MH_OK && setter_status!=MH_ERROR_DISABLED)) return false;
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_ || !matches(target_,code) || !matches(setter_target_,target_code)) return false;
        // Keep the trampoline alive: a render thread may still be leaving its scope.
        return true;
    }
};
} // namespace d2vr
