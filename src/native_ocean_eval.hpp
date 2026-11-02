#pragma once
// v731 (user, headset v724-v730: in the Good Doctor boat ride with Meagan the boat hangs ~6 m above the water, then turns upside down and
// sails backwards; theater cutscenes are fine). The boat rides arkOcean's GPU point evaluation (scene group "boatcontrol",
// m_motionOceanOption normalWeight 0.1; consumer 0x4e3f70: height = result + offsetZ, 0 when no result; tilt from the result normals).
// Engine bookkeeping (RE notes: artifacts/boat-re/): Register 0x3032b0 queues points for the next frame slot; the UPDATE_OCEAN pass
// (0x305370 -> 0x305270) calls Dispatch 0x3030f0 once per VIEW, which evaluates the queued points into staging[slot] and then ALWAYS
// stores dispatched[slot] = queued (ocean+0x12d60) and clears the queue (+0x12d70); the backend frame (0x361d60) reads staging[slot]
// back only while dispatched[slot] > 0 (0x304c40). Both paired eyes are copies of the one game view, so the second eye's Dispatch finds an
// empty queue and zeroes dispatched[slot]: no readback ever runs in VR, and every ocean query (boats, swimming, floating props) reads
// frozen results. The flat game and the theater render one view. Fix: one Dispatch per engine frame slot (slot = 0x3308d04, which the
// backend advances once per frame); later views of the same frame skip it, as a second view never existed for the engine's design.
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#else
typedef enum MH_STATUS {MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
#endif
#include <atomic>
#include <cstdint>
#include <cstring>

namespace d2vr {
// The first Dispatch of a frame slot runs; any other in the same slot is a later view of that frame. Thread-safe (views may record
// in parallel): exactly one caller wins each slot change.
class OceanEvalGate {
    std::atomic<int> last_{-1};
public:
    bool take(int slot) noexcept {int seen=last_.load();return seen!=slot && last_.compare_exchange_strong(seen,slot);}
    void reset() noexcept {last_=-1;}
};

class NativeOceanEval {
public:
    static constexpr std::uintptr_t dispatch_rva=0x3030f0,slot_rva=0x3308d04;
    inline static std::atomic<std::uint64_t> dispatches{},skipped{};
    static bool install(std::uintptr_t base) noexcept {
        if(!base) return false;
        if(!target_) {
            // push rbp; push rsi; push r14; push r15; sub rsp,0x38; movsxd rax,[rcx+0x12d70] (the queue count)
            static const unsigned char want[16]{0x40,0x55,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x63,0x81,0x70,0x2d};
            unsigned char got[16]{};
            __try {std::memcpy(got,reinterpret_cast<const void*>(base+dispatch_rva),16);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
            if(std::memcmp(got,want,16)) return false;
            void* t=reinterpret_cast<void*>(base+dispatch_rva);
            if(MH_CreateHook(t,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
            target_=t;base_=base;
        }
        gate_.reset();
        const auto s=MH_EnableHook(target_);
        return s==MH_OK || s==MH_ERROR_ENABLED;
    }
    static void disable() noexcept {if(target_) MH_DisableHook(target_);}
private:
    inline static void* target_{};inline static std::uintptr_t base_{};inline static OceanEvalGate gate_;
    inline static void (__fastcall *original_)(void*,void*,void*){};
    static void __fastcall hook(void* ocean,void* context,void* list) {
        int slot=-1;
        __try {slot=*reinterpret_cast<const int*>(base_+slot_rva);} __except(EXCEPTION_EXECUTE_HANDLER) {slot=-1;}
        if(slot>=0 && !gate_.take(slot)) {++skipped;return;}
        ++dispatches;
        original_(ocean,context,list);
    }
};
} // namespace d2vr
