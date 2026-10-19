#pragma once
// Carry diagnostics (C7, G6): what the render-breaks-while-carrying candidates (research Q6) look like frame by frame.
// Observation only: nothing here writes to the engine, throws, or blocks the present thread. note() runs on the render thread
// (single writer) for accepted publishes only; json() runs on the status thread, so every counter is an atomic. The per-frame trace
// goes through CarryTraceWriter (bounded ring, formatted and written on its own thread) behind build/carry-trace.
//   1 DEBRIS swap lost       rigid_after_swap / layer_flips (RIGID_BODY seen again after DEBRIS in the same hold); swaps/restores per row
//   2 native rebind 0x753c10 rebind_overwrites (anchor() read back rf+0x3d0/0x3d4/0x3e0 and found them changed since its own write)
//   3 torn anchor quaternion bad_quat_reads (two render-thread reads agree and |norm-1|>1e-4); disagreeing reads = probe tear, not counted
//   4 view-model scale       scale_frames (+0xc8 non-zero after the hold's clear request had time to land), scale_clear_frames_max
//   5 stale palm residual    residual_none_frames / max_residual_age_ms (age only while the residual is applied, <=500 ms)
//   6 master re-base         max_entity_err, max_master_drift
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace d2vr {
struct CarryFrame {
    static constexpr float unread=-1.f;            // sentinel for the float probes: a failed read is never a healthy value
    static constexpr std::int32_t joint_unread=0x7fff;
    static constexpr std::uint32_t layer_unread=0xff;
    std::uint64_t tick{};std::uintptr_t entity{};std::uint64_t hold_serial{};std::uint64_t hold_id{};std::uint32_t layer{layer_unread};
    float render_scale{unread};std::int64_t residual_age_ms{-1};          // -1: no residual applied this frame
    float rf_quat_norm{unread},rf_quat_norm2{unread};                      // two reads of rf+0x3e0 on the render thread
    unsigned clear_reason{};bool clamp_hit_held{};
    std::int32_t rf_joint{joint_unread};                                   // rf+0x3d0 (int16); -1 once anchored to the master
    float entity_err{unread};                                              // |entity+0x300 - the bind's post-carry target| at the last bind tick
    float master_pos[3]{};bool master_valid{};float master_drift{unread};  // the published master origin; drift (stamped by note) = its step since the previous frame of the hold
    std::uint64_t rebinds{},rebinds_hold{};                                // lifetime rebind_overwrites; the hold's share (stamped by note)
    float hand_speed{unread};                                              // m/s of the controller; entity_err only counts while the hand is slow
    std::uint64_t swaps{},restores{};                                      // lifetime layer swap / restore counts at this frame
};
struct CarryEngineCounters {
    std::uint64_t swaps{},restores{},rebind_overwrites{},anchored{};
    std::array<std::uint64_t,5> raw_clears{};
};
class CarryDiagnostics {
public:
    static constexpr unsigned grace_frames=2;      // frames the hold's clear request may take to land before a non-zero scale counts
    static constexpr std::uint32_t layer_rigid_body=3,layer_debris=4;
    static constexpr float quat_bad=1e-4f,quat_agree=1e-6f,entity_err_bad=.10f,slow_hand=.3f;
    static constexpr unsigned reasons=5;           // 0 unattributed, 1 rf/entity gone, 2 placement refused, 3 no hold, 4 tracking dropout
    // Render thread. Returns the frame as recorded (hold id and the latched clear reason stamped) for the trace.
    CarryFrame note(CarryFrame f) noexcept {
        if(!f.entity || f.entity!=entity_ || f.hold_serial!=serial_ || !hold_id_) begin_hold(f);
        const auto index=hold_frame_++;
        if(!hold_rebind_known_) {hold_rebind_base_=f.rebinds;hold_rebind_known_=true;}
        f.rebinds_hold=f.rebinds>=hold_rebind_base_?f.rebinds-hold_rebind_base_:0;raise<std::uint64_t>(rebinds_hold_max_,f.rebinds_hold);
        f.master_drift=CarryFrame::unread;
        if(f.master_valid && std::isfinite(f.master_pos[0]) && std::isfinite(f.master_pos[1]) && std::isfinite(f.master_pos[2])) {
            if(have_master_) {
                const float dx=f.master_pos[0]-master_prev_[0],dy=f.master_pos[1]-master_prev_[1],dz=f.master_pos[2]-master_prev_[2];
                f.master_drift=std::sqrt(dx*dx+dy*dy+dz*dz);raise(max_master_drift_,f.master_drift);
            }
            for(unsigned i=0;i<3;++i) master_prev_[i]=f.master_pos[i];
            have_master_=true;
        } else have_master_=false;
        f.hold_id=hold_id_;f.clear_reason=latched_.exchange(0,std::memory_order_relaxed);
        holding_.store(true,std::memory_order_relaxed);
        ++frames_;
        if(f.layer==CarryFrame::layer_unread) ++layer_unknown_frames_;
        else {
            if(f.layer==layer_rigid_body) {
                ++rigid_frames_;
                if(seen_debris_) {++rigid_after_swap_;if(last_layer_==layer_debris) ++layer_flips_;}
            } else if(f.layer==layer_debris) seen_debris_=true;
            last_layer_=f.layer;
        }
        if(f.render_scale<0.f || !std::isfinite(f.render_scale)) ++failed_reads_;
        else if(f.render_scale!=0.f) {
            if(req_frame_<0) req_frame_=static_cast<long long>(index);
            if(scale_cleared_) {++scale_frames_;++scale_reentries_;scale_cleared_=false;}
            else if(index>req_frame_+grace_frames) ++scale_frames_;
        } else if(req_frame_>=0 && !scale_cleared_) {
            scale_cleared_=true;raise<std::uint64_t>(scale_clear_max_,index-static_cast<std::uint64_t>(req_frame_));
        }
        if(f.rf_joint==CarryFrame::joint_unread) ++failed_reads_;
        if(f.rf_quat_norm<0.f || f.rf_quat_norm2<0.f || !std::isfinite(f.rf_quat_norm) || !std::isfinite(f.rf_quat_norm2)) ++failed_reads_;
        else if(std::abs(f.rf_quat_norm-f.rf_quat_norm2)>quat_agree) ++quat_read_disagree_;
        else {
            const float dev=std::abs(f.rf_quat_norm-1.f);
            if(dev>quat_bad) {++bad_quat_reads_;raise(max_rf_norm_dev_,dev);}
        }
        if(f.residual_age_ms<0) ++residual_none_frames_; else raise<std::int64_t>(max_residual_age_ms_,f.residual_age_ms);
        if(f.entity_err<0.f || !std::isfinite(f.entity_err)) ++failed_reads_;
        else {
            raise(max_entity_err_,f.entity_err);
            if(f.hand_speed>=0.f && f.hand_speed<slow_hand) {raise(max_entity_err_slow_,f.entity_err);if(f.entity_err>entity_err_bad) ++entity_err_frames_;}
        }
        if(f.clamp_hit_held) ++clamp_hits_held_;
        return f;
    }
    // Render thread: a publish the native side refused (nothing was bound this frame).
    void rejected() noexcept {++rejected_publishes_;}
    // Any thread. Only a clear that ends a published hold counts (the hold-less frames also call clear()); its reason is stamped on the next row.
    void clear_seen(unsigned reason) noexcept {
        if(!holding_.exchange(false,std::memory_order_relaxed)) return;
        const unsigned r=reason<reasons?reason:0;
        ++clears_[r];latched_.store(r,std::memory_order_relaxed);
    }
    void engine(const CarryEngineCounters& c) noexcept {
        swaps_=c.swaps;restores_=c.restores;rebind_overwrites_=c.rebind_overwrites;anchored_=c.anchored;
        for(unsigned i=0;i<reasons;++i) raw_clears_[i]=c.raw_clears[i];
    }
    std::string json() const {
        try {
            std::string o="{\"frames\":"+n(frames_)+",\"holds\":"+std::to_string(holds_.load())+",\"rigid_frames\":"+n(rigid_frames_)+
                ",\"rigid_after_swap\":"+n(rigid_after_swap_)+",\"layer_flips\":"+n(layer_flips_)+",\"layer_unknown_frames\":"+n(layer_unknown_frames_)+
                ",\"scale_frames\":"+n(scale_frames_)+",\"scale_reentries\":"+n(scale_reentries_)+",\"scale_clear_frames_max\":"+n(scale_clear_max_)+
                ",\"clears\":"+list(clears_)+",\"raw_clears\":"+list(raw_clears_)+
                ",\"swaps\":"+n(swaps_)+",\"restores\":"+n(restores_)+",\"anchored\":"+n(anchored_)+",\"rebind_overwrites\":"+n(rebind_overwrites_)+
                ",\"bad_quat_reads\":"+n(bad_quat_reads_)+",\"quat_read_disagree\":"+n(quat_read_disagree_)+",\"max_rf_norm_dev\":"+num(max_rf_norm_dev_.load())+
                ",\"residual_none_frames\":"+n(residual_none_frames_);
            const auto age=max_residual_age_ms_.load();
            o+=",\"max_residual_age_ms\":"+(age<0?std::string("null"):std::to_string(age))+
                ",\"max_entity_err\":"+num(max_entity_err_.load())+",\"max_entity_err_slow\":"+num(max_entity_err_slow_.load())+",\"entity_err_frames\":"+n(entity_err_frames_)+
                ",\"rebinds_hold_max\":"+n(rebinds_hold_max_)+
                ",\"max_master_drift\":"+num(max_master_drift_.load())+",\"failed_reads\":"+n(failed_reads_)+
                ",\"rejected_publishes\":"+n(rejected_publishes_)+",\"clamp_hits_held\":"+n(clamp_hits_held_)+"}";
            return o;
        } catch(...) {return "null";}
    }
    static std::string csv_header() {
        return "tick,hold,entity,layer,swaps,restores,render_scale,residual_age_ms,rf_quat_norm,rf_quat_norm2,clear_reason,clamp_hit_held,rf_joint,entity_err,master_drift,rebinds_hold,hand_speed";
    }
    static std::string csv_row(const CarryFrame& f) {
        try {
            return std::to_string(f.tick)+","+std::to_string(f.hold_id)+",0x"+hex(f.entity)+","+
                (f.layer==CarryFrame::layer_unread?std::string("null"):std::to_string(f.layer))+","+std::to_string(f.swaps)+","+std::to_string(f.restores)+","+
                fnum(f.render_scale)+","+(f.residual_age_ms<0?std::string("null"):std::to_string(f.residual_age_ms))+","+fnum(f.rf_quat_norm)+","+fnum(f.rf_quat_norm2)+","+
                std::to_string(f.clear_reason)+","+(f.clamp_hit_held?"1":"0")+","+
                (f.rf_joint==CarryFrame::joint_unread?std::string("null"):std::to_string(f.rf_joint))+","+fnum(f.entity_err)+","+fnum(f.master_drift)+","+std::to_string(f.rebinds_hold)+","+fnum(f.hand_speed);
        } catch(...) {return {};}
    }
    static constexpr unsigned csv_max_rows=20000;
private:
    static std::string num(float v) {
        if(!std::isfinite(v)) return "null";
        char b[32];std::snprintf(b,sizeof b,"%.6g",static_cast<double>(v));return b;
    }
    static std::string fnum(float v) {return v<0.f?std::string("null"):num(v);}
    static std::string hex(std::uintptr_t v) {char b[24];std::snprintf(b,sizeof b,"%llx",static_cast<unsigned long long>(v));return b;}
    static std::string n(const std::atomic<std::uint64_t>& a) {return std::to_string(a.load());}
    static std::string list(const std::array<std::atomic<std::uint64_t>,reasons>& a) {
        std::string o="[";for(unsigned i=0;i<a.size();++i) o+=(i?",":"")+std::to_string(a[i].load());return o+"]";
    }
    template<class T> static void raise(std::atomic<T>& a,T v) noexcept {
        for(T c=a.load(std::memory_order_relaxed);v>c && !a.compare_exchange_weak(c,v,std::memory_order_relaxed););
    }
    void begin_hold(const CarryFrame& f) noexcept {
        entity_=f.entity;serial_=f.hold_serial;hold_frame_=0;++hold_id_;++holds_;
        seen_debris_=false;last_layer_=CarryFrame::layer_unread;req_frame_=-1;scale_cleared_=false;hold_rebind_known_=false;have_master_=false;
    }
    // render thread only: the current hold (entity + publication serial; a long frame never ends one)
    std::uintptr_t entity_{};std::uint64_t serial_{},hold_id_{};unsigned hold_frame_{};bool seen_debris_{},scale_cleared_{};
    std::uint32_t last_layer_{CarryFrame::layer_unread};long long req_frame_{-1};
    std::uint64_t hold_rebind_base_{};bool hold_rebind_known_{},have_master_{};float master_prev_[3]{};
    std::atomic<bool> holding_{};std::atomic<unsigned> latched_{};
    std::atomic<std::uint64_t> holds_{},frames_{},rigid_frames_{},rigid_after_swap_{},layer_flips_{},layer_unknown_frames_{},scale_frames_{},scale_reentries_{},
        scale_clear_max_{},rebinds_hold_max_{},clamp_hits_held_{},swaps_{},restores_{},anchored_{},rebind_overwrites_{},bad_quat_reads_{},quat_read_disagree_{},
        residual_none_frames_{},entity_err_frames_{},failed_reads_{},rejected_publishes_{};
    std::array<std::atomic<std::uint64_t>,reasons> clears_{},raw_clears_{};
    std::atomic<std::int64_t> max_residual_age_ms_{-1};
    std::atomic<float> max_rf_norm_dev_{},max_entity_err_{},max_entity_err_slow_{},max_master_drift_{};
};

// The per-frame trace: the render thread only copies a CarryFrame into a bounded ring; this thread formats and writes the rows,
// flushes about once a second and at stop(). A full ring drops the frame (counted). Never throws into the caller.
class CarryTraceWriter {
public:
    static constexpr std::size_t ring=1024;
    ~CarryTraceWriter() {stop();}   // a joinable thread must never be destroyed (static destruction on a teardown path that skipped stop())
    bool start(const std::filesystem::path& path) noexcept {
        try {
            if(running_.load()) return true;
            out_.open(path,std::ios::trunc);if(!out_.is_open()) return false;
            out_<<CarryDiagnostics::csv_header()<<'\n';
            head_=0;tail_=0;pushed_=0;written_=0;dropped_=0;
            running_=true;worker_=std::thread([this] {run();});
            return true;
        } catch(...) {running_=false;try {out_.close();} catch(...) {}return false;}
    }
    void push(const CarryFrame& f) noexcept {
        if(!running_.load(std::memory_order_acquire) || pushed_>=CarryDiagnostics::csv_max_rows) return;
        const auto h=head_.load(std::memory_order_relaxed);
        if(h-tail_.load(std::memory_order_acquire)>=ring) {++dropped_;return;}
        buf_[h%ring]=f;head_.store(h+1,std::memory_order_release);++pushed_;
    }
    void stop() noexcept {
        try {
            if(!running_.exchange(false)) return;
            if(worker_.joinable()) worker_.join();
            drain();out_.flush();out_.close();
        } catch(...) {}
    }
    std::uint64_t written() const noexcept {return written_.load();}
    std::uint64_t dropped() const noexcept {return dropped_.load();}
private:
    void drain() noexcept {
        try {
            std::string text;
            for(auto t=tail_.load(std::memory_order_relaxed);t<head_.load(std::memory_order_acquire);++t) {
                text+=CarryDiagnostics::csv_row(buf_[t%ring]);text+='\n';++written_;tail_.store(t+1,std::memory_order_release);
            }
            if(!text.empty()) out_<<text;
        } catch(...) {}
    }
    void run() noexcept {
        auto flushed=std::chrono::steady_clock::now();
        while(running_.load()) {
            drain();
            const auto now=std::chrono::steady_clock::now();
            if(now-flushed>=std::chrono::seconds(1)) {try {out_.flush();} catch(...) {}flushed=now;}
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    std::array<CarryFrame,ring> buf_{};std::atomic<std::uint64_t> head_{},tail_{};
    std::atomic<std::uint64_t> written_{},dropped_{};std::uint64_t pushed_{};
    std::atomic<bool> running_{};std::thread worker_;std::ofstream out_;
};
}
