#pragma once
#include "full_body_driver.hpp"
#include "game_camera.hpp"
#include "hand_contact.hpp"
#include "anatomical_raw_pair.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <limits>

namespace d2vr {
struct SynchronizedTracking {
    std::uint64_t tick{},revision{},native_frame{},present{},pose_epoch{},reference_epoch{},head_flags{},source_tick{},mapping_revision{};
    std::int64_t prediction_time{};
    TrackedPose head{},grip[2]{},aim[2]{},origin{};
    std::uint64_t grip_flags[2]{},aim_flags[2]{};
    double grip_value[2]{},trigger_value[2]{};
    GameCameraPose camera{};
    Vec3 controller_centre{};
};
struct SynchronizedPalettePose {
    std::uint64_t tick{},source_tick{},tracking_revision{},pose_epoch{},reference_epoch{},mapping_revision{},fit_revision{},native_frame{};
    unsigned tracked_hands{};
    double native_ownership{};
    bool valid{};
};
struct SynchronizedConsumer {
    SynchronizedTracking raw{};
    fullbody::Diagnostic solver{};
    std::array<rig::Transform,69> palette{};
    std::array<float,15> model_frame{};
    std::uintptr_t model{},source{},player{},animation{},skeleton{};
    std::uint32_t owner_generation{},owner_flags{},count{},source_kind{},gate{},material_reason{};
    std::uint64_t tick{},callback_sequence{},consumer_native_frame{},consumer_present{},refreshes{},refresh_deferred{},query_outcomes[5]{},observer_epoch{};
    double preparation_ms{},consumer_ms{},material_ms{},refresh_ms{},post_validation_ms{};
    bool replaced{},palette_valid{},consistent{};
    body::Xf actual_palm{},actual_wrist{},actual_pelvis{},actual_feet[2]{};
    bool landmarks_valid{};
    HandProxies hand_proxies{};
    SynchronizedPalettePose palette_pose{};
    bool hand_proxies_valid{},consumer_returned{},arm_owner_rechecked{},binding_snapshot_current{},model_frame_current{};
    HandProxies raw_hand_proxies{};
    body::Xf raw_hand_grip{};
    bool raw_hand_proxies_valid{},raw_hand_reached{};
    AnatomicalRawPair::Diagnostic anatomical_shadow{};
    std::uintptr_t selected_component{},focus_component[2]{},held_component[2]{};
    std::uint64_t selection_epoch{},selection_revision{},selection_tick{},grab_tick[2]{},grab_epoch[2]{},acquisition_tick[2]{},focus_calls[2]{},focus_us[2]{},discovery_slices[2]{};
    bool selection_qualified{};
    std::uint64_t grab_decision_tick[2]{},grab_source_tick[2]{},grab_source_age_ms[2]{};
    unsigned grab_gate[2]{},grab_role[2]{},grab_candidates[2]{};
    double grab_raw[2]{};
    bool grab_age_valid[2]{},grab_armed[2]{},grab_deferred[2]{},grab_occupied[2]{},grab_usable[2]{};
    std::uint64_t socket_token{},socket_pose_epoch{},socket_reference_epoch{},socket_mapping_revision{},socket_tick{},socket_source_tick{},socket_consumer_tick{};
    unsigned socket_stage{},socket_joint{};
    bool socket_hand_empty{},socket_tracked_submitted{},socket_native_joint_valid{};
    body::Xf actual_attachment{},socket_native_joint{};
    std::uint64_t socket_tracking_revision{};
    body::Xf socket_grip{};
    double socket_native_ownership{};
};
inline bool synchronized_palette_tracking_matches(const SynchronizedConsumer& value) noexcept {
    const auto& pose=value.palette_pose;const auto& raw=value.raw;
    return pose.valid && pose.tracking_revision && pose.tracking_revision==raw.revision && pose.tick==raw.tick && pose.source_tick==raw.source_tick &&
        pose.pose_epoch==raw.pose_epoch && pose.reference_epoch==raw.reference_epoch && pose.mapping_revision==raw.mapping_revision && pose.native_frame==raw.native_frame;
}
// Opt-in observer: fixed storage, try-lock publication, at most 10 samples/s
// for each owned consumer. Only the probe worker serializes or writes files.
class SynchronizedObserver {
    mutable std::mutex mutex_;
    std::array<SynchronizedConsumer,128> ring_{};
    SynchronizedTracking tracking_{};
    std::array<std::uint64_t,5> last_{};
    std::size_t read_{},size_{};
    std::atomic<bool> enabled_{};
    std::atomic<bool> compact_only_{};
    std::atomic<std::uint64_t> dropped_{},serial_{};
    std::uint64_t epoch_{};
public:
    void enable(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);tracking_={};last_={};read_=size_=0;
        dropped_=serial_=0;compact_only_=false;
        if(epoch_==UINT64_MAX) {enabled_=false;return;}
        ++epoch_;enabled_=enabled;
    }
    bool enabled() const noexcept {return enabled_.load(std::memory_order_relaxed);}
    void stop() noexcept {enabled_=false;}
    void continue_compact() noexcept {compact_only_=true;}
    std::uint64_t dropped() const noexcept {return dropped_.load();}
    void tracking(const SynchronizedTracking& value) {
        if(!enabled())return;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++dropped_;return;}tracking_=value;
    }
    std::optional<SynchronizedConsumer> begin(unsigned kind,std::uint64_t tick) {
        if(!enabled() || kind>=last_.size())return {};
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++dropped_;return {};}
        if(!enabled())return {};
        if(last_[kind] && tick>=last_[kind] && tick-last_[kind]<100)return {};
        last_[kind]=tick;SynchronizedConsumer value{};value.raw=tracking_;value.tick=tick;
        value.source_kind=kind;value.callback_sequence=++serial_;value.observer_epoch=epoch_;return value;
    }
    std::optional<SynchronizedConsumer> begin_palette(unsigned kind,std::uint64_t tick) {
        return kind<3 && (!compact_only_.load(std::memory_order_relaxed) || kind==1)?begin(kind,tick):std::nullopt;
    }
    void publish(const SynchronizedConsumer& value) {
        if(!enabled())return;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++dropped_;return;}
        if(!enabled() || value.observer_epoch!=epoch_)return;
        if(size_==ring_.size()){++dropped_;return;}
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
    if(v.source_kind==3 || v.source_kind==4) {
        out<<std::setprecision(9);
        out<<"{\"schema_version\":1,\"source\":\"attachment_socket_reader\",\"observation_only\":true,\"pid\":"<<pid
           <<",\"tick\":"<<v.tick<<",\"source_kind\":"<<v.source_kind<<",\"token\":"<<v.socket_token
           <<",\"pose_epoch\":"<<v.socket_pose_epoch<<",\"reference_epoch\":"<<v.socket_reference_epoch
           <<",\"mapping_revision\":"<<v.socket_mapping_revision<<",\"tracking_revision\":"<<v.palette_pose.tracking_revision
           <<",\"source_tick\":"<<v.socket_source_tick<<",\"publication_tick\":"<<v.socket_tick
           <<",\"animation\":"<<v.animation<<",\"joint\":"<<v.socket_joint<<",\"stage\":"<<v.socket_stage
           <<",\"substituted\":"<<(v.replaced?"true":"false")<<",\"consumer_returned\":"<<(v.consumer_returned?"true":"false")
           <<",\"model_local_joint\":";
        if(v.palette_valid){out<<'[';for(unsigned index=0;index<16;++index){if(index)out<<',';out<<DiagnosticNumber{v.palette[0].f[index]};}out<<']';}else out<<"null";
        out<<",\"dropped\":"<<dropped<<",\"consumer_frame_verified\":false}\n";out.precision(precision);return;
    }
    out<<"{\"schema_version\":1,\"source\":\"owned_native_palette\",\"pid\":"<<pid
       <<",\"tick\":"<<v.tick<<",\"native_frame\":"<<v.raw.native_frame<<",\"callback_sequence\":"<<v.callback_sequence<<",\"observer_epoch\":"<<v.observer_epoch
       <<",\"source_kind\":"<<v.source_kind<<",\"tracking_revision\":"<<v.raw.revision<<",\"pose_epoch\":"<<v.raw.pose_epoch
       <<",\"tracking_present\":"<<v.raw.present<<",\"consumer_present\":"<<v.consumer_present<<",\"consumer_native_frame\":"<<v.consumer_native_frame
       <<",\"source_tick\":"<<v.raw.source_tick<<",\"reference_epoch\":"<<v.raw.reference_epoch<<",\"mapping_revision\":"<<v.raw.mapping_revision<<",\"reference_space\":\"LOCAL\",\"prediction_time\":"<<v.raw.prediction_time
       <<",\"publication_consistent\":"<<(v.consistent?"true":"false")<<",\"consumer_frame_verified\":false,\"dropped\":"<<dropped<<",\"raw_head\":";diagnostic_pose(out,v.raw.head);
    out<<",\"socket_terminal\":{\"observation_only\":true,\"token\":"<<v.socket_token<<",\"stage\":"<<v.socket_stage
       <<",\"pose_epoch\":"<<v.socket_pose_epoch<<",\"reference_epoch\":"<<v.socket_reference_epoch<<",\"mapping_revision\":"<<v.socket_mapping_revision
       <<",\"publication_tick\":"<<v.socket_tick<<",\"source_tick\":"<<v.socket_source_tick<<",\"consumer_tick\":"<<v.socket_consumer_tick
       <<",\"hand_empty\":"<<(v.socket_hand_empty?"true":"false")<<",\"tracked_submitted\":"<<(v.socket_tracked_submitted?"true":"false")
       <<",\"attachment_world\":";
    if(v.landmarks_valid && v.source_kind>=1 && v.source_kind<=2)diagnostic_xf(out,v.actual_attachment);else out<<"null";
    out<<",\"native_attachment_world\":";if(v.socket_native_joint_valid)diagnostic_xf(out,v.socket_native_joint);else out<<"null";out<<'}';
    out<<",\"head_flags\":"<<v.raw.head_flags<<",\"origin\":";diagnostic_pose(out,v.raw.origin);
    out<<",\"controller_centre\":";diagnostic_vec(out,v.raw.controller_centre);
    out<<",\"camera_anchor\":[";for(unsigned i=0;i<3;++i){if(i)out<<',';out<<DiagnosticNumber{v.raw.camera.position[i]};}
    for(auto x:v.raw.camera.basis)out<<','<<DiagnosticNumber{x};out<<"],\"hands\":[";
    for(unsigned i=0;i<2;++i){if(i)out<<',';out<<"{\"grip\":";diagnostic_pose(out,v.raw.grip[i]);out<<",\"aim\":";diagnostic_pose(out,v.raw.aim[i]);
        out<<",\"grip_flags\":"<<v.raw.grip_flags[i]<<",\"aim_flags\":"<<v.raw.aim_flags[i]<<",\"grip_value\":"<<DiagnosticNumber{v.raw.grip_value[i]}
           <<",\"trigger_value\":"<<DiagnosticNumber{v.raw.trigger_value[i]}<<",\"world_grip\":";diagnostic_xf(out,v.solver.tracking.grip[i]);
        out<<",\"target_attachment\":";diagnostic_xf(out,v.solver.target_attachment[i]);out<<",\"grip_to_attachment\":";diagnostic_xf(out,v.solver.grip_to_attachment[i]);
        // Only once this hand's arm was exported for the current publication: otherwise palm/wrist are the previous solve's and pair
        // with a newer world_grip (v558 run: 10-50 cm "jumps" that were one or two frames of locomotion, not the drawn hand).
        out<<",\"solver_palm\":";if(v.solver.arm_valid[i])diagnostic_xf(out,v.solver.palm[i]);else out<<"null";
        out<<",\"solver_wrist\":";if(v.solver.arm_valid[i])diagnostic_xf(out,v.solver.wrist[i]);else out<<"null";
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
    if(v.landmarks_valid && v.source_kind>=1 && v.source_kind<=2 && v.consistent && synchronized_palette_tracking_matches(v) &&
       v.palette_pose.tracking_revision==v.solver.tracking.tracking_revision && v.solver.tracking.grip_valid[v.source_kind-1])
        out<<DiagnosticNumber{length(v.actual_palm.p-v.solver.tracking.grip[v.source_kind-1].p)};else out<<"null";
    out<<",\"pelvis\":";if(v.landmarks_valid && !v.source_kind)diagnostic_xf(out,v.actual_pelvis);else out<<"null";
    out<<",\"feet\":[";if(v.landmarks_valid && !v.source_kind){diagnostic_xf(out,v.actual_feet[0]);out<<',';diagnostic_xf(out,v.actual_feet[1]);}out<<"]}";
    const bool geometry_valid=v.hand_proxies_valid && v.palette_valid && v.source_kind>=1 && v.source_kind<=2 && v.count==69 &&
        std::all_of(v.hand_proxies.begin(),v.hand_proxies.end(),[](const ContactSphere& sphere) {
            return finite(sphere.centre) && std::isfinite(sphere.radius) && sphere.radius>.0005 && sphere.radius<=.15;
        });
    out<<",\"hand_geometry\":{\"observation_only\":true,\"raw_intent\":false,\"source\":\"submitted_palette_after_legacy_collision\",\"valid\":"
       <<(geometry_valid?"true":"false")<<",\"consumer_returned\":"<<(v.consumer_returned?"true":"false")
       <<",\"arm_owner_rechecked\":"<<(v.arm_owner_rechecked?"true":"false")<<",\"binding_snapshot_current\":"<<(v.binding_snapshot_current?"true":"false")
       <<",\"model_frame_current\":"<<(v.model_frame_current?"true":"false")<<",\"proxies\":[";
    if(geometry_valid)for(unsigned index=0;index<hand_proxy_count;++index) {
        if(index)out<<',';const auto& sphere=v.hand_proxies[index];out<<'['<<DiagnosticNumber{sphere.centre.x}<<','<<DiagnosticNumber{sphere.centre.y}
            <<','<<DiagnosticNumber{sphere.centre.z}<<','<<DiagnosticNumber{sphere.radius}<<']';
    }
    const auto& pose=v.palette_pose;
    out<<"],\"palette_pose\":{\"valid\":"<<(pose.valid?"true":"false")<<",\"tick\":"<<pose.tick<<",\"source_tick\":"<<pose.source_tick
       <<",\"tracking_revision\":"<<pose.tracking_revision<<",\"pose_epoch\":"<<pose.pose_epoch<<",\"reference_epoch\":"<<pose.reference_epoch
       <<",\"mapping_revision\":"<<pose.mapping_revision<<",\"fit_revision\":"<<pose.fit_revision<<",\"native_frame\":"<<pose.native_frame
       <<",\"tracked_hands\":"<<pose.tracked_hands<<",\"native_ownership\":"<<DiagnosticNumber{pose.native_ownership}<<"}}";
    const bool raw_geometry_valid=geometry_valid && pose.valid && v.raw_hand_proxies_valid && body::valid_pose(v.raw_hand_grip) &&
        (pose.tracked_hands & (1u<<(v.source_kind-1)))!=0 &&
        std::all_of(v.raw_hand_proxies.begin(),v.raw_hand_proxies.end(),[](const ContactSphere& sphere) {
            return finite(sphere.centre) && std::isfinite(sphere.radius) && sphere.radius>.0005 && sphere.radius<=.15;
        });
    out<<",\"raw_hand_candidate\":{\"observation_only\":true,\"authorizes_response\":false,\"source\":\"forward_raw_grip_same_frozen_body\",\"valid\":"
       <<(raw_geometry_valid?"true":"false")<<",\"reached\":"<<(raw_geometry_valid && v.raw_hand_reached?"true":"false")
       <<",\"tracking_revision\":"<<pose.tracking_revision<<",\"source_tick\":"<<pose.source_tick<<",\"native_frame\":"<<pose.native_frame<<",\"grip\":";
    if(raw_geometry_valid)diagnostic_xf(out,v.raw_hand_grip);else out<<"null";
    out<<",\"proxies\":[";
    if(raw_geometry_valid)for(unsigned index=0;index<hand_proxy_count;++index) {
        if(index)out<<',';const auto& sphere=v.raw_hand_proxies[index];out<<'['<<DiagnosticNumber{sphere.centre.x}<<','<<DiagnosticNumber{sphere.centre.y}
            <<','<<DiagnosticNumber{sphere.centre.z}<<','<<DiagnosticNumber{sphere.radius}<<']';
    }
    out<<"]}";
    const auto& shadow=v.anatomical_shadow;
    out<<",\"anatomical_shadow\":{\"observation_only\":true,\"authorizes_response\":false,\"valid\":"<<(shadow.valid?"true":"false")
       <<",\"empty_verified_at_query\":"<<(shadow.empty_verified_at_query?"true":"false")
       <<",\"admission_epoch\":"<<shadow.admission_epoch<<",\"revision\":"<<shadow.revision<<",\"tracking_revision\":"<<shadow.tracking_revision
       <<",\"frame\":"<<shadow.frame<<",\"request_tick\":"<<shadow.request_tick<<",\"source_tick\":"<<shadow.source_tick<<",\"query_tick\":"<<shadow.query_tick
       <<",\"lifecycle_revision\":"<<shadow.lifecycle_revision<<",\"picked\":"<<shadow.picked<<",\"source_body\":"<<shadow.source_body<<",\"hands\":[";
    for(unsigned side=0;side<2;++side) {
        if(side)out<<',';out<<"{\"state\":"<<shadow.state[side]<<",\"casts\":"<<shadow.casts[side]<<",\"witnesses\":"<<shadow.witnesses[side]<<",\"shift\":";
        diagnostic_vec(out,shadow.shift[side]);out<<'}';
    }
    out<<"]}";
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
    out<<"},\"cost_ms\":{\"preparation\":"<<DiagnosticNumber{v.preparation_ms}<<",\"native_consumer\":"<<DiagnosticNumber{v.consumer_ms}<<",\"material_validation\":"<<DiagnosticNumber{v.material_ms}<<",\"native_refresh\":"<<DiagnosticNumber{v.refresh_ms}<<",\"post_validation\":"<<DiagnosticNumber{v.post_validation_ms}
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
inline void diagnostic_socket_pose(std::ostream& out,const body::Xf& value) {
    out<<'['<<DiagnosticNumber{value.p.x}<<','<<DiagnosticNumber{value.p.y}<<','<<DiagnosticNumber{value.p.z}
       <<','<<DiagnosticNumber{value.q.x}<<','<<DiagnosticNumber{value.q.y}<<','<<DiagnosticNumber{value.q.z}<<','<<DiagnosticNumber{value.q.w}<<']';
}
inline void write_socket_terminal(std::ostream& out,const SynchronizedConsumer& value,std::uint32_t pid,std::uint64_t dropped) {
    const auto precision=out.precision();out<<std::setprecision(9);
    out<<"{\"schema_version\":1,\"source\":\"arm_socket_terminal\",\"observation_only\":true,\"consumer_frame_verified\":false,\"pid\":"<<pid
       <<",\"source_kind\":1,\"tick\":"<<value.tick<<",\"consumer_tick\":"<<value.socket_consumer_tick
       <<",\"present\":"<<value.consumer_present<<",\"model\":"<<value.model<<",\"animation\":"<<value.animation
       <<",\"token\":"<<value.socket_token<<",\"stage\":"<<value.socket_stage<<",\"tracking_revision\":"<<value.socket_tracking_revision
       <<",\"pose_epoch\":"<<value.socket_pose_epoch<<",\"reference_epoch\":"<<value.socket_reference_epoch<<",\"mapping_revision\":"<<value.socket_mapping_revision
       <<",\"publication_tick\":"<<value.socket_tick<<",\"source_tick\":"<<value.socket_source_tick
       <<",\"hand_empty\":"<<(value.socket_hand_empty?"true":"false")<<",\"native_ownership\":"<<DiagnosticNumber{value.socket_native_ownership}
       <<",\"tracked_submitted\":"<<(value.socket_tracked_submitted?"true":"false")<<",\"consumer_returned\":"<<(value.consumer_returned?"true":"false")
       <<",\"landmarks_valid\":"<<(value.landmarks_valid?"true":"false")<<",\"pose_encoding\":\"xyz_xyzw\",\"frozen_grip\":";
    if(value.socket_tick)diagnostic_socket_pose(out,value.socket_grip);else out<<"null";
    for(unsigned index=0;index<3;++index) {
        const char* fields[]={",\"wrist\":",",\"palm\":",",\"attachment\":"};out<<fields[index];
        if(value.landmarks_valid)diagnostic_socket_pose(out,index==0?value.actual_wrist:index==1?value.actual_palm:value.actual_attachment);else out<<"null";
    }
    out<<",\"native_attachment\":";if(value.socket_native_joint_valid)diagnostic_socket_pose(out,value.socket_native_joint);else out<<"null";
    out<<",\"dropped\":"<<dropped<<"}\n";out.precision(precision);
}
struct SynchronizedLogBudget {
    std::uint64_t palette_limit{64ull*1024*1024},compact_limit{64ull*1024*1024},palette_bytes{},compact_bytes{},skipped{};
    bool compact_only{};
};
enum class SynchronizedWrite {written,skipped,exhausted,failed};
inline SynchronizedWrite write_synchronized_bounded(std::ostream& output,const SynchronizedConsumer& value,std::uint32_t pid,
        std::uint64_t dropped,SynchronizedLogBudget& budget) {
    const bool reader=value.source_kind==3 || value.source_kind==4;
    bool compact=reader || budget.compact_only;
    std::ostringstream line;
    if(!compact) {
        write_synchronized(line,value,pid,dropped);
        if(line.str().size()>budget.palette_limit-budget.palette_bytes) {budget.compact_only=true;compact=true;line.str({});line.clear();}
    }
    if(compact) {
        if(reader)write_synchronized(line,value,pid,dropped);
        else if(value.source_kind==1)write_socket_terminal(line,value,pid,dropped);
        else {++budget.skipped;return SynchronizedWrite::skipped;}
    }
    const auto bytes=line.str();
    if(compact && bytes.size()>budget.compact_limit-budget.compact_bytes)return SynchronizedWrite::exhausted;
    output<<bytes;if(!output)return SynchronizedWrite::failed;
    if(compact)budget.compact_bytes+=bytes.size();else budget.palette_bytes+=bytes.size();
    return SynchronizedWrite::written;
}
} // namespace d2vr
