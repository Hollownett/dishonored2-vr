#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace d2vr {
// Session-only queued headphone panning. No writes to native caches or main flags.
// Serialize this adapter's lifecycle; the native Wwise API queues the audio work.
class NativeHeadphoneMode {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    inline static constexpr std::uintptr_t cvar_rva=0x2bfacd0,queue_rva=0x10ca520,
        main_pointer_rva=0x2286080;
    inline static constexpr std::array<unsigned char,92> queue_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x41,0x8b,0xd8,0x8b,0xfa,0x8b,0xf1,0xe8,0x25,0x24,0x05,0xff,0x48,0x8b,0x0d,0x0e,0xdc,0xde,0x02,0xba,0x2a,0x00,0x00,0x00,0x44,0x0f,0xb7,0xc0,0xe8,0x20,0xe6,0x01,0x00,0x89,0x58,0x08,0x48,0x8b,0x5c,0x24,0x30,0x89,0x70,0x10,0x48,0x8b,0x74,0x24,0x38,0x89,0x78,0x0c,0x48,0x8b,0x05,0xe6,0xdb,0xde,0x02,0xf0,0xff,0x88,0xac,0x00,0x00,0x00,0xb8,0x01,0x00,0x00,0x00,0x48,0x83,0xc4,0x20,0x5f,0xc3};
private:
    std::uintptr_t base_{},main_{};Read read_{};
    std::uint64_t saved_{};bool baseline_{},active_{};
    enum class State {unavailable,replaced,ready};
    State state(std::uint64_t& cache,bool& headphones) const {
        std::uintptr_t object{},again{};unsigned char flags{};
        if(!read_ || !read_(base_+main_pointer_rva,&object,8))return State::unavailable;
        if(object!=main_ || !object)return State::replaced;
        const bool values=read_(object,&flags,1) && (flags&0x24)==0x24 &&
            read_(base_+cvar_rva+0x28,&cache,8);
        // Never authorize a queue request from values spanning two owners.
        if(!read_(base_+main_pointer_rva,&again,8))return State::unavailable;
        if(again!=main_ || !again)return State::replaced;
        if(!values)return State::unavailable;
        headphones=(flags&0x40)!=0;return State::ready;
    }
    bool queue(bool headphones) const {
        using Fn=int (__fastcall*)(std::uint32_t,std::uint32_t,std::uint32_t);
        return reinterpret_cast<Fn>(base_+queue_rva)(headphones?1u:0u,0x20u,0u)==1;
    }
public:
    static bool verify(std::uintptr_t base,Read read) {
        if(!base || !read)return false;
        std::array<unsigned char,92> code{};std::uintptr_t name{};std::array<char,19> text{};
        return read(base+queue_rva,code.data(),code.size()) && code==queue_code &&
            read(base+cvar_rva+0x30,&name,8) && name && read(name,text.data(),text.size()) &&
            std::strcmp(text.data(),"s_useHeadphoneMode")==0;
    }
    bool install(std::uintptr_t base,Read read) {
        std::uint64_t cache{};bool headphones{};
        if(active_) {
            if(base!=base_)return false;
            const auto status=state(cache,headphones);
            if(status==State::unavailable)return false;
            if(status==State::ready)return cache==saved_ && headphones==baseline_;
            // The previous audio owner is gone. Its baseline cannot be applied
            // to a replacement; bind a fresh receipt below when one is ready.
            active_=false;
        }
        if(!verify(base,read))return false;
        base_=base;read_=read;
        if(!read_(base_+main_pointer_rva,&main_,8) || !main_ ||
           state(saved_,baseline_)!=State::ready ||
           state(cache,headphones)!=State::ready || cache!=saved_ || headphones!=baseline_ ||
           !queue(true))return false;
        active_=true;return true;
    }
    // A true return records accepted queue submission, not acoustic confirmation.
    bool enabled() const {
        std::uint64_t cache{};bool headphones{};
        return active_ && state(cache,headphones)==State::ready && cache==saved_ && headphones==baseline_;
    }
    bool owned() const noexcept {return active_;}
    bool restore() {
        if(!active_)return true;
        std::uint64_t cache{};bool headphones{};
        auto status=state(cache,headphones);
        if(status==State::replaced) {active_=false;return true;}
        if(status==State::unavailable)return false;
        // Native settings changed: their own queue notification takes precedence.
        if(cache!=saved_ || headphones!=baseline_) {active_=false;return true;}
        status=state(cache,headphones);
        if(status==State::replaced) {active_=false;return true;}
        if(status==State::unavailable)return false;
        if(cache!=saved_ || headphones!=baseline_) {active_=false;return true;}
        if(!queue(baseline_))return false;
        active_=false;return true;
    }

};
}
