#pragma once
// In-game full-body VR: adapts the engine's renderer uploads (0x25efe0 body/arm source
// transforms, native model frames) and the mod's tracked head/hands to body::BodySolver,
// and returns replacement model-space palettes. Pure math plus owned copies: it never
// reads or writes engine memory itself (callers pass native uploads and FSM state bits).
#include "body_ik.hpp"
#include "player_rig_data.hpp"
#include "rig_transform.hpp"
#include "native_power_hand.hpp"
#include "hand_bind.hpp"
#include "avatar_calibration.hpp"
#include "tracked_pose_lifetime.hpp"
#include "drawn_palm_residual.hpp"
#include "hand_reach.hpp"
#include "note_reading.hpp"
#include "weapon_grip_clips.hpp"
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <memory>

namespace d2vr::fullbody {
using body::Xf;
using rig::Transform;

inline Xf to_xf(const Transform& t) {
    return {body::from_axes(unit(rig::column(t,0)),unit(rig::column(t,1)),unit(rig::column(t,2))),rig::position(t)};
}
inline Transform to_transform(const Xf& x) {
    Transform t{};
    const Vec3 axes[3]={rotated(x.q,{1,0,0}),rotated(x.q,{0,1,0}),rotated(x.q,{0,0,1})};
    for(unsigned i=0;i<3;++i) {
        t.f[i*4]=static_cast<float>(axes[i].x);t.f[i*4+1]=static_cast<float>(axes[i].y);t.f[i*4+2]=static_cast<float>(axes[i].z);
    }
    t.f[12]=static_cast<float>(x.p.x);t.f[13]=static_cast<float>(x.p.y);t.f[14]=static_cast<float>(x.p.z);t.f[15]=1;
    return t;
}
// Affine palette widening keeps skinning vertices transverse to the torso's
// current heading; wrist/head targets remain rigid and outside this operation.
inline Transform widen_transverse(Transform t,Vec3 origin,Vec3 left,double scale) {
    left=unit(left);
    const auto widen=[&](Vec3 v) {return v+left*(dot(v,left)*(scale-1));};
    for(unsigned i=0;i<3;++i) {
        const auto v=widen(rig::column(t,i));
        t.f[i*4]=static_cast<float>(v.x);t.f[i*4+1]=static_cast<float>(v.y);t.f[i*4+2]=static_cast<float>(v.z);
    }
    const auto p=origin+widen(rig::position(t)-origin);
    t.f[12]=static_cast<float>(p.x);t.f[13]=static_cast<float>(p.y);t.f[14]=static_cast<float>(p.z);
    return t;
}
// Pose given as position + forward/left/up axes (the mod's WorldTrackedPose convention).
inline Xf from_basis(Vec3 position,Vec3 forward,Vec3 left,Vec3 up) {
    return {body::from_axes(unit(forward),unit(left),unit(up)),position};
}

// Native model frame (origin, orthonormal axes, near-uniform scale) as rigid transform + scale.
struct Frame {Xf rigid{};double scale=1;};
inline Frame frame_of(const std::array<float,15>& raw) {
    const rig::ModelFrame m(raw);
    if(std::abs(m.scale[0]-m.scale[1])>.001 || std::abs(m.scale[0]-m.scale[2])>.001)
        throw std::invalid_argument("Full-body requires a uniform native model scale");
    return {{body::from_axes(m.axes[0],m.axes[1],m.axes[2]),m.origin},(m.scale[0]+m.scale[1]+m.scale[2])/3};
}
inline Xf to_world(const Frame& f,const Xf& model) {
    return {body::mul(f.rigid.q,model.q),f.rigid.p+rotated(f.rigid.q,model.p*f.scale)};
}
inline Xf to_model(const Frame& f,const Xf& world) {
    const auto c=body::conj(f.rigid.q);
    return {body::mul(c,world.q),rotated(c,world.p-f.rigid.p)*(1/f.scale)};
}

inline body::Skeleton skeleton(const data::FixtureJoints& joints) {
    body::Skeleton s;
    for(const auto& j:joints) {
        s.names.push_back(j.name);s.parent.push_back(j.parent);
        s.local.push_back({normalized({j.q[0],j.q[1],j.q[2],j.q[3]}),{j.t[0],j.t[1],j.t[2]}});
    }
    s.finish();return s;
}

// Player state machine pool indices for the pinned executable (live RTTI census,
// tools/player_states.py, 2026-10-10). Only states that change body ownership are listed.
inline const char* state_name(unsigned index) {
    switch(index) {
        case 2:return "Jumping";case 3:return "Falling";case 4:return "CatFall";case 5:return "Slipping";
        case 7:return "Sliding";case 8:return "Mantle";case 9:return "Vault";case 13:return "Assassination";
        case 14:return "Stun";case 15:return "Dead";case 16:return "KnockedDown";case 17:return "Stumble";
        case 24:return "Peeking";case 31:return "Versus";case 32:return "VersusGrab";case 33:return "SyncAnim";
        case 38:return "NpcFatality";case 41:return "Choke";case 42:return "DropChoke";case 43:return "CureChoke";
        case 45:return "CarryCorpse";case 46:return "DropCorpse";case 55:return "BloodthirstyFatality";
        case 67:return "UseHeart";case 83:return "KillCam";case 84:return "GrabGrappledEntity";case 85:return "Crank";
        case 86:return "StoneShroud";case 87:return "EatRat";
        default:return nullptr;
    }
}
constexpr unsigned kStates=96;
// Combine every active state's request (largest per region). A native authored camera with no
// recognised state (cutscene, scripted sync) hands the whole body to the game.
inline body::Ownership ownership(const std::array<bool,kStates>& active,bool authored_camera) {
    body::Ownership own{};bool any=false;
    for(unsigned i=0;i<kStates;++i) if(active[i]) if(const auto* name=state_name(i)) {
        const auto o=body::native_ownership(name);
        if(o.legs+o.torso+o.arm[0]+o.arm[1]<=0) continue;
        if(!any) {own.in_seconds=o.in_seconds;own.out_seconds=o.out_seconds;}
        own.legs=std::max(own.legs,o.legs);own.torso=std::max(own.torso,o.torso);
        own.arm[0]=std::max(own.arm[0],o.arm[0]);own.arm[1]=std::max(own.arm[1],o.arm[1]);
        own.in_seconds=std::min(own.in_seconds,o.in_seconds);own.out_seconds=std::max(own.out_seconds,o.out_seconds);
        any=true;
    }
    if(!any && authored_camera) return {1,1,{1,1},.15,.35};
    return own;
}

// One frame of tracking from the render thread, in game-world units (metres).
struct Tracking {
    NoteViewMapping note{};
    Xf eye{};                 // centre-eye pose: +X forward, +Y left, +Z up
    Xf grip[2]{};             // controller grip poses (same convention)
    bool grip_valid[2]{};
    double grip_value[2]{},trigger[2]{};
    bool hand_empty[2]{};     // hand holds nothing: fingers curl from grip/trigger
    bool mirror_native[2]{};  // dual wield: this hand holds the other arm's item, posed by that arm's native clip mirrored onto it
    bool slot1_gun{};         // dual wield: slot 1 (the left arm) holds a native gun or crossbow: its calm clip is kept for gun copies (v771)
    unsigned slot1_type{};    // v1104: the slot-1 gun's component type (grip_clips::type_pistol/type_crossbow); 0 when unknown/other
    unsigned weapon_type[2]{}; // v1104: the type of the item each physical hand holds (grip_clips::Type); the kept clip is keyed by it
    // v1105: the hand state (hand_empty / slot tables / weapon_type) came from a fresh native snapshot this frame. Outside gameplay
    // (menus, loads) the snapshot is stale and every type reads 0; the driver keeps the last in-gameplay type/clip fields for the status
    // instead of overwriting them with that 0 (v1012 full_body.weapon_type/clip_source [0,0] was such a snapshot, taken with logic 12).
    bool hand_state_fresh{};
    // v1106: this frame's weapon_type came from a live source (the fresh hand snapshot OR the publisher's live item-record read).
    // weapon_type may be valid without the full hand state being fresh (a menu/load status still shows the equipped item).
    bool weapon_type_fresh{};
    bool gun_clip[2]{};       // dual wield: this hand holds a gun copy and no arm plays a gun clip: posed by the kept/sampled clip of ITS type (mirrored on the right)
    bool weapon_grip[2]{};    // v1102: a weapon hand with no authored weapon clip (gun_clip wanted, none kept): procedural grip, never the idle/empty native clip's straight fingers
    std::uint64_t native_socket_token[2]{};
    bool reverse_grip[2]{};   // held item: flip the socket 180 deg about the palm normal
    std::uint64_t item_key[2]{}; // which item each hand holds (body::Inputs::item_key)
    bool weapon_incoming[2]{};   // a draw/pickup the mod asked for is about to fill this hand (body::Inputs::weapon_incoming)
    float finger_turn[2][5][3]{};bool has_finger[2]{}; // per-joint finger turns (finger_ik.hpp), see body::Inputs
    float finger_axis[2][5][3][3]{};bool has_finger_axis[2]{}; // the hinges they turn about (finger_ik.hpp hinge_axes), see body::Inputs
    std::uint64_t tick{},native_frame{},tracking_revision{}; // tick: monotonic milliseconds
    std::uint64_t epoch{};    // reference/pose epoch: a change resets the solver
    std::uint64_t pose_epoch{},reference_epoch{},mapping_revision{};
    std::uint64_t source_tick{}; // actual XR sample; tick remains the world publication time
    double seconds{};          // solver clock (steady seconds); publish() stamps it when 0
    unsigned tracked_hands{};
    Xf raw_grip[2]{};
    unsigned raw_grip_hands{};
    std::array<float,15> anchor_root{};bool anchor_root_valid{}; // the body model frame (player+0xf0 -> +0x1860) read on the render thread beside the camera anchor
    // NativeTurning's head-follow total (rad) behind that anchor. The anchor heading already has it taken off (BodyAnchor), so the world the hands were
    // solved in does not turn with it: the root-motion carry must not turn the hands by it either (see Driver::world_from_solve).
    double head_follow{};
    // v830: the runtime sample this publication was mapped from, as mapped (raw world poses: no palm offset, no clamp), and the key of that map.
    // The arm uploads that solve this publication run before the next Setup: they move the eye and grips by the newest sample's motion under
    // the same map (Driver::note_xr), so an image's arms come from the locate of its own camera.
    Xf xr_head_world{},xr_grip_world[2]{};unsigned xr_hands{};std::uint64_t xr_map{},xr_serial{};
    double camera_heading{};bool camera_heading_valid{};   // v832: the native player heading (rad) at this Setup (Driver::root_now)
    // v861: the world yaw of the tracking space this publication was mapped with (the calibrated forward), and where it sits (the camera anchor):
    // a change between solves is a rig turn (stick, snap, fold), which turns the solver's memory with it (BodySolver::turn_rig).
    double rig_yaw{};Vec3 rig_pivot{};bool rig_valid{};
};

enum class ArmGate {ready,no_solution,stale_revision,stale_age};
struct ArmPose {
    std::array<Transform,69> joints{};
    Tracking tracking{};
    std::uint64_t revision{},fit_revision{};
    double owned{};
    std::optional<std::array<Transform,69>> raw_joints;
    bool raw_hand_reached{};
    Xf world_from_solve{};
    bool root_mapping_valid{};
};
struct Diagnostic {
    DrawnPalmSource palm_source[2]{};
    WorldTrackedPose current_root{};bool current_root_valid{};
    double root_gap_m{};      // horizontal distance at publish between the render-thread anchor root and the last arm-upload root (diagnostic only)
    bool root_gap_valid{};    // the gap above was measured: both the anchor root and an arm-upload root were known at publish
    std::uint64_t root_gap_publishes{};   // publishes with root_gap_m > 1e-6 (cumulative since the driver was built; clear() keeps it)
    double root_turn{},follow_delta{};    // rad about +Z, solve -> the last arm upload: how far the player root turned, and how much of that NativeTurning's head-follow did (the carry leaves that part out)
    Tracking tracking{};
    std::uint64_t revision{},solved_revision{},fit_revision{},callback_serial{},body_callback{},arm_callback[2]{};
    double wall_dt{},consumed_dt{},substep_dt{},output_age_seconds{};
    unsigned substeps{};bool gap_reset{},body_valid{},arm_valid[2]{};
    Frame body_frame{},arm_frame[2]{};
    Xf pelvis{},feet[2]{},palm[2]{},wrist[2]{},target_attachment[2]{},grip_to_attachment[2]{};
    Vec3 pelvis_forward{},pelvis_left{};
    Vec3 plant[2]{},swing_from[2]{},swing_to[2]{};double phase[2]{},owned[4]{},palm_residual[2]{};
    ArmGate arm_gate[2]{ArmGate::no_solution,ArmGate::no_solution};
    // v1104: per hand, the held item's component type and where its authored grip clip came from (0 none, 1 captured live this
    // session, 2 sampled: the first live frame or the baked engine idle clip for that type, 3 procedural fallback).
    int weapon_type[2]{};int clip_source[2]{};std::uint64_t clip_fallbacks[2]{};
    double scale=1,arm_scale=1,arm_scale_side[2]{1,1},torso_scale=1,configured_palm_span{};std::optional<AvatarCalibrationResult> fit;
    // Per side, the last visible arm solve's reach in the world (current root): reach_clamp a grip against it so the hand
    // clamp and the arm IK agree on a reachable hand (G2, C4). Invalid without a solve, a root mapping or a tracked hand.
    ArmReach reach[2]{};
};
class Driver {
public:
    struct ContactArm : ArmPose {bool reached{};};
    class PreparedContactArm {
        friend class Driver;
        std::shared_ptr<const body::BodySolver> model_;
        body::BodySolver::FrozenArm frozen_;
        ArmPose original_;
        std::array<float,15> frame_{};
        Frame conversion_{};
        Xf grip_to_attachment_{};
        Vec3 width_left_{};
        double width_factor_{1};
        std::array<unsigned,2> width_joints_{};
        unsigned side_{};
        static bool fresh(const Tracking& t,std::uint64_t now) {
            return t.tick && t.source_tick && now>=t.tick && now>=t.source_tick && now-t.tick<=32 && now-t.source_tick<=32;
        }
    public:
        const ArmPose& original() const noexcept {return original_;}
        const std::array<float,15>& model_frame() const noexcept {return frame_;}
        unsigned side() const noexcept {return side_;}
        std::optional<ContactArm> solve_world_grip(const Xf& target,std::uint64_t now) const {
            if(!model_ || !body::valid_pose(target) || !fresh(original_.tracking,now))return {};
            bool reached{};
            const auto attachment=body::mul(body::mul(body::inverse(original_.world_from_solve),target),grip_to_attachment_);
            const auto joints=model_->rearm(frozen_,attachment,reached);
            if(joints.size()!=69)return {};
            ContactArm result;static_cast<ArmPose&>(result)=original_;result.reached=reached;
            for(unsigned j=0;j<69;++j)result.joints[j]=to_transform(to_model(conversion_,joints[j]));
            if(width_factor_!=1)for(const auto j:width_joints_)
                result.joints[j]=widen_transverse(result.joints[j],rig::position(result.joints[j]),width_left_,width_factor_);
            return result;
        }
    };
    std::optional<PreparedContactArm> prepare_contact_arm(unsigned side,const ArmPose& actual,const std::array<float,15>& frame,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || !have_out_ || actual.revision!=solved_revision_ || actual.fit_revision!=fit_revision_ || actual.owned!=0 ||
           !actual.raw_joints || !actual.raw_hand_reached || !actual.root_mapping_valid || !PreparedContactArm::fresh(actual.tracking,now) ||
           !actual.tracking.hand_empty[side] || actual.tracking.native_socket_token[side] || actual.tracking.tracking_revision!=solved_track_.tracking_revision)return {};
        const auto frozen=solver_.freeze_arm(side,solved_inputs_,out_.arm_frame[side]);if(!frozen)return {};
        PreparedContactArm result;result.side_=side;result.original_=actual;result.frame_=frame;
        result.conversion_=frame_of(frame);
        result.conversion_.rigid=body::mul(body::inverse(actual.world_from_solve),result.conversion_.rigid);
        if(!contact_model_ || contact_model_fit_!=fit_revision_) {contact_model_=std::make_shared<const body::BodySolver>(solver_);contact_model_fit_=fit_revision_;}
        result.model_=contact_model_;result.frozen_=*frozen;result.grip_to_attachment_=grip_to_attachment_[side];
        const auto& b=solver_.skeleton(0);const auto chest=b.find("center_spine03_jnt");
        const auto heading=body::mul(out_.body[chest].q,body::conj(b.bind[chest].q));
        result.width_left_=rotated(body::conj(frame_of(frame).rigid.q),rotated(heading,{0,1,0}));
        result.width_factor_=1+(solver_.torso_scale()-1)*(1-out_.owned[1])*(1-out_.owned[2+side]);
        const auto& a=solver_.skeleton(1+side);
        result.width_joints_={static_cast<unsigned>(a.find("origin")),static_cast<unsigned>(a.find(side?"right_shoulder_jnt":"left_shoulder_jnt"))};
        return result;
    }
    bool contact_arm_current(const PreparedContactArm& prepared,const ArmPose& actual,const std::array<float,15>& frame,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& expected=prepared.original_;const auto& a=actual.tracking;const auto& b=expected.tracking;
        try {(void)frame_of(frame);}catch(...) {return false;}
        const auto side=prepared.side_;
        const auto world_raw=body::mul(actual.world_from_solve,a.raw_grip[side]);
        const auto original_raw=body::mul(expected.world_from_solve,b.raw_grip[side]);
        const auto alignment=std::abs(world_raw.q.x*original_raw.q.x+world_raw.q.y*original_raw.q.y+world_raw.q.z*original_raw.q.z+world_raw.q.w*original_raw.q.w);
        return prepared.model_==contact_model_ && have_out_ && have_track_ && actual.fit_revision==expected.fit_revision &&
            actual.fit_revision==fit_revision_ && actual.root_mapping_valid && actual.owned==0 && actual.tracking.hand_empty[side] &&
            PreparedContactArm::fresh(a,now) && PreparedContactArm::fresh(b,now) && a.tick>=b.tick && a.source_tick>=b.source_tick && a.epoch==b.epoch &&
            a.tracking_revision>=b.tracking_revision && a.pose_epoch==b.pose_epoch && a.reference_epoch==b.reference_epoch && a.mapping_revision==b.mapping_revision &&
            a.has_finger[side]==b.has_finger[side] && a.has_finger_axis[side]==b.has_finger_axis[side] &&
            a.grip_value[side]==b.grip_value[side] && a.trigger[side]==b.trigger[side] &&
            std::memcmp(a.finger_turn[side],b.finger_turn[side],sizeof(a.finger_turn[side]))==0 &&
            std::memcmp(a.finger_axis[side],b.finger_axis[side],sizeof(a.finger_axis[side]))==0 &&
            actual.raw_joints && actual.raw_hand_reached &&
            body::valid_pose(world_raw) && alignment>=std::cos(.15) && length(world_raw.p-original_raw.p)<=.06 &&
            track_.hand_empty[side] && !track_.native_socket_token[side] && own_.arm[side]==0 && track_.has_finger[side]==b.has_finger[side] && track_.has_finger_axis[side]==b.has_finger_axis[side] &&
            std::memcmp(track_.finger_turn[side],b.finger_turn[side],sizeof(b.finger_turn[side]))==0 &&
            std::memcmp(track_.finger_axis[side],b.finger_axis[side],sizeof(b.finger_axis[side]))==0 &&
            track_.pose_epoch==b.pose_epoch && track_.reference_epoch==b.reference_epoch && track_.mapping_revision==b.mapping_revision;
    }
    Driver():solver_(skeleton(data::kBody),skeleton(data::kLeftArm),skeleton(data::kRightArm),{1.7,0}) {
        // The game world is avatar-scaled: calibrate eye height to the avatar's own camera_jnt.
        const auto b=skeleton(data::kBody),l=skeleton(data::kLeftArm),r=skeleton(data::kRightArm);
        solver_=body::BodySolver(b,l,r,{b.bind[b.find("camera_jnt")].p.z,0});
        folded_={b.find("center_neck_jnt"),b.find("center_head_jnt"),b.find("center_head_end_jnt"),b.find("camera_jnt")};
        rebuild_hand_mapping();
    }
    // Fits are XR metres; solver/world tracking also use metres. No LOCAL floor
    // height is inferred when the wizard has no measured floor datum.
    void configure_calibration(const AvatarCalibrationResult& fit,std::optional<double> native_torso_width_metres={}) {
        if(!fit.reference_epoch || !std::isfinite(fit.arm_scale) || fit.arm_scale<.7 || fit.arm_scale>1.45 ||
           !std::isfinite(fit.palm_span_metres) || fit.palm_span_metres<.8 || fit.palm_span_metres>2.5 ||
           !std::isfinite(fit.shoulder_span_metres) || fit.shoulder_span_metres<.1 || fit.shoulder_span_metres>.95 ||
           !std::isfinite(fit.eye_to_shoulder_metres) || fit.eye_to_shoulder_metres<.05 || fit.eye_to_shoulder_metres>.4)
            throw std::invalid_argument("Invalid full-body fit");
        if(fit.torso_width_metres && (!native_torso_width_metres || !std::isfinite(*native_torso_width_metres) ||
           *native_torso_width_metres<.1 || *native_torso_width_metres>.8 || !std::isfinite(*fit.torso_width_metres)))
            throw std::invalid_argument("Torso fit requires a verified native chest surface width");
        for(const auto reach:fit.arm_reach_metres) if(reach && (!std::isfinite(*reach) || *reach<.25 || *reach>1.1))
            throw std::invalid_argument("Invalid independent arm reach");
        std::lock_guard<std::mutex> lock(mutex_);
        if(fit_ && fit_->reference_epoch==fit.reference_epoch && fit_->arm_scale==fit.arm_scale &&
           fit_->palm_span_metres==fit.palm_span_metres && fit_->standing_eye_height_metres==fit.standing_eye_height_metres &&
           fit_->shoulder_span_metres==fit.shoulder_span_metres && fit_->eye_to_shoulder_metres==fit.eye_to_shoulder_metres &&
           fit_->shoulder_span_measured==fit.shoulder_span_measured && fit_->arm_lengths_measured==fit.arm_lengths_measured &&
           fit_->torso_width_measured==fit.torso_width_measured && fit_->torso_width_metres==fit.torso_width_metres &&
           fit_->arm_reach_metres==fit.arm_reach_metres && native_torso_width_==native_torso_width_metres) return;
        const auto b=skeleton(data::kBody),l=skeleton(data::kLeftArm),r=skeleton(data::kRightArm);
        const double height=fit.standing_eye_height_metres.value_or(b.bind[b.find("camera_jnt")].p.z);
        const auto chest=b.bind[b.find("center_spine03_jnt")];
        const auto palm_bind=[&](const body::Skeleton& a,const std::string& prefix) {
            std::array<Transform,4> knuckles{};const char* fingers[]={"index","medium","ring","pinky"};
            for(unsigned i=0;i<4;++i) knuckles[i]=to_transform(a.bind[a.find(prefix+"_"+fingers[i]+"_02_jnt")]);
            return body::mul(chest,to_xf(rig::hand_bind_grip(to_transform(a.bind[a.find(prefix+"_wrist_jnt")]),knuckles))).p;
        };
        const auto palm_delta=palm_bind(l,"left")-palm_bind(r,"right");
        const auto attachment_delta=body::mul(chest,l.bind[l.find("left_hand_attachment_jnt")]).p-
                                    body::mul(chest,r.bind[r.find("right_hand_attachment_jnt")]).p;
        const double avatar_scale=std::clamp(height/b.bind[b.find("camera_jnt")].p.z,.6,1.4);
        // Limb fit does not resize the wrist/palm subtree: subtract its fixed
        // landmark offset after fitting palm span, rather than scaling it twice.
        const double attachment_span=length(unit(palm_delta)*fit.palm_span_metres-(palm_delta-attachment_delta)*avatar_scale);
        body::Calibration calibration{height,attachment_span,fit.shoulder_span_metres,fit.eye_to_shoulder_metres};
        // Height moves joint centres; native inverse-bind skin offsets retain their size.
        if(fit.torso_width_metres) calibration.torso_scale=*fit.torso_width_metres/ *native_torso_width_metres;
        for(unsigned side=0;side<2;++side) if(fit.arm_reach_metres[side]) {
            const auto& a=side?r:l;const std::string prefix=side?"right":"left";
            const auto shoulder=body::mul(chest,a.bind[a.find(prefix+"_arm_jnt")]).p;
            const auto attachment=body::mul(chest,a.bind[a.find(prefix+"_hand_attachment_jnt")]).p;
            const auto palm=palm_bind(a,prefix);
            calibration.arm_reach[side]=length(unit(palm-shoulder)*(*fit.arm_reach_metres[side])+(attachment-palm)*avatar_scale);
        }
        if(calibration.arm_reach[0]>0 && calibration.arm_reach[1]>0) calibration.hand_span=0;
        solver_=body::BodySolver(b,l,r,calibration);
        native_torso_width_=native_torso_width_metres;fit_=fit;have_out_=false;last_={};++fit_revision_;rebuild_hand_mapping();
    }
    std::uint64_t fit_epoch() const {std::lock_guard<std::mutex> lock(mutex_);return fit_?fit_->reference_epoch:0;}
    // Current player-root (body model) frame, read at an ARM upload: arms upload before the body each
    // frame, so without this they converted with last frame's root and trailed one frame while moving.
    // head_follow: NativeTurning's head-follow total (rad) as of this upload.
    void note_root(const std::array<float,15>& body_model_frame,double head_follow=0,std::optional<double> camera_heading={}) {
        try {const auto f=frame_of(body_model_frame);std::lock_guard<std::mutex> lock(mutex_);root_last_=f.rigid;follow_last_=head_follow;have_root_last_=true;
             have_heading_last_=camera_heading && std::isfinite(*camera_heading);if(have_heading_last_)heading_last_=*camera_heading;} catch(...) {}
    }
    // Render thread, once per frame.
    void publish(const Tracking& t,const body::Ownership& own) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(have_track_ && t.epoch!=track_.epoch) {solver_.reset();have_out_=false;last_={};}
        if(have_track_ && t.epoch==track_.epoch && t.seconds>track_.seconds && track_.seconds>0) {
            const double dt=t.seconds-track_.seconds;
            eye_speed_=std::hypot(t.eye.p.x-track_.eye.p.x,t.eye.p.y-track_.eye.p.y)/std::max(dt,1e-3);
        } else eye_speed_=0;
        // Player root at this publication: uploads later carry the solved pose by the root motion since.
        // Fix A: the root the solve's eye belongs to is the render thread's anchor root (what the camera was placed with), not the last arm upload's:
        // that one is a frame (or more) stale at speed, and the whole gap rode on the hands as a lag. build/legacy-arm-root-carry keeps the old start.
        diag_.root_gap_m=0;diag_.root_gap_valid=false;
        std::optional<Xf> anchor;
        if(t.anchor_root_valid) try {anchor=frame_of(t.anchor_root).rigid;} catch(...) {}
        if(anchor && have_root_last_) {diag_.root_gap_m=std::hypot(anchor->p.x-root_last_.p.x,anchor->p.y-root_last_.p.y);diag_.root_gap_valid=true;if(diag_.root_gap_m>1e-6)++root_gap_publishes_;}
        heading_at_publish_=t.camera_heading;have_heading_at_publish_=t.camera_heading_valid && std::isfinite(t.camera_heading);
        if(anchor && !legacy_root_carry_) {root_at_publish_=*anchor;have_root_at_publish_=true;follow_at_publish_=t.head_follow;}
        else {root_at_publish_=root_last_;have_root_at_publish_=have_root_last_;follow_at_publish_=follow_last_;}
        track_=t;own_=own;have_track_=true;++revision_;diag_.body_valid=false;diag_.arm_valid[0]=diag_.arm_valid[1]=false;published_=std::chrono::steady_clock::now();
        if(!(track_.seconds>0)) track_.seconds=std::chrono::duration<double>(published_.time_since_epoch()).count();
    }
    void legacy_root_carry(bool on) {std::lock_guard<std::mutex> lock(mutex_);legacy_root_carry_=on;}   // build/legacy-arm-root-carry
    // v830: the newest runtime sample (the head and grips located for the display of the image whose arms upload next), mapped to the world by
    // the last publication's map (map = that publication's xr_map). Render thread, at the Present that located it.
    struct XrSample {Xf head{},grip[2]{};unsigned hands{};std::uint64_t map{},serial{};};
    void note_xr(const XrSample& s) {std::lock_guard<std::mutex> lock(mutex_);xr_latest_=s;have_xr_latest_=true;}
    void legacy_root_yaw(bool on) {std::lock_guard<std::mutex> lock(mutex_);legacy_root_yaw_=on;}   // build/legacy-root-yaw-carry (v832)
    void legacy_hand_sample(bool on) {std::lock_guard<std::mutex> lock(mutex_);legacy_hand_sample_=on;}   // build/legacy-hand-sample: the publication's own sample, as before v830
    std::uint64_t sample_refreshes() const {std::lock_guard<std::mutex> lock(mutex_);return sample_refreshes_;}
    // v835: the engine places attached items (weapons, power effects) from the arm palette BEFORE that tick's arm upload (v834 sim: 5712 of 5712
    // reads saw the previous interval's palette), so an item showed the hand a tick late. The world motion the drawn hand makes from the last
    // publication's tick to now (root, heading, head-follow read now): the camera anchor's carry after the newest sample's hand motion. Applied
    // to the item's joint as drawn in the previous tick, it is what this tick's upload will draw. Nothing without a heading or a fresh map.
    std::optional<Xf> item_advance(unsigned side,const std::array<float,15>& root_frame,double follow_now,double heading_now) const {
        try {
            const auto r=frame_of(root_frame).rigid;
            std::lock_guard<std::mutex> lock(mutex_);
            if(side>1 || legacy_item_lag_ || !have_track_ || !track_.anchor_root_valid || !track_.camera_heading_valid || !std::isfinite(heading_now) || !std::isfinite(follow_now)) return {};
            const auto r0=frame_of(track_.anchor_root).rigid;
            if(length(r.p-r0.p)>1.) return {};   // a teleport or a load is not motion
            const auto q=body::rot_z(body::wrap_pi((heading_now-follow_now)-(track_.camera_heading-track_.head_follow)));
            Xf hand{};
            const auto& sample=xr_latest_;
            if(have_xr_latest_ && track_.xr_map && sample.map==track_.xr_map && sample.serial>track_.xr_serial && ((track_.xr_hands&sample.hands)>>side&1u)) {
                const auto& from=track_.xr_grip_world[side];const auto& to=sample.grip[side];const auto turn=body::mul(to.q,body::conj(from.q));
                if(length(to.p-from.p)<.5 && std::abs(turn.w)>std::cos(.5)) hand=body::mul(to,body::inverse(from));
            }
            return body::mul(Xf{q,r.p-rotated(q,r0.p)},hand);
        } catch(...) {return {};}
    }
    void legacy_item_lag(bool on) {std::lock_guard<std::mutex> lock(mutex_);legacy_item_lag_=on;}   // build/legacy-item-lag (v835)
    void legacy_body_turn(bool on) {std::lock_guard<std::mutex> lock(mutex_);legacy_body_turn_=on;}   // build/legacy-body-turn (v861): the solver's memory stays in the world on rig turns
    std::uint64_t rig_turns() const {std::lock_guard<std::mutex> lock(mutex_);return rig_turns_;}
    // v861 (paired, user: "the camera turns, then the body, then the hands are computed... it lags and looks non-native"): an image is placed from
    // its own publication, the camera anchor and runtime sample its Setup placed the camera with: no carry by the player root since, no newer
    // sample. Render thread, at the eye-0 begin of a render (it draws the newest Setup's views): solve that publication once (later uploads reuse
    // it) and keep the result for the render's skin copies.
    void begin_image(std::uint64_t frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        image_valid_=false;
        if(!have_track_ || !have_native_body_ || !solve_locked(0,false)) return;
        image_out_=out_;image_fit_=fit_revision_;image_frame_=frame;image_valid_=true;image_held_ok_=true;++image_begins_;
    }
    // Render jobs, at the GPU skin copy (0x25f930): the image's joints in the frame the model is drawn in (its live model+0x1860).
    // frame: the Present counter now; the image must have begun in this interval (a render without a begin keeps the tick's upload).
    // raw: the full-hand-contact "raw reach" pose (Output::raw_arm), which an empty hand's upload submits while its contact is prepared.
    // v1213 `held`: without this Present's image, the last image solved since the last clear() (same fit), re-expressed in the live frame: the
    // hands stay where they were last drawn instead of the engine's arm pose (dxgi_probe image_skin_plan write_held)
    std::optional<std::array<Transform,69>> image_arm(unsigned side,const std::array<float,15>& live,std::uint64_t frame,bool raw=false,bool held=false) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || image_fit_!=fit_revision_ || !(held?image_held_ok_:image_valid_ && image_frame_==frame)) return {};
        const auto& joints=raw?image_out_.raw_arm[side]:image_out_.arm[side];if(joints.size()!=69) return {};
        try {const auto f=frame_of(live);return export_arm(side,joints,f,f,image_out_);} catch(...) {return {};}
    }
    std::optional<std::array<Transform,20>> image_body(const std::array<float,15>& live,std::uint64_t frame) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!image_valid_ || image_frame_!=frame || image_fit_!=fit_revision_ || image_out_.body.size()!=20) return {};
        try {const auto f=frame_of(live);return export_body(image_out_,f,f);} catch(...) {return {};}
    }
    std::uint64_t image_begins() const {std::lock_guard<std::mutex> lock(mutex_);return image_begins_;}
    bool image_fresh(std::uint64_t frame) const {std::lock_guard<std::mutex> lock(mutex_);return image_valid_ && image_frame_==frame && image_fit_==fit_revision_;} // v1213
    // v861: the image's solved world joint (as its render draws it), for an item placed before the next image: Nothing without an image this interval.
    std::optional<Xf> image_world(unsigned side,unsigned joint,std::uint64_t frame) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || !image_valid_ || image_frame_!=frame || image_fit_!=fit_revision_ || joint>=image_out_.arm[side].size()) return {};
        return image_out_.arm[side][joint];
    }
    // v1104: the image's version of the controller grip (tracking frame, world) of one hand. A copy is placed from that grip (not from
    // joint 59: the authored socket rotates the hand attachment away from the controller), so the draw-time publisher must compare and
    // correct the copy's read in the same grip frame. Returns nothing without an image of this frame.
    std::optional<Xf> image_grip(unsigned side,std::uint64_t frame) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || !image_valid_ || image_frame_!=frame || image_fit_!=fit_revision_ || !solved_track_.grip_valid[side]) return {};
        return solved_track_.grip[side];
    }
    std::optional<Vec3> image_joint(unsigned side,unsigned joint) const {   // the image's solved world joint (diagnostics)
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || !image_valid_ || joint>=image_out_.arm[side].size()) return {};
        return image_out_.arm[side][joint].p;
    }
    Xf attachment_target(unsigned side,const Xf& grip) const {std::lock_guard<std::mutex> lock(mutex_);return hand_target(side&1u,grip);}   // the attachment joint a grip asks for (frame-order trace)
    void clear() {std::lock_guard<std::mutex> lock(mutex_);contact_model_.reset();image_valid_=false;image_held_ok_=false;have_track_=have_out_=have_native_body_=false;have_root_last_=have_root_at_publish_=have_root_solved_=false;have_xr_latest_=false;have_heading_last_=have_heading_at_publish_=have_heading_solved_=false;
        have_native_arm_[0]=have_native_arm_[1]=false;native_={};for(auto& c:gun_clip_) c.clear();for(auto& c:gun_clip_first_) c.clear();legacy_gun_clip_.clear();gun_clip_have_last_=false;gun_clip_calm_=0;gun_clip_type_=0;clip_fallbacks_={};last_={};solved_track_={};diag_={};solver_.reset();}
    void clear_calibration() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!fit_) return;
        const auto b=skeleton(data::kBody);
        solver_=body::BodySolver(b,skeleton(data::kLeftArm),skeleton(data::kRightArm),{b.bind[b.find("camera_jnt")].p.z,0});
        fit_.reset();native_torso_width_.reset();have_out_=false;last_={};++fit_revision_;rebuild_hand_mapping();
    }
    // back: metres the body sits behind the eye (horizontal), so looking down shows chest/legs
    // instead of the inside of the collar. head_scale: neck/head/camera joints are folded to this
    // scale at the neck, hiding the hood/collar skinned to them (the wearer never sees them).
    void tune(double back,double head_scale) {
        std::lock_guard<std::mutex> lock(mutex_);
        back_=std::clamp(back,0.,.3);head_scale_=std::clamp(head_scale,.01,1.);
    }
    // Animation/render workers: the engine's own body upload for this frame. Returns the
    // replacement model-space joints, or nothing (keep the native upload).
    std::optional<std::array<Transform,20>> body(const std::array<Transform,20>& native,const std::array<float,15>& frame,std::uint64_t frame_key=0) {
        std::lock_guard<std::mutex> lock(mutex_);
        diag_.body_callback=++diag_.callback_serial;diag_.body_valid=false;
        body_frame_=frame_of(frame);
        native_.model=body_frame_.rigid;native_.body.resize(20);
        for(unsigned j=0;j<20;++j) native_.body[j]=to_xf(native[j]);
        have_native_body_=true;
        if(!solve_locked(frame_key)) return {};
        const auto conv=anchored(2,body_frame_);
        const auto out=export_body(out_,conv,body_frame_);
        diag_.body_frame=body_frame_;diag_.body_valid=true;
        const auto& sk=solver_.skeleton(0);
        const auto pelvis_index=sk.find("center_root_jnt");
        diag_.pelvis=to_world(conv,to_xf(out[pelvis_index]));
        const auto pelvis_heading=body::mul(diag_.pelvis.q,body::conj(sk.bind[pelvis_index].q));
        diag_.pelvis_forward=rotated(pelvis_heading,{1,0,0});
        diag_.pelvis_left=rotated(pelvis_heading,{0,1,0});
        for(unsigned i=0;i<2;++i) diag_.feet[i]=to_world(conv,to_xf(out[sk.find(i?"right_ankle_jnt":"left_ankle_jnt")]));
        return out;
    }
    std::optional<std::array<Transform,69>> arm(unsigned side,const std::array<Transform,69>& native,const std::array<float,15>& frame) {
        const auto posed=arm_frozen(side,native,frame);
        return posed?std::optional<std::array<Transform,69>>(posed->joints):std::nullopt;
    }
    std::optional<ArmPose> arm_frozen(unsigned side,const std::array<Transform,69>& native,const std::array<float,15>& frame,
                                      std::uint64_t callback_tick=0,std::uint64_t frame_key=0) {
        if(side>1) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        const bool latched=frame_key && frame_key==latched_key_ && have_out_;
        if(callback_tick && !latched) {
            // A track newer than the callback (the head-view thread published after this callback read its clock) is not old:
            // only that pose moved on, and the solve below uses the newest one. Refusing it dropped both hands for that frame.
            const auto skew=[&](std::uint64_t stamp) {return callback_tick<stamp && stamp-callback_tick>tracked_pose_future_skew_ms;};
            const auto aged=[&](std::uint64_t stamp) {return callback_tick>=stamp && callback_tick-stamp>tracked_pose_freshness_ms;};
            unsigned why=0;
            if(!have_track_ || !track_.tick) why=1;
            else if(skew(track_.tick) || (track_.source_tick && skew(track_.source_tick))) why=2;
            else if(aged(track_.tick) || (track_.source_tick && aged(track_.source_tick))) why=3;
            if(why) {
                diag_.arm_callback[side]=++diag_.callback_serial;diag_.arm_valid[side]=false;diag_.arm_gate[side]=ArmGate::stale_age;
                arm_why_[side]=why;return {};
            }
        }
        auto& n=native_.arm[side];n.resize(69);
        for(unsigned j=0;j<69;++j) n[j]=to_xf(native[j]);
        have_native_arm_[side]=true;
        diag_.arm_callback[side]=++diag_.callback_serial;diag_.arm_valid[side]=false;
        // Native arms usually arrive first; solve this publication from the retained body pose.
        (void)solve_locked(frame_key);
        diag_.arm_gate[side]=!latched && have_track_ && std::chrono::duration<double>(std::chrono::steady_clock::now()-published_).count()>tracked_pose_freshness_seconds?ArmGate::stale_age:
            !have_out_?ArmGate::no_solution:(solved_revision_!=revision_ && !latched)?ArmGate::stale_revision:ArmGate::ready;
        if(diag_.arm_gate[side]!=ArmGate::ready || out_.arm[side].size()!=69) {
            arm_why_[side]=diag_.arm_gate[side]==ArmGate::stale_age?3u:diag_.arm_gate[side]==ArmGate::stale_revision?5u:4u;
            return {};
        }
        arm_why_[side]=0;
        // The engine places the arm model at its own (native) chest; express our world joints there.
        const auto arm_frame=frame_of(frame);
        const auto conv=anchored(side,arm_frame);
        const auto out=export_arm(side,out_.arm[side],conv,arm_frame);
        diag_.arm_frame[side]=arm_frame;diag_.arm_valid[side]=true;
        if(have_root_solved_ && have_root_last_) {diag_.root_turn=body::twist_angle(body::mul(root_last_.q,body::conj(root_solved_.q)),{0,0,1});diag_.follow_delta=body::wrap_pi(follow_last_-follow_solved_);}
        const auto& sk=solver_.skeleton(1+side);const std::string prefix=side?"right":"left";
        diag_.wrist[side]=to_world(conv,to_xf(out[sk.find(prefix+"_wrist_jnt")]));
        std::array<Transform,4> knuckles{};const char* fingers[]={"index","medium","ring","pinky"};
        for(unsigned j=0;j<4;++j) knuckles[j]=out[sk.find(prefix+"_"+fingers[j]+"_02_jnt")];
        diag_.palm[side]=to_world(conv,to_xf(rig::hand_bind_grip(out[sk.find(prefix+"_wrist_jnt")],knuckles)));
        diag_.palm_residual[side]=length(diag_.palm[side].p-solved_track_.grip[side].p);

        auto& palm_source=diag_.palm_source[side];palm_source={};
        if(have_root_solved_ && have_root_last_ && frame_key && solved_track_.grip_valid[side] &&
           (solved_track_.tracked_hands&(1u<<side)) && out_.owned[2+side]==0) {
            const auto world_from_solve=this->world_from_solve();
            const auto world_pose=[](const Xf& value) {return WorldTrackedPose{value.p,
                {rotated(value.q,{1,0,0}),rotated(value.q,{0,1,0}),rotated(value.q,{0,0,1})}};};
            palm_source.context={solved_track_.pose_epoch,solved_track_.reference_epoch,solved_track_.mapping_revision,
                fit_revision_,world_pose(root_last_),true};
            palm_source.grip=world_pose(body::mul(world_from_solve,solved_track_.grip[side]));
            palm_source.palm=world_pose(to_world(arm_frame,to_xf(rig::hand_bind_grip(out[sk.find(prefix+"_wrist_jnt")],knuckles))));
            palm_source.tick=solved_track_.tick;palm_source.source_tick=solved_track_.source_tick;
            palm_source.tracking_revision=solved_track_.tracking_revision;palm_source.frame=frame_key;
            palm_source.callback=diag_.arm_callback[side];palm_source.valid=true;
        }
        ArmPose posed{out,solved_track_,solved_revision_,fit_revision_,out_.owned[2+side],{},false};
        posed.root_mapping_valid=have_root_solved_ && have_root_last_;
        if(posed.root_mapping_valid)posed.world_from_solve=world_from_solve();
        if(out_.raw_arm[side].size()==69) {
            posed.raw_joints=export_arm(side,out_.raw_arm[side],conv,arm_frame);
            posed.raw_hand_reached=out_.raw_hand_reached[side];
        }
        return posed;
    }
    // Why the last arm_frozen(side) returned nothing (ArmFlicker::Why): 0 = it did not, 1 no track yet, 2 the callback read its clock
    // before a newer publication (skew past tolerance), 3 the track is older than the freshness window, 4 no solution, 5 stale revision.
    unsigned arm_why(unsigned side) const {std::lock_guard<std::mutex> lock(mutex_);return side<2?arm_why_[side]:0u;}
    Diagnostic diagnostic() const {
        std::lock_guard<std::mutex> lock(mutex_);auto d=diag_;d.root_gap_publishes=root_gap_publishes_;
        if(have_root_last_)d.current_root={root_last_.p,{rotated(root_last_.q,{1,0,0}),rotated(root_last_.q,{0,1,0}),rotated(root_last_.q,{0,0,1})}};
        d.current_root_valid=have_root_last_;
        d.tracking=track_;d.revision=revision_;d.solved_revision=solved_revision_;d.fit_revision=fit_revision_;d.fit=fit_;
        d.output_age_seconds=have_out_?std::chrono::duration<double>(std::chrono::steady_clock::now()-last_).count():0;
        d.scale=solver_.scale();d.arm_scale=solver_.arm_scale();
        for(unsigned i=0;i<2;++i)d.arm_scale_side[i]=solver_.arm_scale(i);d.torso_scale=solver_.torso_scale();
        d.configured_palm_span=configured_palm_span_;
        for(unsigned i=0;i<2;++i) {d.grip_to_attachment[i]=grip_to_attachment_[i];d.target_attachment[i]=hand_target(i,track_.grip[i]);}
        for(unsigned i=0;i<2;++i) d.reach[i]=world_reach(i);
        return d;
    }
    double owned(unsigned k) const {std::lock_guard<std::mutex> lock(mutex_);return k<4?out_.owned[k]:0;}
    std::uint64_t solves() const {std::lock_guard<std::mutex> lock(mutex_);return solves_;}
    std::uint64_t failures() const {std::lock_guard<std::mutex> lock(mutex_);return failures_;}
    // Per hand, how often each way of posing the fingers ran (body::Output::finger_mode: none, contact turns, grip alone, the game's), and how often
    // the solve was given contact turns / an empty hand: where a finger pose is lost shows here (status JSON full_body.finger_modes / finger_inputs).
    std::array<std::array<std::uint64_t,4>,2> finger_modes() const {std::lock_guard<std::mutex> lock(mutex_);return finger_modes_;}
    std::array<std::array<std::uint64_t,2>,2> finger_inputs() const {std::lock_guard<std::mutex> lock(mutex_);return finger_inputs_;}
    // v1102: the kept native gun clip exists (the left arm held a gun calmly for 0.25 s): a gun-copy hand can take its authored
    // fingers. Without it the hand must take the procedural grip (Tracking::weapon_grip), or it would follow an empty/idle clip.
    // v1104: per type; the baked engine idle clip counts as a sampled source too.
    bool gun_clip_ready() const {std::lock_guard<std::mutex> lock(mutex_);return clip_for_type_locked(track_.slot1_type)!=nullptr;}
    bool gun_clip_ready(unsigned type) const {std::lock_guard<std::mutex> lock(mutex_);return clip_for_type_locked(type)!=nullptr;}
    // v1104 off switch (build/legacy-single-gun-clip): the old single kept clip, whatever type was held when it was captured.
    void legacy_single_gun_clip(bool on) {std::lock_guard<std::mutex> lock(mutex_);legacy_single_clip_=on;}
    // v1104: per hand the held item's type and the authored clip's source (Diagnostic::clip_source: 0 none, 1 captured, 2 sampled, 3 fallback).
    std::array<int,2> clip_sources() const {std::lock_guard<std::mutex> lock(mutex_);return {diag_.clip_source[0],diag_.clip_source[1]};}
    // Held-item grip per hand (status JSON full_body.grip): attachment joint in the drawn-fist frame (metres), wrap centre's distance from the hilt line, reverse flip applied.
    struct GripState {std::array<double,3> attach_in_fist{};double hilt_miss{},reverse_weight{},blade_axis_gap_deg{},hold_native_deg{},hold_native_m{};bool reversed{},has_hold{};int finger_mode{};};
    std::array<GripState,2> grip_state() const {
        std::lock_guard<std::mutex> lock(mutex_);std::array<GripState,2> out{};
        for(unsigned i=0;i<2;++i) {for(unsigned j=0;j<3;++j) out[i].attach_in_fist[j]=out_.fist_attach[i][j];out[i].hilt_miss=out_.hilt_miss[i];out[i].reversed=out_.grip_reversed[i];out[i].reverse_weight=out_.reverse_weight[i];out[i].blade_axis_gap_deg=out_.blade_axis_gap_deg[i];
            out[i].hold_native_deg=out_.hold_native_deg[i];out[i].hold_native_m=out_.hold_native_m[i];out[i].has_hold=out_.has_hold[i];out[i].finger_mode=out_.finger_mode[i];}
        return out;
    }
    // Degrees the last solve turned each thumb joint by (CMC, MCP, IP), per hand.
    std::array<std::array<double,3>,2> thumb_applied() const {
        std::lock_guard<std::mutex> lock(mutex_);std::array<std::array<double,3>,2> out{};
        for(unsigned i=0;i<2;++i) for(unsigned j=0;j<3;++j) out[i][j]=out_.thumb_applied[i][j];
        return out;
    }

