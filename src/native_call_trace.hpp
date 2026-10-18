#pragma once
// Diagnostic only (build/trace-calls knob: one hex RVA per entry): counts calls of native functions and remembers each one's
// last rcx (`this`). Each hook is a raw thunk that touches rax only (mov rax,&slot; lock inc [rax]; mov [rax+8],rcx;
// mov rax,original; jmp rax), so register/stack arguments, xmm and the return address pass through untouched whatever the
// signature. poll() (any one thread) appends changes as tick,rva,count,last_rcx. Used to find which native code runs when
// bloodfly swarm agents die (docs/native-attack-gating.md).
#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace d2vr {
class NativeCallTrace {
public:
    static unsigned install(std::uintptr_t base,const std::vector<std::uintptr_t>& rvas) noexcept {
        if(!thunks_) thunks_=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if(!thunks_) return 0;
        unsigned n=0;
        for(const auto rva:rvas) {
            if(count_>=slots_.size()) break;
            auto& s=slots_[count_];s={};s.rva=rva;
            auto* t=thunks_+count_*48;void* original{};void* target=reinterpret_cast<void*>(base+rva);
            if(MH_CreateHook(target,t,&original)!=MH_OK) continue;
            const std::uint64_t at=reinterpret_cast<std::uint64_t>(&s.count),to=reinterpret_cast<std::uint64_t>(original);
            unsigned char code[]{0x48,0xb8,0,0,0,0,0,0,0,0, 0xf0,0x48,0xff,0x00, 0x48,0x89,0x48,0x08, 0x48,0xb8,0,0,0,0,0,0,0,0, 0xff,0xe0};
            std::memcpy(code+2,&at,8);std::memcpy(code+20,&to,8);std::memcpy(t,code,sizeof(code));
            FlushInstructionCache(GetCurrentProcess(),t,sizeof(code));
            if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);continue;}
            s.target=target;++count_;++n;
        }
        return n;
    }
    static void disable() noexcept {for(unsigned i=0;i<count_;++i) if(slots_[i].target) MH_DisableHook(slots_[i].target);} // thunks stay
    static void poll(const std::filesystem::path& file) noexcept {
        const auto now=GetTickCount64();if(!count_ || now-last_poll_<100) return; // 10 Hz: frequent callers stay cheap
        last_poll_=now;
        try {
            std::ofstream f;
            for(unsigned i=0;i<count_;++i) {
                const auto c=slots_[i].count;if(c==seen_[i]) continue;
                if(!f.is_open()) f.open(file,std::ios::app);
                f<<GetTickCount64()<<','<<std::hex<<slots_[i].rva<<std::dec<<','<<c<<",0x"<<std::hex<<slots_[i].last_rcx<<std::dec<<'\n';seen_[i]=c;
            }
        } catch(...) {}
    }
private:
    struct Slot {volatile std::uint64_t count{},last_rcx{};std::uintptr_t rva{};void* target{};};
    static std::array<Slot,128> slots_; // defined after the class: clang-cl needs Slot's member initializers complete
    inline static std::array<std::uint64_t,128> seen_{};
    inline static unsigned count_{};inline static unsigned char* thunks_{};inline static std::uint64_t last_poll_{};
};
inline std::array<NativeCallTrace::Slot,128> NativeCallTrace::slots_{};
} // namespace d2vr
