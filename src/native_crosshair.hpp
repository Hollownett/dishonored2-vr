#pragma once
#include "crash_observer.hpp"
#include "native_guarded_call.hpp"
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
// The HUD movie is width-fitted and centred in the eye target (native_hud_stage_fit), so the visible stage is stage_x wide and
// stage_x*eye_h/eye_w tall. An unknown or non-finite eye size falls back to the 16:9 stage the engine formula assumes.
inline std::array<float,2> crosshair_fitted_stage_position(float ndc_x,float ndc_y,float stage_x,float eye_w,float eye_h) noexcept {
    if(!(eye_w>0.f) || !(eye_h>0.f) || !std::isfinite(eye_w) || !std::isfinite(eye_h)) return crosshair_stage_position(ndc_x,ndc_y,stage_x,stage_x*9.f/16.f);
    return {0.5f*ndc_x*stage_x,-0.5f*ndc_y*stage_x*eye_h/eye_w};
}
// Paired stereo draws the UI once per eye: the second draw moves by (eye-placed) stage pixels, times the movie root scale.
inline std::array<float,2> crosshair_eye_shift_px(std::array<float,2> stage_eye,std::array<float,2> stage_placed,float root_s) noexcept {
    return {(stage_eye[0]-stage_placed[0])*root_s,(stage_eye[1]-stage_placed[1])*root_s};
}
// Distance to the reticle target: the weapon-segment cast's hit fraction along `length` (clamped to .5 m), else `length`.
inline float crosshair_distance(std::optional<double> hit_fraction,double length=30.) noexcept {
    if(!hit_fraction || !std::isfinite(*hit_fraction)) return static_cast<float>(length);
    return static_cast<float>(std::clamp(*hit_fraction*length,.5,length));
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
// publication must exist, or this writes nothing at all. A null movie or an
// empty AS3 id list is never written through, and a faulting setter stops all writes until reinstall: the movie is the engine's own
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
    struct Binding {
        std::uintptr_t element{},movie{},iggy{},list{};std::uint32_t count{};std::uint64_t tick{};
        std::array<std::byte,0x28> record{};
    };
    using TargetSize=std::array<float,2> (*)() noexcept; // the eye target size in pixels ({0,0} unknown); NativeHudChildDraw::target_size
    std::atomic<TargetSize> target_size{};
private:
    inline static constexpr std::uintptr_t movie_offset=0x98,object_offset=0x3a8;
    // arkIggyAS3Object layout (REA): +0x18 idList data pointer, +0x20 count in the low 24 bits. The setter writes through
    // entry[count-1], so an empty list (a cleared object) would write through data-0x28. The qword at +0 is the vtable, never null.
    inline static constexpr std::uintptr_t as3_data_offset=0x18,as3_count_offset=0x20;
    inline static constexpr std::uint32_t as3_count_mask=0xffffff;
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
    std::mutex mutex_;Publication publication_;Binding binding_;std::ofstream log_;
    // Per displayed frame, binding() validates the element against the live engine once and answers every further call of that frame from
    // this cache (the HUD child draw asks per panel child). frame_==0 (no begin_frame yet) means no caching. generation_ moves whenever the
    // remembered binding changes or is cleared, so a teardown seen by place() is never answered from the cache. The render thread only ever
    // try_locks: a busy lock means "no ownership this call" and is not cached.
    std::atomic<std::uint64_t> frame_{},generation_{};
    std::atomic<std::uint64_t> validations_{},cache_hits_{};
    struct Validated {std::uint64_t frame{},generation{},tick{};std::optional<Binding> result;};
    std::mutex cache_mutex_;Validated cache_;
    inline static constexpr std::uint64_t cache_max_age_ms=50; // a stalled frame counter can never serve an old answer
    static bool same_identity(const Binding& a,const Binding& b) noexcept {
        return a.element==b.element && a.movie==b.movie && a.iggy==b.iggy && a.list==b.list && a.count==b.count && a.record==b.record;
    }
    std::atomic<bool> placed_{false},failed_{false};
    inline static const std::byte zero_byte{};

    struct Validation {std::optional<Binding> binding;bool cacheable{};};
    // The engine reads. Never blocks: when the update thread holds mutex_ there is no snapshot to judge, so nothing is owned (and nothing cached).
    Validation validate(std::uint64_t now) noexcept {
        try {
            if(logic_()!=2) return {{},true};
            Binding out;
            if(!mutex_.try_lock()) return {};
            out=binding_;mutex_.unlock();
            if(!out.element || !out.tick || now<out.tick || now-out.tick>250) return {{},true};
            Binding current{out.element,0,0,0,0,out.tick};
            if(!read_(out.element+movie_offset,&current.movie,8) || current.movie!=out.movie || !current.movie ||
               !read_(out.element+object_offset+8,&current.iggy,8) || current.iggy!=out.iggy ||
               !read_(out.element+object_offset+as3_data_offset,&current.list,8) || current.list!=out.list ||
               !read_(out.element+object_offset+as3_count_offset,&current.count,4) || current.count!=out.count ||
               !read_(out.list+((out.count&as3_count_mask)-1)*0x28,current.record.data(),current.record.size()) || current.record!=out.record) return {{},true};
            return {out,true};
        } catch(...) {return {};}
    }

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
            if(!read_(at+movie_offset,&movie,sizeof movie) || !movie) {std::lock_guard<std::mutex> lock(mutex_);binding_={};generation_.fetch_add(1,std::memory_order_release);return;}
            Binding live{at,movie,0,0,0,now};
            if(!read_(at+object_offset+8,&live.iggy,8) || !live.iggy ||
               !read_(at+object_offset+as3_data_offset,&live.list,8) || !live.list ||
               !read_(at+object_offset+as3_count_offset,&live.count,4) || !(live.count&as3_count_mask) || (live.count&as3_count_mask)>16 ||
               !read_(live.list+((live.count&as3_count_mask)-1)*0x28,live.record.data(),live.record.size())) live={};
            {std::lock_guard<std::mutex> lock(mutex_);
             if(!same_identity(binding_,live)) generation_.fetch_add(1,std::memory_order_release);
             binding_=live;}
            const auto owns=!failed_.load(std::memory_order_acquire) && route_.load(std::memory_order_acquire) && logic_()==2 && publication.active &&
                publication.epoch && now>=publication.tick && now-publication.tick<=freshness_ms;
            if(!owns) {
                // Hand the reticle back once: clear the centred latch so the engine re-places its own.
                if(placed_.load()) {if(write_(at+centred_latch_offset,&zero_byte,1)) {placed_=false;++latch_clears_;}else ++latch_failures_;}
                ++skipped_;return;
            }
            std::uintptr_t list{};std::uint32_t count{};
            float stage_x{},stage_y{};
            bool applied=false;
            if(read_(at+object_offset+as3_data_offset,&list,sizeof list) && list &&
               read_(at+object_offset+as3_count_offset,&count,sizeof count) && (count&as3_count_mask) &&
               read_(base_+stage_x_rva,&stage_x,sizeof stage_x) && read_(base_+stage_y_rva,&stage_y,sizeof stage_y) &&
               std::isfinite(stage_x) && std::isfinite(stage_y) && stage_x>0 && stage_y>0 && stage_x<1e6f && stage_y<1e6f &&
               std::isfinite(publication.x) && std::isfinite(publication.y)) {
                // A hand aimed off the screen edge parks the reticle on the edge
                // rather than dropping the whole correction for that frame. The HUD scale is compensated once,
                // by native_crosshair_unlayout in the draw path, so nothing is pre-scaled here.
                const auto size=target_size.load(std::memory_order_acquire);const auto eye=size?size():std::array<float,2>{};
                const auto stage=crosshair_fitted_stage_position(std::clamp(publication.x,-1.f,1.f),std::clamp(publication.y,-1.f,1.f),stage_x,eye[0],eye[1]);
                // The setter takes the ADDRESS of the cached object field and of the member handle fields.
                // SEH-guarded: the AS3 object can be torn down under the engine during load/death, and catch(...) does not see an AV.
                auto write_both=[&]{
                    setter_(at+object_offset,&stage[0],reinterpret_cast<void*>(at+x_member_offset));
                    setter_(at+object_offset,&stage[1],reinterpret_cast<void*>(at+y_member_offset));
                };
                CrashOperation operation("native crosshair setter");
                if(guarded_call(write_both)) {++applied_;applied=true;placed_=true;}
                else {++rejected_;failed_.store(true,std::memory_order_release);} // stop writing until reinstall
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
    void clear() {std::lock_guard<std::mutex> lock(mutex_);publication_={};binding_={};generation_.fetch_add(1,std::memory_order_release);}
    // Render thread, once per displayed frame: binding() validates at most once until the next call. 0 turns the cache off.
    void begin_frame(std::uint64_t frame) noexcept {frame_.store(frame,std::memory_order_relaxed);}
    UINT64 validations() const {return validations_.load();}
    UINT64 cache_hits() const {return cache_hits_.load();}
    std::optional<Binding> binding(std::uint64_t now) noexcept {
        try {
            if(!active_.load() || !route_.load() || failed_.load() || !logic_) return {};
            const auto frame=frame_.load(std::memory_order_relaxed);
            const auto generation=generation_.load(std::memory_order_acquire);
            if(frame && cache_mutex_.try_lock()) {
                const bool hit=cache_.frame==frame && cache_.generation==generation && now>=cache_.tick && now-cache_.tick<=cache_max_age_ms;
                const auto cached=hit?cache_.result:std::nullopt;
                cache_mutex_.unlock();
                if(hit) {++cache_hits_;return cached;}
            }
            ++validations_;
            const auto result=validate(now);
            if(!result.cacheable) return {};
            if(frame && cache_mutex_.try_lock()) {cache_={frame,generation,now,result.binding};cache_mutex_.unlock();}
            return result.binding;
        } catch(...) {return {};}
    }
    bool owns(const Binding& candidate,std::uint64_t now) noexcept {
        const auto live=binding(now);
        return live && live->element==candidate.element && live->movie==candidate.movie && live->iggy==candidate.iggy &&
            live->list==candidate.list && live->count==candidate.count && live->record==candidate.record;
    }
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
        {std::lock_guard<std::mutex> lock(mutex_);publication_={};binding_={};generation_.fetch_add(1,std::memory_order_release);}
        calls_=applied_=rejected_=skipped_=latch_clears_=latch_failures_=samples_=0;failed_=false;route_=route;instance_=this;
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
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();publication_={};binding_={};generation_.fetch_add(1,std::memory_order_release);} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 applied() const {return applied_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 skipped() const {return skipped_.load();}
    bool failed() const {return failed_.load(std::memory_order_acquire);}
    UINT64 latch_clears() const {return latch_clears_.load();}
    UINT64 latch_failures() const {return latch_failures_.load();}
    bool routing() const {return active_.load() && route_.load();}
};
} // namespace d2vr
