#pragma once
#include "full_body_driver.hpp"
#include "game_camera.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <ostream>
#include <iomanip>
#include <limits>

namespace d2vr {
struct SynchronizedTracking {
    std::uint64_t tick{},revision{},native_frame{},present{},pose_epoch{},reference_epoch{},head_flags{},source_tick{};
    std::int64_t prediction_time{};
    TrackedPose head{},grip[2]{},aim[2]{},origin{};
    std::uint64_t grip_flags[2]{},aim_flags[2]{};
    double grip_value[2]{},trigger_value[2]{};
    GameCameraPose camera{};
    Vec3 controller_centre{};
};
struct SynchronizedConsumer {
    SynchronizedTracking raw{};
    fullbody::Diagnostic solver{};
    std::array<rig::Transform,69> palette{};
    std::array<float,15> model_frame{};
    std::uintptr_t model{},source{},player{},animation{},skeleton{};
    std::uint32_t owner_generation{},owner_flags{},count{},source_kind{},gate{},material_reason{};
    std::uint64_t tick{},callback_sequence{},consumer_native_frame{},consumer_present{},refreshes{},refresh_deferred{},query_outcomes[5]{};
    double preparation_ms{},consumer_ms{},material_ms{},refresh_ms{};
    bool replaced{},palette_valid{},consistent{};
    body::Xf actual_palm{},actual_wrist{},actual_pelvis{},actual_feet[2]{};
    bool landmarks_valid{};
    std::uintptr_t selected_component{},focus_component[2]{},held_component[2]{};
    std::uint64_t selection_epoch{},selection_revision{},selection_tick{},grab_tick[2]{},grab_epoch[2]{},acquisition_tick[2]{},focus_calls[2]{},focus_us[2]{},discovery_slices[2]{};
    bool selection_qualified{};
    std::uint64_t grab_decision_tick[2]{},grab_source_tick[2]{},grab_source_age_ms[2]{};
    unsigned grab_gate[2]{},grab_role[2]{},grab_candidates[2]{};
    double grab_raw[2]{};
    bool grab_age_valid[2]{},grab_armed[2]{},grab_deferred[2]{},grab_occupied[2]{},grab_usable[2]{};
};
// Opt-in observer: fixed storage, try-lock publication, at most 10 samples/s
// for each owned consumer. Only the probe worker serializes or writes files.
class SynchronizedObserver {
    mutable std::mutex mutex_;
    std::array<SynchronizedConsumer,128> ring_{};
    SynchronizedTracking tracking_{};
    std::array<std::uint64_t,3> last_{};
    std::size_t read_{},size_{};
    std::atomic<bool> enabled_{};
    std::atomic<std::uint64_t> dropped_{},serial_{};
public:
    void enable(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);tracking_={};last_={};read_=size_=0;
        dropped_=serial_=0;enabled_=enabled;
    }
    bool enabled() const noexcept {return enabled_.load(std::memory_order_relaxed);}
    void stop() noexcept {enabled_=false;}
    std::uint64_t dropped() const noexcept {return dropped_.load();}
    void tracking(const SynchronizedTracking& value) {
        if(!enabled())return;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++dropped_;return;}tracking_=value;
    }
    std::optional<SynchronizedConsumer> begin(unsigned kind,std::uint64_t tick) {
        if(!enabled() || kind>=3)return {};
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++dropped_;return {};}
        if(last_[kind] && tick>=last_[kind] && tick-last_[kind]<100)return {};
        last_[kind]=tick;SynchronizedConsumer value{};value.raw=tracking_;value.tick=tick;
        value.source_kind=kind;value.callback_sequence=++serial_;return value;
    }
    void publish(const SynchronizedConsumer& value) {
        if(!enabled())return;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock() || size_==ring_.size()){++dropped_;return;}
        ring_[(read_+size_)%ring_.size()]=value;++size_;
    }
    std::optional<SynchronizedConsumer> pop() {
        std::lock_guard<std::mutex> lock(mutex_);if(!size_)return {};
        auto value=ring_[read_];read_=(read_+1)%ring_.size();--size_;return value;
    }
};
struct DiagnosticNumber {double value;};
inline std::ostream& operator<<(std::ostream& out,DiagnosticNumber number) {
    if(std::isfinite(number.value))out<<number.value;else out<<"null";return out;
}
inline void diagnostic_vec(std::ostream& out,Vec3 p){out<<'['<<DiagnosticNumber{p.x}<<','<<DiagnosticNumber{p.y}<<','<<DiagnosticNumber{p.z}<<']';}
inline void diagnostic_pose(std::ostream& out,const TrackedPose& p){out<<"{\"position\":";diagnostic_vec(out,p.position);
    out<<",\"orientation\":["<<DiagnosticNumber{p.orientation.x}<<','<<DiagnosticNumber{p.orientation.y}<<','<<DiagnosticNumber{p.orientation.z}<<','<<DiagnosticNumber{p.orientation.w}<<"]}";}
