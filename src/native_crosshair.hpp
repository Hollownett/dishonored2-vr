#pragma once
#include "native_readable_range.hpp"
#include <windows.h>
// Relative so a bare `cl tests\..._check.cpp` with no /I still resolves; the
// repository build passes third_party\minhook\include as well.
#include "../third_party/minhook/include/MinHook.h"
#include "game_rig.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace d2vr {
// Exact-build reticle placement. The engine's own world-point placement
// (0x672E20) maps the projected point to the Scaleform stage as
// x = 0.5 * ndc_x * stage_x and y = -0.5 * ndc_y * stage_y; the stage extents
// are read live because they follow the player's resolution.
inline std::array<float,2> crosshair_stage_position(float ndc_x,float ndc_y,float stage_x,float stage_y) {
    return {0.5f*ndc_x*stage_x,-0.5f*ndc_y*stage_y};
}
// Row-major with the column-vector convention game_stereo.hpp reads the native
// projection with, so clip = M * (x,y,z,1). Returns nothing for a point on or
// behind the eye plane (w <= 0), where the divide mirrors the reticle.
inline std::optional<std::array<float,2>> project_to_ndc(const float view_projection_row_major[16],const float point[3]) {
    if(!view_projection_row_major || !point) return std::nullopt;
    for(unsigned i=0;i<16;++i) if(!std::isfinite(view_projection_row_major[i])) return std::nullopt;
    for(unsigned i=0;i<3;++i) if(!std::isfinite(point[i])) return std::nullopt;
    std::array<float,4> clip{};
    for(unsigned row=0;row<4;++row) {
        for(unsigned column=0;column<3;++column) clip[row]+=view_projection_row_major[row*4+column]*point[column];
        clip[row]+=view_projection_row_major[row*4+3];
    }
    if(!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3]) || clip[3]<=1e-6f) return std::nullopt;
    const std::array<float,2> ndc{clip[0]/clip[3],clip[1]/clip[3]};
    if(!std::isfinite(ndc[0]) || !std::isfinite(ndc[1])) return std::nullopt;
    return ndc;
}

