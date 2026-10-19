#pragma once
// Carry diagnostics (C7, G6): what the render-breaks-while-carrying candidates (research Q6) look like frame by frame.
// Observation only: nothing here writes to the engine, throws, or blocks. note() runs on the render thread (single writer);
// json() runs on the status thread, so every counter is an atomic. A per-frame trace goes through csv_row() behind build/carry-trace.
//   1 DEBRIS swap lost       rigid_frames (layer still RIGID_BODY while held), swaps/restores, clears[]
//   2 native rebind 0x753c10 joint_off_frames (rf+0x3d0 no longer -1 after the anchor), max_rf_norm_dev
//   3 torn anchor quaternion max_quat_dev (anchor, pre-normalise), max_rf_norm_dev (render thread read of rf+0x3e0)
//   4 view-model scale       scale_frames (render scale +0xc8 non-zero after the grace window)
//   5 stale palm residual    max_residual_age_ms
//   6 master re-base         clamp_hits_held and the CSV: the held entity hit by the hand's own sweeps
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace d2vr {
struct CarryFrame {
    std::uint64_t tick{};std::uintptr_t entity{};std::uint32_t layer{0xff};float render_scale{};
    std::int64_t residual_age_ms{-1};float rf_quat_norm{};float anchor_quat_dev{};unsigned clear_reason{};bool clamp_hit_held{};
    std::int32_t rf_joint{0x7fff}; // rf+0x3d0 (int16): -1 once the mod anchored it to the master; 0x7fff = not read
};
class CarryDiagnostics {
public:
    static constexpr unsigned csv_max_rows=20000;
    static constexpr unsigned grace_frames=2;     // HoldObject's own view-model scale is expected on the first frames of a hold
    static constexpr std::uint64_t gap_ms=250;    // a longer gap between frames is a new hold
    static constexpr std::uint32_t layer_rigid_body=3;
    // Render thread.
    void note(const CarryFrame& f) noexcept {
        if(!f.entity || f.entity!=entity_ || f.tick<last_tick_ || f.tick-last_tick_>gap_ms) hold_frame_=0;
        entity_=f.entity;last_tick_=f.tick;
        const auto index=hold_frame_++;
        holding_.store(true,std::memory_order_relaxed);
        ++frames_;
        if(f.layer==layer_rigid_body) ++rigid_frames_;
        if(index>=grace_frames && f.render_scale!=0.f) ++scale_frames_;
        if(f.clamp_hit_held) ++clamp_hits_held_;
        if(index>=grace_frames+1 && f.rf_joint!=0x7fff && f.rf_joint!=-1) ++joint_off_frames_;
        raise(max_residual_age_ms_,f.residual_age_ms);
        raise(max_quat_dev_,std::isfinite(f.anchor_quat_dev)?f.anchor_quat_dev:1e9f);
        if(f.rf_quat_norm!=0.f) raise(max_rf_norm_dev_,std::isfinite(f.rf_quat_norm)?std::abs(f.rf_quat_norm-1.f):1e9f);
    }
    // Any thread. Only a clear that ends a published hold counts (the hold-less frames also call clear()).
    void clear_seen(unsigned reason) noexcept {
        if(!holding_.exchange(false,std::memory_order_relaxed)) return;
        ++clears_[reason<clears_.size()?reason:0];
    }
    void layers(std::uint64_t swapped,std::uint64_t restored) noexcept {swaps_.store(swapped,std::memory_order_relaxed);restores_.store(restored,std::memory_order_relaxed);}
    std::string json() const {
        try {
            std::string o="{\"frames\":"+std::to_string(frames_.load())+",\"rigid_frames\":"+std::to_string(rigid_frames_.load())+
                ",\"scale_frames\":"+std::to_string(scale_frames_.load())+",\"clears\":[";
            for(unsigned i=0;i<clears_.size();++i) o+=(i?",":"")+std::to_string(clears_[i].load());
            const auto age=max_residual_age_ms_.load();
            o+="],\"swaps\":"+std::to_string(swaps_.load())+",\"restores\":"+std::to_string(restores_.load())+
                ",\"max_residual_age_ms\":"+(age<0?std::string("null"):std::to_string(age))+
                ",\"max_quat_dev\":"+num(max_quat_dev_.load())+",\"max_rf_norm_dev\":"+num(max_rf_norm_dev_.load())+
                ",\"joint_off_frames\":"+std::to_string(joint_off_frames_.load())+",\"clamp_hits_held\":"+std::to_string(clamp_hits_held_.load())+"}";
            return o;
        } catch(...) {return "null";}
    }
    static std::string csv_header() {
        return "tick,entity,layer,render_scale,residual_age_ms,rf_quat_norm,anchor_quat_dev,clear_reason,clamp_hit_held,rf_joint";
    }
    static std::string csv_row(const CarryFrame& f) {
        try {
            return std::to_string(f.tick)+",0x"+hex(f.entity)+","+std::to_string(f.layer)+","+num(f.render_scale)+","+
                (f.residual_age_ms<0?std::string("null"):std::to_string(f.residual_age_ms))+","+num(f.rf_quat_norm)+","+
                num(f.anchor_quat_dev)+","+std::to_string(f.clear_reason)+","+(f.clamp_hit_held?"1":"0")+","+std::to_string(f.rf_joint);
        } catch(...) {return {};}
    }
private:
    static std::string num(float v) {
        if(!std::isfinite(v)) return "null";
        char b[32];std::snprintf(b,sizeof b,"%.6g",static_cast<double>(v));return b;
    }
    static std::string hex(std::uintptr_t v) {char b[24];std::snprintf(b,sizeof b,"%llx",static_cast<unsigned long long>(v));return b;}
    template<class T> static void raise(std::atomic<T>& a,T v) noexcept {
        for(T c=a.load(std::memory_order_relaxed);v>c && !a.compare_exchange_weak(c,v,std::memory_order_relaxed););
    }
    std::uintptr_t entity_{};std::uint64_t last_tick_{};unsigned hold_frame_{};   // render thread only
    std::atomic<bool> holding_{};
    std::atomic<std::uint64_t> frames_{},rigid_frames_{},scale_frames_{},clamp_hits_held_{},joint_off_frames_{},swaps_{},restores_{};
    std::array<std::atomic<std::uint64_t>,4> clears_{};
    std::atomic<std::int64_t> max_residual_age_ms_{-1};
    std::atomic<float> max_quat_dev_{},max_rf_norm_dev_{};
};
}