private:
    mutable std::mutex mutex_;
    unsigned arm_why_[2]{};
    body::BodySolver solver_;
    body::Inputs solved_inputs_{};
    std::shared_ptr<const body::BodySolver> contact_model_;
    std::uint64_t contact_model_fit_{};
    Xf grip_to_attachment_[2]{};double configured_palm_span_{};
    std::optional<AvatarCalibrationResult> fit_;std::optional<double> native_torso_width_;
    Tracking track_{};body::Ownership own_{};bool have_track_{};
    std::uint64_t revision_{},solved_revision_{},solves_{},failures_{};
    std::array<std::array<std::uint64_t,4>,2> finger_modes_{};std::array<std::array<std::uint64_t,2>,2> finger_inputs_{};
    body::NativePose native_{};bool have_native_body_{},have_native_arm_[2]{};std::array<std::vector<Xf>,2> mirrored_arm_{}; // mirror_native
    // v1104: the authored gun clip PER COMPONENT TYPE (1 pistol, 2 crossbow). gun_clip_ = the calm captured clip; gun_clip_first_ =
    // the first live frame of that type (a sampled clip, used until a calm one exists); baked_ = the engine's own idle clip for the
    // type from src/weapon_grip_clips.hpp. A hand never takes another type's clip.
    std::array<std::vector<Xf>,3> gun_clip_{},gun_clip_first_{};std::vector<Xf> legacy_gun_clip_{};Xf gun_clip_last_{};double gun_clip_calm_{};bool gun_clip_have_last_{};unsigned gun_clip_type_{};
    std::array<std::uint64_t,2> clip_fallbacks_{};bool legacy_single_clip_{};
    Frame body_frame_{};
    double back_=.08,head_scale_=.02,eye_speed_{};
    // Locomotion lag: the solved world joints come from the eye sampled at publish time, but the
    // native model frame belongs to the upload's own game frame. While standing, learn where each
    // frame origin sits relative to the eye (yaw-aligned); while moving, shift the world joints by
    // the deviation so arms/body stay glued to the camera instead of trailing by speed*lag.
    Frame anchored(unsigned slot,Frame f) {
        if(slot==2) {root_last_=f.rigid;have_root_last_=true;}
        // Locomotion and snap turns move the player root between the tracking publication this
        // solution came from and this upload. Carry the solved world pose by exactly that root
        // motion: model = inverse(f)*(root_now*inverse(root_solved))*world, i.e. use
        // f' = root_solved*inverse(root_now)*f. Head rotation alone does not move the root; head-follow's turn of it is left out (root_now()).
        if(!have_root_solved_ || !have_root_last_) return f;
        f.rigid=body::mul(body::mul(root_solved_,body::inverse(root_now())),f.rigid);return f;
    }
    // The root as the carry sees it: its yaw since the solve minus what NativeTurning's head-follow turned it by (about its own axis). Head-follow turns the
    // player to face the head and BodyAnchor takes the same turn off the anchor heading, so the camera, the controllers and the hands solved from them keep
    // their world pose; carrying the hands by that turn too rotated the sword about the player on every physics step of a head turn. A stick turn is not
    // taken off (nothing compensates it), so locomotion and snap turns still carry the hands.
    Xf root_now() const {
        // v832: with the native camera heading known at the solve's Setup and at this upload, the turn carried is the camera anchor's own (the heading
        // minus head-follow, which BodyAnchor takes off): the body model's yaw need not turn in step with the view. build/legacy-root-yaw-carry: the root's.
        if(!legacy_root_yaw_ && have_heading_last_ && have_heading_solved_)
            return {body::mul(body::rot_z(body::wrap_pi((heading_last_-follow_last_)-(heading_solved_-follow_solved_))),root_solved_.q),root_last_.p};
        const double turn=body::twist_angle(body::mul(root_last_.q,body::conj(root_solved_.q)),{0,0,1}),follow=body::wrap_pi(follow_last_-follow_solved_);
        const double remove=std::clamp(follow,std::min(0.,turn),std::max(0.,turn));   // never more than the root really turned the same way
        return {body::mul(body::rot_z(-remove),root_last_.q),root_last_.p};
    }
    Xf world_from_solve() const {return body::mul(root_now(),body::inverse(root_solved_));}
    bool legacy_root_carry_{};std::uint64_t root_gap_publishes_{};
    double heading_last_{},heading_at_publish_{},heading_solved_{};bool have_heading_last_{},have_heading_at_publish_{},have_heading_solved_{},legacy_root_yaw_{},legacy_item_lag_{};
    XrSample xr_latest_{};bool have_xr_latest_{},legacy_hand_sample_{};std::uint64_t sample_refreshes_{};
    // The publication's eye and grips moved on by the newest sample's motion under the publication's own map: exact for the rigid map, and a
    // clamped or palm-offset grip keeps its offset. A jump (over 0.5 m or 57 deg in one sample) is tracking, not motion: that pose stays.
    void refresh_sample_locked() {
        if(legacy_hand_sample_ || !have_xr_latest_ || !track_.xr_map || xr_latest_.map!=track_.xr_map || xr_latest_.serial<=track_.xr_serial) return;
        const auto& s=xr_latest_;
        const auto plausible=[](const Xf& from,const Xf& to) {   // the pose's own travel and turn, not the delta's (that grows with the distance from the world origin)
            const auto turn=body::mul(to.q,body::conj(from.q));return length(to.p-from.p)<.5 && std::abs(turn.w)>std::cos(.5);};
        if(plausible(track_.xr_head_world,s.head)) track_.eye=body::mul(body::mul(s.head,body::inverse(track_.xr_head_world)),track_.eye);
        for(unsigned i=0;i<2;++i) {
            if(!((track_.xr_hands&s.hands)>>i&1u) || !track_.grip_valid[i] || !plausible(track_.xr_grip_world[i],s.grip[i])) continue;
            const auto dg=body::mul(s.grip[i],body::inverse(track_.xr_grip_world[i]));
            track_.grip[i]=body::mul(dg,track_.grip[i]);
            if(track_.raw_grip_hands>>i&1u) track_.raw_grip[i]=body::mul(dg,track_.raw_grip[i]);
        }
        track_.xr_head_world=s.head;for(unsigned i=0;i<2;++i) track_.xr_grip_world[i]=s.grip[i];track_.xr_hands=s.hands;track_.xr_serial=s.serial;++sample_refreshes_;
    }
    Xf root_last_{},root_at_publish_{},root_solved_{};bool have_root_last_{},have_root_at_publish_{},have_root_solved_{};
    double follow_last_{},follow_at_publish_{},follow_solved_{};   // NativeTurning's head-follow total (rad) at the last upload, at the last publish, at the solve
    // Engine frame (present index) whose solution every upload of that frame reuses: the render
    // thread publishes tracking concurrently with the next frame's animation uploads, so without
    // this body and the two arms can be solved from different publications within one frame.
    std::uint64_t latched_key_{};double solved_speed_{};
    std::array<int,4> folded_{};
    body::Output out_{};bool have_out_{};
    std::chrono::steady_clock::time_point last_{},published_{};
    Tracking solved_track_{};Xf solved_eye_{};Diagnostic diag_{};std::uint64_t fit_revision_{};

    void rebuild_hand_mapping() {
        Vec3 palms[2]{};const auto& b=solver_.skeleton(0);const auto chest=b.bind[b.find("center_spine03_jnt")];
        for(unsigned side=0;side<2;++side) {
            const auto& a=solver_.skeleton(1+side);
            const std::string prefix=side?"right":"left";
            std::array<Transform,4> knuckles{};
            const char* fingers[]={"index","medium","ring","pinky"};
            for(unsigned i=0;i<4;++i) knuckles[i]=to_transform(a.bind[a.find(prefix+"_"+fingers[i]+"_02_jnt")]);
            const auto palm=to_xf(rig::hand_bind_grip(to_transform(a.bind[a.find(prefix+"_wrist_jnt")]),knuckles));
            const auto attachment=a.bind[a.find(prefix+"_hand_attachment_jnt")];
            grip_to_attachment_[side]=body::mul(body::inverse(palm),attachment);
            palms[side]=body::mul(chest,palm).p;
        }
        configured_palm_span_=length(palms[0]-palms[1]);
    }
    Xf hand_target(unsigned side,const Xf& grip) const {
        return body::mul(grip,grip_to_attachment_[side]);
    }
    // The solver's solve-space reach carried into the world by the root motion since that solve (as arm_frozen maps
    // poses), with the wrist expressed in the controller grip frame: grip -> attachment -> wrist.
    mutable std::array<ExactReachCache,2> reach_cache_{};   // under mutex_ (diagnostic() is the only caller of world_reach)
    ArmReach world_reach(unsigned side) const {
        const auto r=solver_.solved_reach(side);
        if(!have_out_ || !have_root_solved_ || !have_root_last_ || !r.valid) return {};
        const auto world_from_solve=this->world_from_solve();
        ArmReach result{world_from_solve.p+rotated(world_from_solve.q,r.shoulder),reach_cache_[side&1u].get(r.full),
                        body::mul(grip_to_attachment_[side],r.hand_to_wrist).p,false};
        result.valid=finite(result.shoulder) && finite(result.wrist_local) && std::isfinite(result.max_wrist) && result.max_wrist>0;
        return result;
    }
    std::array<Transform,20> export_body(const body::Output& o,const Frame& conv,const Frame& body_frame) const {
        std::array<Transform,20> out{};
        for(unsigned j=0;j<20;++j) out[j]=to_transform(to_model(conv,o.body[j]));
        if(solver_.torso_scale()!=1) {
            const auto& sk=solver_.skeleton(0);const auto chest_index=sk.find("center_spine03_jnt");
            const auto& chest=o.body[chest_index];
            const auto heading=body::mul(chest.q,body::conj(sk.bind[chest_index].q));
            const auto c=body::conj(body_frame.rigid.q);
            const auto left=rotated(c,rotated(heading,{0,1,0}));
            const auto origin=to_model(body_frame,chest).p;
            const double factor=1+(solver_.torso_scale()-1)*(1-o.owned[1]);
            for(const auto name:{"center_spine01_jnt","center_spine02_jnt","center_spine03_jnt"}) {
                const auto j=static_cast<unsigned>(sk.find(name));out[j]=widen_transverse(out[j],origin,left,factor);
            }
        }
        if(head_scale_<1) {
            // Fold the head chain into a tiny transform at the neck (scaled basis, as hide_arm does).
            const auto& neck=out[folded_[0]];
            for(int j:folded_) {
                auto t=neck;
                for(unsigned axis=0;axis<3;++axis) for(unsigned row=0;row<3;++row) t.f[axis*4+row]*=static_cast<float>(head_scale_);
                out[static_cast<unsigned>(j)]=t;
            }
        }
        return out;
    }
    std::array<Transform,69> export_arm(unsigned side,const std::vector<Xf>& joints,const Frame& conv,const Frame& arm_frame) const {return export_arm(side,joints,conv,arm_frame,out_);}
    std::array<Transform,69> export_arm(unsigned side,const std::vector<Xf>& joints,const Frame& conv,const Frame& arm_frame,const body::Output& o) const {
        std::array<Transform,69> out{};
        for(unsigned index=0;index<69;++index)out[index]=to_transform(to_model(conv,joints[index]));
        if(solver_.torso_scale()!=1) {
            const auto& skeleton=solver_.skeleton(0);const auto index=skeleton.find("center_spine03_jnt");
            const auto heading=body::mul(o.body[index].q,body::conj(skeleton.bind[index].q));
            const auto left=rotated(body::conj(arm_frame.rigid.q),rotated(heading,{0,1,0}));
            const double factor=1+(solver_.torso_scale()-1)*(1-o.owned[1])*(1-o.owned[2+side]);
            const auto& arm=solver_.skeleton(1+side);
            for(const auto joint:{arm.find("origin"),arm.find(side?"right_shoulder_jnt":"left_shoulder_jnt")})
                out[joint]=widen_transverse(out[joint],rig::position(out[joint]),left,factor);
        }
        return out;
    }
    body::Output image_out_{};bool image_valid_{},image_held_ok_{};std::uint64_t image_fit_{},image_begins_{},image_frame_{};
    // v1104: the baked engine idle clip per type (model space, unscaled), built once. clip_for_type_locked picks the best authored
    // clip for a type: the calm captured one, then the first live frame, then the engine's own idle clip; never another type's.
    static const std::vector<Xf>& baked_clip(unsigned type) {
        static const std::array<std::vector<Xf>,3> table=[]{
            std::array<std::vector<Xf>,3> t{};
            for(unsigned k=1;k<=2;++k) if(const auto* baked=grip_clips::sampled(k)) {
                t[k].resize(69);
                for(unsigned j=0;j<69;++j) {const auto& b=(*baked)[j];
                    t[k][j]=Xf{normalized(Quaternion{b.q[0],b.q[1],b.q[2],b.q[3]}),Vec3{b.p[0],b.p[1],b.p[2]}};}
            }
            return t;
        }();
        return table[type<3?type:0];
    }
    const std::vector<Xf>* clip_for_type_locked(unsigned type) const {
        if(legacy_single_clip_) return legacy_gun_clip_.size()==69?&legacy_gun_clip_:nullptr;
        if(type<1 || type>2) return nullptr;
        if(gun_clip_[type].size()==69) return &gun_clip_[type];
        if(gun_clip_first_[type].size()==69) return &gun_clip_first_[type];
        const auto& baked=baked_clip(type);
        return baked.size()==69?&baked:nullptr;
    }
    bool legacy_body_turn_{};std::uint64_t rig_turns_{};
    bool solve_locked(std::uint64_t frame_key=0,bool refresh=true) {
        if(!have_track_ || !have_native_body_) return false;
        if(frame_key && frame_key==latched_key_ && have_out_) return true;
        const auto now=std::chrono::steady_clock::now();
        if(std::chrono::duration<double>(now-published_).count()>tracked_pose_freshness_seconds) {have_out_=false;return false;}
        if(revision_==solved_revision_ && have_out_) {if(frame_key)latched_key_=frame_key;return true;}
        if(refresh) refresh_sample_locked();
        const bool previous=last_.time_since_epoch().count()!=0;
        // Not tick deltas: GetTickCount64 advances in 15.6 ms quanta, so frames read dt 0 (gait,
        // chest yaw and elbow limits frozen) or a doubled step while the eye moved uniformly.
        double dt=previous?track_.seconds-solved_track_.seconds:1/90.;
        diag_.wall_dt=dt;diag_.gap_reset=dt>.5 || dt<0;
        if(diag_.gap_reset) {solver_.reset();dt=1/90.;}
        diag_.substeps=std::max(1u,static_cast<unsigned>(std::ceil(dt/(1/30.))));
        diag_.consumed_dt=dt;diag_.substep_dt=dt/diag_.substeps;
        // v861: a rig turn since the last solve turns the solver's memory (and the substeps' start) with the camera, about the camera anchor.
        Xf from_eye=solved_eye_,from_hand[2]{hand_target(0,solved_track_.grip[0]),hand_target(1,solved_track_.grip[1])};
        if(previous && !diag_.gap_reset && !legacy_body_turn_ && track_.rig_valid && solved_track_.rig_valid && track_.epoch==solved_track_.epoch) {
            const double turn=body::wrap_pi(track_.rig_yaw-solved_track_.rig_yaw);
            if(std::abs(turn)>1e-7 && std::abs(turn)<3.1) {
                solver_.turn_rig(turn,track_.rig_pivot);++rig_turns_;
                const auto q=body::rot_z(turn);const Xf r{q,track_.rig_pivot-rotated(q,track_.rig_pivot)};
                from_eye=body::mul(r,from_eye);for(auto& h:from_hand) h=body::mul(r,h);
            }
        }
        body::Inputs in;
        in.eye=track_.eye;in.dt=dt;in.ground=body_frame_.rigid.p.z;
        {   // Body behind the eye: shift the solver's eye back along the level view direction.
            const auto f=rotated(track_.eye.q,{1,0,0}),u=rotated(track_.eye.q,{0,0,1});
            auto flat=f+u*(-f.z);flat.z=0;
            if(length(flat)>1e-6) in.eye.p=in.eye.p-unit(flat)*back_;
        }
        // v771/v1104: the left arm's native gun clip from its last calm stretch (the attachment turning under 20 deg/s against its parent
        // for 0.25 s; equip, fire and reload clips are not calm), kept PER COMPONENT TYPE so a crossbow copy can never take the pistol's
        // grip (the user's round-4 report). The first live frame of a type is kept too: a sampled clip of the right type, used until a
        // calm one exists; the engine's own idle clip (weapon_grip_clips.hpp) covers a type whose arm was never seen.
        if(track_.slot1_gun && have_native_arm_[0] && native_.arm[0].size()==69) {
            const auto& sk=solver_.skeleton(1);const int at=sk.find("left_hand_attachment_jnt");
            const unsigned type=track_.slot1_type;
            if(type>=1 && type<=2 && at>0 && static_cast<std::size_t>(at)<sk.parent.size() && sk.parent[static_cast<std::size_t>(at)]>=0) {
                if(gun_clip_type_!=type) {gun_clip_type_=type;gun_clip_have_last_=false;gun_clip_calm_=0;}
                const auto& a=native_.arm[0];
                if(gun_clip_first_[type].size()!=69) gun_clip_first_[type]=a;   // sampled: the engine's live pose of this type, first sight
                const auto local=body::mul(body::inverse(a[static_cast<std::size_t>(sk.parent[static_cast<std::size_t>(at)])]),a[static_cast<std::size_t>(at)]);
                const auto& q=gun_clip_last_.q;const double turn=2*std::acos(std::min(1.,std::abs(local.q.x*q.x+local.q.y*q.y+local.q.z*q.z+local.q.w*q.w)));
                const bool calm=gun_clip_have_last_ && dt>1e-4 && turn/dt<20*3.14159265358979323846/180;
                gun_clip_calm_=calm?gun_clip_calm_+dt:0;gun_clip_last_=local;gun_clip_have_last_=true;
                if(gun_clip_calm_>=.25) {gun_clip_[type]=a;legacy_gun_clip_=a;}
            }
        } else gun_clip_have_last_=false;
        for(unsigned i=0;i<2;++i) {
            in.hand_valid[i]=track_.grip_valid[i];
            in.hand[i]=track_.grip_valid[i]?hand_target(i,track_.grip[i]):Xf{};
            in.raw_hand_valid[i]=track_.grip_valid[i] && (track_.raw_grip_hands & track_.tracked_hands & (1u<<i))!=0 && body::valid_pose(track_.raw_grip[i]);
            if(in.raw_hand_valid[i])in.raw_hand[i]=hand_target(i,track_.raw_grip[i]);
            in.grip[i]=track_.grip_value[i];in.trigger[i]=track_.trigger[i];
            in.hand_empty[i]=track_.hand_empty[i];in.weapon_grip[i]=track_.weapon_grip[i];in.native_socket_token[i]=track_.native_socket_token[i];in.reverse_grip[i]=track_.reverse_grip[i];in.item_key[i]=track_.item_key[i];in.weapon_incoming[i]=track_.weapon_incoming[i];
            in.has_finger[i]=track_.has_finger[i];std::memcpy(in.finger_turn[i],track_.finger_turn[i],sizeof(in.finger_turn[i]));
            in.has_finger_axis[i]=track_.has_finger_axis[i];std::memcpy(in.finger_axis[i],track_.finger_axis[i],sizeof(in.finger_axis[i]));
            in.native_arm[i]=have_native_arm_[i]?&native_.arm[i]:nullptr;
            // v763 (user, headset v762: "the right hand must hold every weapon exactly as the left does, the left the sword as the right"): the
            // other arm's own native clip (its hold, fire, reload), mirrored onto this skeleton (power_hand::mirror_joint, exact on all 69 joints).
            if(track_.mirror_native[i] && have_native_arm_[i^1u] && native_.arm[i^1u].size()==69) {
                auto& m=mirrored_arm_[i];m.resize(69);m[0]=native_.arm[i^1u][0];
                for(unsigned j=1;j<69;++j) m[j]=to_xf(power_hand::mirror_joint(to_transform(native_.arm[i^1u][j])));
                in.native_arm[i]=&m;in.native_mirrored[i]=true;
            }
            unsigned source=0;
            if(track_.gun_clip[i]) {
                const auto* clip=clip_for_type_locked(track_.weapon_type[i]);
                if(clip) {
                    const unsigned t=track_.weapon_type[i]<3?track_.weapon_type[i]:0u;
                    source=gun_clip_[t].size()==69?1u:2u;   // captured this session / sampled (first live frame or the baked idle clip)
                    if(i==0) {in.native_arm[0]=clip;in.native_mirrored[0]=false;}
                    else {auto& m=mirrored_arm_[1];m.resize(69);m[0]=(*clip)[0];
                        for(unsigned j=1;j<69;++j) m[j]=to_xf(power_hand::mirror_joint(to_transform((*clip)[j])));
                        in.native_arm[1]=&m;in.native_mirrored[1]=true;}
                } else {
                    // Last resort, and never another type's clip: the procedural grip, with no authored socket to keep.
                    in.weapon_grip[i]=true;in.native_socket_token[i]=0;source=3;++clip_fallbacks_[i];
                }
            }
            if(track_.weapon_grip[i]) source=3;
            // v1105: a type/clip source is only meaningful for a frame whose native hand state was fresh; a menu/load frame would
            // report "none" for a weapon that is still equipped. The last in-gameplay value stays (a held item does not change type
            // while the snapshot is stale), and a real change is reported on the first fresh frame after it.
            if(track_.hand_state_fresh || (track_.weapon_type_fresh && track_.weapon_type[i]!=0)) {
                diag_.weapon_type[i]=static_cast<int>(track_.weapon_type[i]);
                diag_.clip_source[i]=static_cast<int>(source);
            }
            diag_.clip_fallbacks[i]=clip_fallbacks_[i];
        }
        const bool arms_ready=have_native_arm_[0] && have_native_arm_[1];
        in.native=arms_ready?&native_:nullptr;
        in.own=arms_ready?own_:body::Ownership{};
        try {
            const auto target=in;
            const auto interpolate=[](const Xf& a,const Xf& b,double t) {return Xf{body::nlerp(a.q,b.q,t),a.p+(b.p-a.p)*t};};
            for(unsigned step=1;step<=diag_.substeps;++step) {
                in=target;in.dt=diag_.substep_dt;
                if(previous && !diag_.gap_reset) {
                    const double t=static_cast<double>(step)/diag_.substeps;
                    in.eye=interpolate(from_eye,target.eye,t);
                    for(unsigned i=0;i<2;++i) if(solved_track_.grip_valid[i] && track_.grip_valid[i])
                        in.hand[i]=interpolate(from_hand[i],target.hand[i],t);
                }
                if(step!=diag_.substeps)in.raw_hand_valid[0]=in.raw_hand_valid[1]=false;
                out_=solver_.solve(in);solved_inputs_=in;
            }
            for(unsigned i=0;i<2;++i) {++finger_modes_[i][static_cast<unsigned>(std::clamp(out_.finger_mode[i],0,3))];
                finger_inputs_[i][0]+=target.has_finger[i]?1u:0u;finger_inputs_[i][1]+=target.hand_empty[i]?1u:0u;}
            for(unsigned i=0;i<2;++i) {diag_.plant[i]=out_.feet[i];diag_.phase[i]=out_.phase[i];
                diag_.swing_from[i]=out_.swing_from[i];diag_.swing_to[i]=out_.swing_to[i];}
            for(unsigned i=0;i<4;++i) diag_.owned[i]=out_.owned[i];
            have_out_=true;latched_key_=frame_key;solved_speed_=eye_speed_;root_solved_=root_at_publish_;follow_solved_=follow_at_publish_;heading_solved_=heading_at_publish_;have_heading_solved_=have_heading_at_publish_;have_root_solved_=have_root_at_publish_;solved_revision_=revision_;solved_track_=track_;solved_eye_=target.eye;last_=now;++solves_;return true;
        }
        catch(...) {++failures_;have_out_=false;return false;}
    }
};
using PreparedContactArm=Driver::PreparedContactArm;
using ContactArm=Driver::ContactArm;
} // namespace d2vr::fullbody