// Moves the game's own HUD reticle (arkHUDElementCrosshair) to where the VR hand
// aims, reusing the pinned build's own placement path instead of a second widget.
// 0x672F70 is the element's per-frame update: it returns immediately unless the
// movie bound at element+0x98 is live, then either parks the reticle at the
// stage centre or projects the world point on its checked component. This hook
// runs the original first and then writes x/y on the Scaleform object the
// element caches at +0x3A8 through the member handles at +0x660/+0x670 with the
// engine's own arkIggyAS3Object::SetAS3Value<float> (0x2A2C0, value by pointer),
// so the engine's teardown, Scaleform handles and stage arithmetic stay its own.
//
// The centred latch at +0x94 (the decompile's `element+0x94` byte). Writing after
// the original is enough to own the frame: when the component is centred and the
// latch is set the engine's tail does nothing at all, and when it is clear the
// centre helper runs inside the original, i.e. before this write. The latch is
// cleared once on the first unowned update, with retry after a failed write: the
// next engine update re-parks the reticle at centre the moment this publication
// goes stale, the route is switched off, or the hook is disabled and unhooked -
// without the clear the last position written here would stay on screen for the
// rest of the mission. Clearing it can never move the reticle within the frame
// that clears it, since it happens after the original returned.
//
// Default-off: install(...,route) must be true and a fresh (<=150 ms) active
// publication must exist, or this writes nothing at all. A null movie or a
// non-pointer AS3 object is never written through: the movie is the engine's own
// teardown signal and the object is released as it clears. Writes are gated on
// logic()==2, the gameplay state the other native hooks use, so a reticle in a
// menu or a loading screen is the game's own.
class NativeCrosshair {
    using Update=void (__fastcall*)(void*);
    // arkIggyAS3Object::SetAS3Value<float>(object field address, &value, member handle field address) (MIT reference reticle_hook.h).
    using Setter=std::uint64_t (__fastcall*)(std::uintptr_t,const float*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Write=bool (*)(std::uintptr_t,const void*,std::size_t);
    using Logic=UINT (*)();
    struct Publication {float x{},y{};std::uint64_t epoch{},tick{};bool active{};};
    inline static NativeCrosshair* instance_{};
    inline static constexpr std::uintptr_t update_rva=0x672f70,setter_rva=0x2a2c0,setter_call_rva=0x67318f;
    inline static constexpr std::uintptr_t stage_x_rva=0x3989eac,stage_y_rva=0x3989f2c;
public:
    std::atomic<float> ndc_limit{1.f};
private:
    inline static constexpr std::uintptr_t movie_offset=0x98,object_offset=0x3a8;
    inline static constexpr std::uintptr_t x_member_offset=0x660,y_member_offset=0x670,centred_latch_offset=0x94;
    inline static constexpr std::uint64_t freshness_ms=150;
    // 0x672F70 from the pinned decompile: movie null check through the JZ that
    // leaves the function, then the update body's own prologue.
    inline static constexpr std::array<std::byte,32> expected{{
        std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x48},std::byte{0x89},std::byte{0x70},std::byte{0x20},std::byte{0x55},
        std::byte{0x48},std::byte{0x8d},std::byte{0x68},std::byte{0xa8},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x50},
        std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0x83},std::byte{0xb9},std::byte{0x98},std::byte{0x00},
        std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0x8b},std::byte{0xf1},std::byte{0x0f},std::byte{0x84}
    }};
    Update original_{};Setter setter_{};Read read_{};Write write_{};Logic logic_{};
    std::uintptr_t base_{};
    bool enabled_{};
    std::atomic<bool> active_{false},route_{false};
    std::atomic<UINT> depth_{};
    std::atomic<UINT64> calls_{},applied_{},rejected_{},skipped_{},latch_clears_{},latch_failures_{},samples_{};
    std::mutex mutex_;Publication publication_;std::ofstream log_;
    std::atomic<bool> placed_{false};
    inline static const std::byte zero_byte{};

    static bool executable(std::uintptr_t at) {
        return native_executable_cached(at);
    }

    // Runs on the thread that owns the element: the same call that is updating
    // it, immediately after the engine's own placement decisions for this frame.
    void place(void* element) noexcept {
        if(!element) return;
        try {
            const auto at=reinterpret_cast<std::uintptr_t>(element);element_.store(at,std::memory_order_relaxed);
            Publication publication;{std::lock_guard<std::mutex> lock(mutex_);publication=publication_;}
            const auto now=GetTickCount64();
            std::uintptr_t movie{};
            // The engine's own guard, and its teardown signal: while the movie is
            // null the AS3 object has been released and the engine touches neither.
            if(!read_(at+movie_offset,&movie,sizeof movie) || !movie) return;
            const auto owns=route_.load(std::memory_order_acquire) && logic_()==2 && publication.active &&
                publication.epoch && now>=publication.tick && now-publication.tick<=freshness_ms;
            if(!owns) {
                // Hand the reticle back once: clear the centred latch so the engine re-places its own.
                if(placed_.load()) {if(write_(at+centred_latch_offset,&zero_byte,1)) {placed_=false;++latch_clears_;}else ++latch_failures_;}
                ++skipped_;return;
            }
            std::uintptr_t object{};
            float stage_x{},stage_y{};
            bool applied=false;
            if(read_(at+object_offset,&object,sizeof object) && object &&
               read_(base_+stage_x_rva,&stage_x,sizeof stage_x) && read_(base_+stage_y_rva,&stage_y,sizeof stage_y) &&
               std::isfinite(stage_x) && std::isfinite(stage_y) && stage_x>0 && stage_y>0 && stage_x<1e6f && stage_y<1e6f &&
               std::isfinite(publication.x) && std::isfinite(publication.y)) {
                // A hand aimed off the screen edge parks the reticle on the edge
                // rather than dropping the whole correction for that frame.
                // ndc_limit>1 when the HUD is drawn shrunk about the centre (native_hud_rect.hpp):
                // the caller pre-divides the aim point so the drawn reticle still lands on it.
                const float limit=ndc_limit.load(std::memory_order_relaxed);
                const auto stage=crosshair_stage_position(std::clamp(publication.x,-limit,limit),
                    std::clamp(publication.y,-limit,limit),stage_x,stage_y);
                // The setter takes the ADDRESS of the cached object field and of the member handle fields.
                setter_(at+object_offset,&stage[0],reinterpret_cast<void*>(at+x_member_offset));
                setter_(at+object_offset,&stage[1],reinterpret_cast<void*>(at+y_member_offset));
                ++applied_;applied=true;placed_=true;
            } else ++rejected_;
            if(samples_<4000) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(samples_<4000) {
                    log_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<at<<','<<applied<<','<<publication.x<<','
                        <<publication.y<<','<<publication.epoch<<','<<(GetTickCount64()-publication.tick)<<','<<stage_x<<','<<stage_y<<'\n';
                    ++samples_;if(samples_%120==0) log_.flush();
                }
            }
        } catch(...) {++rejected_;}
    }
    static void __fastcall update(void* element) {
        auto& self=*instance_;++self.depth_;++self.calls_;
        __try {self.original_(element);self.place(element);}
        __finally {--self.depth_;}
    }