inline void diagnostic_xf(std::ostream& out,const body::Xf& p){diagnostic_pose(out,{p.p,p.q});}
inline void write_synchronized(std::ostream& out,const SynchronizedConsumer& v,std::uint32_t pid,std::uint64_t dropped) {
    const auto precision=out.precision();out<<std::setprecision(std::numeric_limits<double>::max_digits10);
    out<<"{\"schema_version\":1,\"source\":\"owned_native_palette\",\"pid\":"<<pid
       <<",\"tick\":"<<v.tick<<",\"native_frame\":"<<v.raw.native_frame<<",\"callback_sequence\":"<<v.callback_sequence
       <<",\"source_kind\":"<<v.source_kind<<",\"tracking_revision\":"<<v.raw.revision<<",\"pose_epoch\":"<<v.raw.pose_epoch
       <<",\"tracking_present\":"<<v.raw.present<<",\"consumer_present\":"<<v.consumer_present<<",\"consumer_native_frame\":"<<v.consumer_native_frame
       <<",\"source_tick\":"<<v.raw.source_tick<<",\"reference_epoch\":"<<v.raw.reference_epoch<<",\"reference_space\":\"LOCAL\",\"prediction_time\":"<<v.raw.prediction_time
       <<",\"publication_consistent\":"<<(v.consistent?"true":"false")<<",\"consumer_frame_verified\":false,\"dropped\":"<<dropped<<",\"raw_head\":";diagnostic_pose(out,v.raw.head);
    out<<",\"head_flags\":"<<v.raw.head_flags<<",\"origin\":";diagnostic_pose(out,v.raw.origin);
    out<<",\"controller_centre\":";diagnostic_vec(out,v.raw.controller_centre);
    out<<",\"camera_anchor\":[";for(unsigned i=0;i<3;++i){if(i)out<<',';out<<DiagnosticNumber{v.raw.camera.position[i]};}
    for(auto x:v.raw.camera.basis)out<<','<<DiagnosticNumber{x};out<<"],\"hands\":[";
    for(unsigned i=0;i<2;++i){if(i)out<<',';out<<"{\"grip\":";diagnostic_pose(out,v.raw.grip[i]);out<<",\"aim\":";diagnostic_pose(out,v.raw.aim[i]);
        out<<",\"grip_flags\":"<<v.raw.grip_flags[i]<<",\"aim_flags\":"<<v.raw.aim_flags[i]<<",\"grip_value\":"<<DiagnosticNumber{v.raw.grip_value[i]}
           <<",\"trigger_value\":"<<DiagnosticNumber{v.raw.trigger_value[i]}<<",\"world_grip\":";diagnostic_xf(out,v.solver.tracking.grip[i]);
        out<<",\"target_attachment\":";diagnostic_xf(out,v.solver.target_attachment[i]);out<<",\"grip_to_attachment\":";diagnostic_xf(out,v.solver.grip_to_attachment[i]);
        out<<",\"solver_palm\":";diagnostic_xf(out,v.solver.palm[i]);out<<",\"solver_wrist\":";diagnostic_xf(out,v.solver.wrist[i]);
        out<<",\"palm_residual_metres\":";if(v.consistent && v.solver.arm_valid[i])out<<DiagnosticNumber{v.solver.palm_residual[i]};else out<<"null";
        out<<",\"arm_gate\":"<<static_cast<unsigned>(v.solver.arm_gate[i])<<",\"owned\":"<<DiagnosticNumber{v.solver.owned[i+2]}<<'}';}
    out<<"],\"model\":"<<v.model<<",\"animation\":"<<v.animation<<",\"skeleton\":"<<v.skeleton<<",\"player\":"<<v.player
       <<",\"owner_generation\":"<<v.owner_generation<<",\"owner_flags\":"<<v.owner_flags<<",\"source_address\":"<<v.source
       <<",\"gate\":"<<v.gate<<",\"replaced\":"<<(v.replaced?"true":"false")<<",\"palette_valid\":"<<(v.palette_valid?"true":"false")
       <<",\"model_frame\":[";for(unsigned i=0;i<15;++i){if(i)out<<',';out<<DiagnosticNumber{v.model_frame[i]};}
    out<<"],\"final_palette\":[";if(v.palette_valid && v.count<=v.palette.size())for(unsigned i=0;i<v.count;++i){if(i)out<<',';out<<'[';for(unsigned j=0;j<16;++j){if(j)out<<',';out<<DiagnosticNumber{v.palette[i].f[j]};}out<<']';}
    out<<"],\"consumer_landmarks\":{\"valid\":"<<(v.landmarks_valid?"true":"false")<<",\"palm\":";
    if(v.landmarks_valid && v.source_kind)diagnostic_xf(out,v.actual_palm);else out<<"null";
    out<<",\"wrist\":";if(v.landmarks_valid && v.source_kind)diagnostic_xf(out,v.actual_wrist);else out<<"null";
    out<<",\"palm_residual_metres\":";
    if(v.landmarks_valid && v.source_kind>=1 && v.source_kind<=2 && v.consistent && v.solver.tracking.grip_valid[v.source_kind-1])out<<DiagnosticNumber{length(v.actual_palm.p-v.solver.tracking.grip[v.source_kind-1].p)};else out<<"null";
    out<<",\"pelvis\":";if(v.landmarks_valid && !v.source_kind)diagnostic_xf(out,v.actual_pelvis);else out<<"null";
    out<<",\"feet\":[";if(v.landmarks_valid && !v.source_kind){diagnostic_xf(out,v.actual_feet[0]);out<<',';diagnostic_xf(out,v.actual_feet[1]);}out<<"]}";
    out<<",\"gait\":{\"wall_dt\":"<<DiagnosticNumber{v.solver.wall_dt}<<",\"consumed_dt\":"<<DiagnosticNumber{v.solver.consumed_dt}<<",\"substep_dt\":"<<DiagnosticNumber{v.solver.substep_dt}
       <<",\"substeps\":"<<v.solver.substeps<<",\"gap_reset\":"<<(v.solver.gap_reset?"true":"false")<<",\"output_age_seconds\":"<<DiagnosticNumber{v.solver.output_age_seconds}
       <<",\"revision\":"<<v.solver.revision<<",\"solved_revision\":"<<v.solver.solved_revision<<",\"pelvis\":";diagnostic_xf(out,v.solver.pelvis);
    out<<",\"feet\":[";for(unsigned i=0;i<2;++i){if(i)out<<',';out<<"{\"final\":";diagnostic_xf(out,v.solver.feet[i]);out<<",\"plant\":";diagnostic_vec(out,v.solver.plant[i]);
        out<<",\"swing_from\":";diagnostic_vec(out,v.solver.swing_from[i]);out<<",\"swing_to\":";diagnostic_vec(out,v.solver.swing_to[i]);out<<",\"phase\":"<<DiagnosticNumber{v.solver.phase[i]}<<'}';}
    out<<"]},\"fit\":{\"revision\":"<<v.solver.fit_revision<<",\"scale\":"<<DiagnosticNumber{v.solver.scale}<<",\"arm_scale\":"<<DiagnosticNumber{v.solver.arm_scale}<<",\"floor_datum\":null,\"standing_eye_height\":";
    if(v.solver.fit && v.solver.fit->standing_eye_height_metres)out<<DiagnosticNumber{*v.solver.fit->standing_eye_height_metres};else out<<"null";
    out<<",\"eye_to_shoulder\":";if(v.solver.fit)out<<DiagnosticNumber{v.solver.fit->eye_to_shoulder_metres};else out<<"null";
    out<<",\"palm_span\":";if(v.solver.fit)out<<DiagnosticNumber{v.solver.fit->palm_span_metres};else out<<"null";
    out<<",\"shoulder_span\":";if(v.solver.fit)out<<DiagnosticNumber{v.solver.fit->shoulder_span_metres};else out<<"null";
    out<<",\"accepted_arm_scale\":";if(v.solver.fit)out<<DiagnosticNumber{v.solver.fit->arm_scale};else out<<"null";
    out<<",\"arm_scale_side\":["<<DiagnosticNumber{v.solver.arm_scale_side[0]}<<','<<DiagnosticNumber{v.solver.arm_scale_side[1]}<<']'
       <<",\"torso_scale\":"<<DiagnosticNumber{v.solver.torso_scale}<<",\"torso_width\":";
    if(v.solver.fit && v.solver.fit->torso_width_metres)out<<DiagnosticNumber{*v.solver.fit->torso_width_metres};else out<<"null";
    out<<",\"arm_reach\":[";for(unsigned i=0;i<2;++i){if(i)out<<',';if(v.solver.fit && v.solver.fit->arm_reach_metres[i])out<<DiagnosticNumber{*v.solver.fit->arm_reach_metres[i]};else out<<"null";}out<<']';
    out<<",\"reference_epoch\":";if(v.solver.fit)out<<v.solver.fit->reference_epoch;else out<<"null";
    out<<",\"shoulder_span_measured\":"<<(v.solver.fit && v.solver.fit->shoulder_span_measured?"true":"false")
       <<",\"arm_lengths_measured\":"<<(v.solver.fit && v.solver.fit->arm_lengths_measured?"true":"false")
       <<",\"torso_width_measured\":"<<(v.solver.fit && v.solver.fit->torso_width_measured?"true":"false");
    out<<"},\"cost_ms\":{\"preparation\":"<<DiagnosticNumber{v.preparation_ms}<<",\"native_consumer\":"<<DiagnosticNumber{v.consumer_ms}<<",\"material_validation\":"<<DiagnosticNumber{v.material_ms}<<",\"native_refresh\":"<<DiagnosticNumber{v.refresh_ms}
       <<"},\"material\":{\"reason\":"<<v.material_reason<<",\"refreshes\":"<<v.refreshes<<",\"deferred\":"<<v.refresh_deferred<<"},\"query_outcomes\":[";
    for(unsigned i=0;i<5;++i){if(i)out<<',';out<<v.query_outcomes[i];}
    out<<"],\"interaction\":{\"component\":"<<v.selected_component<<",\"epoch\":"<<v.selection_epoch<<",\"revision\":"<<v.selection_revision<<",\"tick\":"<<v.selection_tick<<",\"qualified\":"<<(v.selection_qualified?"true":"false")<<",\"grab\":[";
    for(unsigned i=0;i<2;++i){if(i)out<<',';out<<"{\"focus_component\":"<<v.focus_component[i]<<",\"held_component\":"<<v.held_component[i]<<",\"publication_tick\":"<<v.grab_tick[i]<<",\"epoch\":"<<v.grab_epoch[i]<<",\"acquisition_tick\":"<<v.acquisition_tick[i]<<",\"focus_calls\":"<<v.focus_calls[i]<<",\"focus_us\":"<<v.focus_us[i]<<",\"discovery_slices\":"<<v.discovery_slices[i]
        <<",\"decision_tick\":"<<v.grab_decision_tick[i]<<",\"source_tick\":"<<v.grab_source_tick[i]<<",\"source_age_ms\":";
        if(v.grab_age_valid[i])out<<v.grab_source_age_ms[i];else out<<"null";
        out<<",\"gate\":"<<v.grab_gate[i]<<",\"role\":"<<v.grab_role[i]<<",\"raw_grip\":"<<DiagnosticNumber{v.grab_raw[i]}<<",\"candidates\":"<<v.grab_candidates[i]
           <<",\"released_armed\":"<<(v.grab_armed[i]?"true":"false")<<",\"deferred\":"<<(v.grab_deferred[i]?"true":"false")
           <<",\"occupied\":"<<(v.grab_occupied[i]?"true":"false")<<",\"usable\":"<<(v.grab_usable[i]?"true":"false")<<'}';}
    out<<"]},\"culling_observed\":false,\"lens_visibility_observed\":false}\n";out.precision(precision);
}
} // namespace d2vr
