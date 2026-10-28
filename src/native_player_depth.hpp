#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <sstream>

namespace d2vr {
// r_playerDepthHack has native change listeners. Cache-only writes do not
// update their strings or the player's inherited model depth, and loading a
// player restores 5. Native 0x102880(cvar, float, force) updates the string,
// both caches and listeners (0x102400 -> 0x102cb0 -> body 0x7724d0).
// No hook of its own: service ONLY on the qualified Stage0 game thread,
// including restoration before that hook is retired.
class NativePlayerDepth {
public:
    static constexpr std::uintptr_t cvar_rva=0x398e300,setter_rva=0x102880;
    static constexpr std::array<unsigned char,20> setter_code{
        0x40,0x57,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x60};
    struct Value {LONG integer{};float scalar{};};
private:
    mutable std::mutex mutex_;
    std::atomic<bool> requested_{},owned_{};
    bool listener_verified_{};
    std::uintptr_t base_{};
    Value saved_{};
    std::uint64_t changes_{},restores_{},foreign_{},failures_{};
    template<class Read> static std::optional<Value> value(Read& read,std::uintptr_t base) {
        std::uintptr_t text{};std::array<char,18> name{};Value result{},again{};
        if(!read(base+cvar_rva+0x30,&text,8) || text<0x10000 ||
           !read(text,name.data(),name.size()) || std::strcmp(name.data(),"r_playerDepthHack")!=0 ||
           !read(base+cvar_rva+0x28,&result,sizeof(result)) ||
           !read(base+cvar_rva+0x28,&again,sizeof(again)) ||
           result.integer!=again.integer || result.scalar!=again.scalar ||
           !std::isfinite(result.scalar) || result.scalar<0 || result.scalar>64 ||
           result.integer!=static_cast<LONG>(result.scalar)) return {};
        return result;
    }
    static bool equal(Value a,Value b) {return a.integer==b.integer && a.scalar==b.scalar;}
    static bool native_set(std::uintptr_t at,std::uintptr_t object,float scalar) noexcept {
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,float,bool)>(at)(object,scalar,true);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
public:
    static NativePlayerDepth& process_owner() {static NativePlayerDepth instance;return instance;}
    template<class Read> bool start(std::uintptr_t base,Read& read) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(requested_ || owned_)return false;
        std::array<unsigned char,setter_code.size()> bytes{};
        const auto initial=base?value(read,base):std::nullopt;
        if(!initial || !read(base+setter_rva,bytes.data(),bytes.size()) || bytes!=setter_code)return false;
        base_=base;saved_=*initial;changes_=restores_=foreign_=failures_=0;
        listener_verified_=false;requested_=true;return true;
    }
    bool needs_service() const noexcept {return requested_.load() || owned_.load();}
    bool pending() const noexcept {return owned_.load();}
    void request_stop() noexcept {std::lock_guard<std::mutex> lock(mutex_);requested_=false;listener_verified_=false;}
    // A zero cache alone does not prove that native model listeners ran.
    // Read-only; never invokes the setter from an upload or render callback.
    template<class Read> bool world_depth_current(Read& read) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requested_ && owned_ && listener_verified_ && value(read,base_)==std::optional<Value>{Value{0,0.f}};
    }
    // Setter is injectable for the compiled native fixture, never used to
    // change ABI or dispatch engines on the worker thread.
    template<class Read,class Set> void service(Read& read,Set set) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!requested_ && !owned_)return;
        const auto current=value(read,base_);
        if(!current) {listener_verified_=false;++failures_;return;}
        if(requested_) {
            if(equal(*current,{0,0.f}))return;
            listener_verified_=false;
            // Claim before entering native code; failed/partial calls must
            // still require game-thread restoration and block restart.
            owned_=true;
            if(!set(base_+setter_rva,base_+cvar_rva,0.f) || value(read,base_)!=std::optional<Value>{Value{0,0.f}})++failures_;
            else {listener_verified_=true;++changes_;}
        } else {
            listener_verified_=false;
            if(!equal(*current,{0,0.f})) {owned_=false;++foreign_;return;}
            if(!set(base_+setter_rva,base_+cvar_rva,saved_.scalar) || value(read,base_)!=std::optional<Value>{saved_}) {++failures_;return;}
            owned_=false;++restores_;
        }
    }
    template<class Read> void service(Read& read) {service(read,&native_set);}
    // Worker waits; it never calls a native setter. No Stage0 while paused:
    // retain the hook and report pending until the game thread can restore.
    bool stop_and_wait(unsigned timeout_ms=2000) noexcept {
        request_stop();const auto until=GetTickCount64()+timeout_ms;
        while(pending() && GetTickCount64()<until)Sleep(1);
        return !pending();
    }
    std::string status_json() const {
        std::lock_guard<std::mutex> lock(mutex_);std::ostringstream out;
        out<<"{\"enabled\":"<<(requested_?"true":"false")<<",\"owned\":"<<(owned_?"true":"false")
           <<",\"restore_pending\":"<<(owned_ && !requested_?"true":"false")
           <<",\"changes\":"<<changes_<<",\"restores\":"<<restores_<<",\"foreign\":"<<foreign_<<",\"failures\":"<<failures_<<'}';return out.str();
    }
    friend bool operator==(Value a,Value b) {return equal(a,b);}
    friend bool operator!=(Value a,Value b) {return !equal(a,b);}
};
} // namespace d2vr