public:
    // Last crosshair element seen by the engine's update; its AS3 object is at element+object_offset.
    std::atomic<std::uintptr_t> element_{};
    static constexpr std::uintptr_t object_field=object_offset;
    // Render thread, once per published frame: the hand aim point projected into
    // the rendered (left) eye view, in normalised device coordinates. active
    // false, a zero epoch or a publication older than 150 ms hands the reticle
    // straight back to the game.
    void publish(float ndc_x,float ndc_y,std::uint64_t epoch,std::uint64_t tick,bool active) {
        std::lock_guard<std::mutex> lock(mutex_);publication_={ndc_x,ndc_y,epoch,tick,active};
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);publication_={};}
    void install(std::uintptr_t base,Read read,Write write,Logic logic,const std::filesystem::path& file,bool route=false) {
        if(!base || !read || !write || !logic || active_ || (instance_ && instance_!=this))
            throw std::runtime_error("Invalid native crosshair ownership");
        base_=base;read_=read;write_=write;logic_=logic;
        std::array<std::byte,32> code{};
        if(!read_(base_+update_rva,code.data(),code.size()) || code!=expected)
            throw std::runtime_error("Native crosshair update differs from the pinned executable");
        // The update's own call to the shared AS3 member setter is the only
        // pinned evidence that resolves 0x2A2C0, so the setter is verified by its
        // call site rather than by a prologue no artifact records.
        std::array<std::byte,5> call{};
        std::int32_t displacement{};
        if(!read_(base_+setter_call_rva,call.data(),call.size()) || call[0]!=std::byte{0xe8})
            throw std::runtime_error("Native crosshair member setter caller differs");
        std::memcpy(&displacement,call.data()+1,4);
        if(static_cast<std::intptr_t>(base_+setter_call_rva+5)+displacement!=static_cast<std::intptr_t>(base_+setter_rva))
            throw std::runtime_error("Native crosshair member setter target differs");
        if(!executable(base_+setter_rva)) throw std::runtime_error("Native crosshair member setter is not executable");
        setter_=reinterpret_cast<Setter>(base_+setter_rva);
        log_.open(file,std::ios::trunc);
        if(!log_) throw std::runtime_error("Cannot open native crosshair log");
        log_<<std::setprecision(9)<<"tick_ms,thread,element,applied,ndc_x,ndc_y,epoch,age_ms,stage_x,stage_y\n";
        {std::lock_guard<std::mutex> lock(mutex_);publication_={};}
        calls_=applied_=rejected_=skipped_=latch_clears_=latch_failures_=samples_=0;route_=route;instance_=this;
        const auto target=reinterpret_cast<void*>(base_+update_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native crosshair hook");
        active_.store(true,std::memory_order_release);
        if(MH_EnableHook(target)!=MH_OK) {active_.store(false,std::memory_order_release);throw std::runtime_error("Cannot enable native crosshair hook");}
        enabled_=true;
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);route_.store(false,std::memory_order_release);
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+update_rva))!=MH_OK) return false;
            enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<std::byte,32> code{};
        const bool restored=!original_ || (read_(base_+update_rva,code.data(),code.size()) && code==expected);
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();publication_={};} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 applied() const {return applied_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 skipped() const {return skipped_.load();}
    UINT64 latch_clears() const {return latch_clears_.load();}
    UINT64 latch_failures() const {return latch_failures_.load();}
    bool routing() const {return active_.load() && route_.load();}
};
} // namespace d2vr
