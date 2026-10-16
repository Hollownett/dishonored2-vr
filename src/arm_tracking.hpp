#pragma once
#include "tracked_pose_lifetime.hpp"
#include "arm_palette.hpp"
#include "arm_body.hpp"
#include "hand_bind.hpp"
#include "finger_pose.hpp"
#include "native_hand_reference.hpp"
#include "native_finger_grip.hpp"
#include <atomic>
#include "avatar_calibration.hpp"
#include <mutex>
#include <string>

namespace d2vr::rig {
struct NativeBodyBind {
    std::uintptr_t model{},animation{},descriptor{},skeleton{},names{},inverse{};
    Identity animation_identity{};
    std::array<std::uint16_t,20> ids{};
    std::array<std::string,20> labels{};
    std::array<std::array<float,12>,20> inverse_rows{};
    Transform spine{},camera{};
};
inline bool operator==(const NativeBodyBind& a,const NativeBodyBind& b) {
    return a.model==b.model && a.animation==b.animation && a.descriptor==b.descriptor && a.skeleton==b.skeleton &&
        a.names==b.names && a.inverse==b.inverse && a.animation_identity.generation==b.animation_identity.generation &&
        a.ids==b.ids && a.labels==b.labels && a.inverse_rows==b.inverse_rows;
}
struct ArmBinding {
    ArmHierarchy parents;ArmChain chain;Transform bind_wrist{},bind_grip{};ForearmHelpers helpers{};FingerBinding fingers{};
    FingerChains finger_chains{};
    std::array<std::array<float,12>,69> inverse{};
    std::optional<Vec3> native_camera_to_shoulder_body;
    HandJoints palm{};
};
template<class Read> std::optional<std::string> joint_name(Read& read,std::uintptr_t base,std::uint16_t id) {
    const auto manager=value<std::uintptr_t>(read,base+0x22995c0);
    if(id==0xffff || manager!=base+0x22995d0 || value<std::uintptr_t>(read,*manager)!=base+0x1b093c8) return {};
    const auto entries=list(read,*manager+8,65535);
    if(!entries || id>=entries->packed_count) return {};
    const auto text=value<std::uintptr_t>(read,entries->data+id*0x48+0x10);
    if(!text || *text<0x10000 || *text>=0x0000800000000000-128) return {};
    // One read per page-bounded chunk (was one read per byte: millions of calls per minute).
    std::string result;
    for(std::uintptr_t at=*text,end=*text+128;at<end;) {
        std::array<unsigned char,128> chunk{};
        const auto size=static_cast<std::size_t>(std::min<std::uintptr_t>(end-at,0x1000-(at&0xfff)));
        if(!read(at,chunk.data(),size)) return {};
        for(std::size_t i=0;i<size;++i) {
            const auto c=chunk[i];
            if(!c) return result.empty()?std::nullopt:std::optional<std::string>(result);
            if(c<32 || c>126) return {};
            result.push_back(static_cast<char>(c));
        }
        at+=size;
    }
    return {};
}
template<class Read> std::optional<NativeBodyBind> native_body_bind(Read& read,std::uintptr_t base,const Arm& arm) {
    const auto owned=player_arms(read,base);
    if(!owned || arm.side>1 || !((*owned)[arm.side]==arm)) return {};
    NativeBodyBind result{};
    const auto model=value<std::uintptr_t>(read,arm.player+0xf0),animation=value<std::uintptr_t>(read,arm.body+0x58);
    if(!model || !animation || !typed(read,base,*model,0x1b0d628) ||
        !typed(read,base,*animation,0x1b80408,0x1b804f0) || value<std::uintptr_t>(read,*animation+0x38)!=model) return {};
    const auto identity=started(read,*animation);const auto descriptor=value<std::uintptr_t>(read,*model+0x1fe8);
    if(!identity || !descriptor || !pointer(*descriptor)) return {};
    const auto skeleton=value<std::uintptr_t>(read,*descriptor+0x50);
    if(!skeleton || !typed(read,base,*skeleton,0x1b0bd08) || value<std::uint16_t>(read,*skeleton+0x78)!=20) return {};
    const auto names=value<std::uintptr_t>(read,*skeleton+0x70),inverse=value<std::uintptr_t>(read,*skeleton+0x120);
    if(!names || !inverse || !pointer(*names) || !pointer(*inverse)) return {};
    result.model=*model;result.animation=*animation;result.animation_identity=*identity;
    result.descriptor=*descriptor;result.skeleton=*skeleton;result.names=*names;result.inverse=*inverse;
    if(!read(*names,result.ids.data(),sizeof(result.ids)) || !read(*inverse,result.inverse_rows.data(),sizeof(result.inverse_rows))) return {};
    unsigned spine=20,camera=20;
    for(unsigned joint=0;joint<20;++joint) {
        const auto label=joint_name(read,base,result.ids[joint]);if(!label) return {};
        result.labels[joint]=*label;
        if(*label=="center_spine03_jnt") {if(spine!=20) return {};spine=joint;}
        if(*label=="camera_jnt") {if(camera!=20) return {};camera=joint;}
    }
    if(spine>=20 || camera>=20 || result.labels[0]!="origin") return {};
    try {
        for(const auto& row:result.inverse_rows) (void)bind_transform(row);
        result.spine=bind_transform(result.inverse_rows[spine]);result.camera=bind_transform(result.inverse_rows[camera]);
    } catch(...) {return {};}
    std::array<std::uint16_t,20> again{};std::array<std::array<float,12>,20> inverse_again{};
    const auto after=player_arms(read,base);
    if(!after || !((*after)[arm.side]==arm) || value<std::uintptr_t>(read,arm.player+0xf0)!=model ||
        value<std::uintptr_t>(read,arm.body+0x58)!=animation || started(read,*animation)!=identity ||
        value<std::uintptr_t>(read,*animation+0x38)!=model || value<std::uintptr_t>(read,*model+0x1fe8)!=descriptor ||
        value<std::uintptr_t>(read,*descriptor+0x50)!=skeleton || value<std::uint16_t>(read,*skeleton+0x78)!=20 ||
        value<std::uintptr_t>(read,*skeleton+0x70)!=names || value<std::uintptr_t>(read,*skeleton+0x120)!=inverse ||
        !read(*names,again.data(),sizeof(again)) || again!=result.ids ||
        !read(*inverse,inverse_again.data(),sizeof(inverse_again)) || inverse_again!=result.inverse_rows) return {};
    for(unsigned joint=0;joint<20;++joint) if(joint_name(read,base,result.ids[joint])!=result.labels[joint]) return {};
    return result;
}
inline Vec3 native_camera_to_shoulder(const NativeBodyBind& body,const Transform& arm_bind_shoulder) {
    const auto local=position(arm_bind_shoulder);
    const auto shoulder=position(body.spine)+column(body.spine,0)*local.x+column(body.spine,1)*local.y+column(body.spine,2)*local.z;
    const auto offset=shoulder-position(body.camera);
    if(!finite(offset) || offset.x<-.3 || offset.x>.15 || std::abs(offset.y)<.05 || std::abs(offset.y)>.5 || offset.z>-.05 || offset.z<-.5)
        throw std::invalid_argument("Invalid native anatomical camera-to-shoulder bind");
    return offset;
}
template<class Read> std::optional<ArmBinding> arm_binding(Read& read,std::uintptr_t base,const Arm& arm) {
    auto parents=arm_hierarchy(read,base,arm);
    const auto names=value<std::uintptr_t>(read,arm.skeleton+0x70);
    if(!parents || !names || !pointer(*names)) return {};
    std::array<std::uint16_t,69> ids{},again{};
    if(!read(*names,ids.data(),sizeof(ids))) return {};
    const std::string prefix=arm.side?"right":"left";
    const std::array<std::string,11> roles{prefix+"_arm_jnt",prefix+"_elbow_jnt",prefix+"_wrist_jnt",
        prefix+"_index_02_jnt",prefix+"_medium_02_jnt",prefix+"_ring_02_jnt",prefix+"_pinky_02_jnt",
        prefix+"_lower_armhelper1_jnt",prefix+"_lower_armhelper2_jnt",prefix+"_lower_armhelper3_jnt",prefix+"_wrist_rotation_jnt"};
    std::array<unsigned,11> joints{};joints.fill(69);
    std::array<std::string,69> resolved_names;
    for(unsigned joint=0;joint<69;++joint) {
        const auto name=joint_name(read,base,ids[joint]);
        if(!name) return {};
        resolved_names[joint]=*name;
        for(unsigned role=0;role<roles.size();++role) if(*name==roles[role]) {
            if(joints[role]!=69) return {};
            joints[role]=joint;
        }
    }
    for(auto joint:joints) if(joint>=69) return {};
    if((*parents)[joints[1]]!=static_cast<int>(joints[0]) || (*parents)[joints[2]]!=static_cast<int>(joints[1])) return {};
    for(unsigned role=3;role<7;++role) {
        const auto metacarpal=(*parents)[joints[role]];
        if(metacarpal<0 || (*parents)[metacarpal]!=static_cast<int>(joints[2]) ||
           joint_name(read,base,ids[metacarpal])!=roles[role].substr(0,roles[role].size()-6)+"01_jnt") return {};
    }
    for(unsigned role=7;role<roles.size();++role) if((*parents)[joints[role]]!=static_cast<int>(joints[1])) return {};
    const auto inverse_at=value<std::uintptr_t>(read,arm.skeleton+0x120);
    std::array<std::array<float,12>,69> inverse{},inverse_again{};
    if(!inverse_at || !pointer(*inverse_at) || !read(*inverse_at,inverse.data(),sizeof(inverse))) return {};
    Transform bind_wrist{},bind_grip{};
    try {
        bind_wrist=bind_transform(inverse[joints[2]]);
        std::array<Transform,4> knuckles{};
        for(unsigned i=0;i<4;++i) knuckles[i]=bind_transform(inverse[joints[i+3]]);
        bind_grip=hand_bind_grip(bind_wrist,knuckles);
    } catch(const std::invalid_argument&) {return {};}
    auto current=player_arms(read,base);
    if(!current || !((*current)[arm.side]==arm) || value<std::uintptr_t>(read,arm.skeleton+0x70)!=names ||
       !read(*names,again.data(),sizeof(again)) || ids!=again ||
       value<std::uintptr_t>(read,arm.skeleton+0x120)!=inverse_at ||
       !read(*inverse_at,inverse_again.data(),sizeof(inverse_again)) || inverse!=inverse_again) return {};
    // Resolve the roles twice so name-pool replacement cannot silently rebind.
    for(unsigned role=0;role<roles.size();++role) if(joint_name(read,base,ids[joints[role]])!=roles[role]) return {};
    std::array<Transform,69> bind{};FingerChains chains{};
    try {for(unsigned joint=0;joint<69;++joint) bind[joint]=bind_transform(inverse[joint]);} catch(...) {return {};}
    const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned finger=0;finger<5;++finger) for(unsigned segment=0;segment<3;++segment) {
        const auto role=prefix+"_"+digits[finger]+"_0"+std::to_string(segment+(finger?2:1))+"_jnt";
        unsigned found=69;
        for(unsigned joint=0;joint<69;++joint) if(resolved_names[joint]==role) {if(found!=69) return {};found=joint;}
        if(found>=69) return {};chains[finger][segment]=found;
    }
    FingerBinding fingers{};
    try {fingers=finger_binding(bind,*parents,chains);} catch(...) {return {};}
    ArmBinding result{*parents,{joints[0],joints[1],joints[2]},bind_wrist,bind_grip,{joints[7],joints[8],joints[9],joints[10]},fingers,chains,inverse,{},{}};
    for(unsigned joint=0;joint<69;++joint) {
        if(resolved_names[joint]==prefix+"_helper_underwrist_jnt") result.palm.underwrist=joint;
        if(resolved_names[joint]==prefix+"_helper_upperwrist_jnt") result.palm.upperwrist=joint;
    }
    if(const auto body=native_body_bind(read,base,arm)) {
        try {result.native_camera_to_shoulder_body=native_camera_to_shoulder(*body,bind[joints[0]]);} catch(...) {return {};}
    }
    return result;
}
inline NativeAvatarMeasurements native_avatar_measurements(const std::array<ArmBinding,2>& bindings,
    const std::array<Upload,2>& uploads,double units_per_metre) {
    if(!std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000)
        throw std::invalid_argument("Invalid measured native avatar units");
    std::array<Vec3,2> palms{},shoulders{};
    for(unsigned side=0;side<2;++side) {
        if(bindings[side].chain.upper>=69) throw std::invalid_argument("Invalid native shoulder role");
        const ModelFrame model(uploads[side].model_frame);
        palms[side]=model.world_point(position(bindings[side].bind_grip));
        shoulders[side]=model.world_point(position(bind_transform(bindings[side].inverse[bindings[side].chain.upper])));
    }
    const NativeAvatarMeasurements result{length(palms[1]-palms[0])/units_per_metre,length(shoulders[1]-shoulders[0])/units_per_metre};
    if(!std::isfinite(result.palm_span_metres) || result.palm_span_metres<.8 || result.palm_span_metres>2.5 ||
        result.shoulder_span_metres<.15 || result.shoulder_span_metres>.65)
        throw std::invalid_argument("Native bind measurements are not a verified T pose");
    return result;
}
struct ArmTrackingFrame {
    std::uint64_t source_tick{}; // XR sample clock; tick below is native-world publication clock.
    std::uint64_t tick{},epoch{};
    unsigned hands{};
    std::array<WorldTrackedPose,2> grips{};
    std::array<float,2> curls{};
    std::array<float,2> index_curls{};
    bool independent_index{};
    std::array<std::array<float,5>,2> finger_curls{}; // per finger (thumb..pinky) after finger contact IK (finger_ik.hpp)
    bool has_finger_curls{};
    std::uint64_t reference_epoch{};
    WorldTrackedPose head_anchor{}; // Position and level body axes, world units.
    bool has_head_anchor{};
    double units_per_metre{1};
    unsigned empty_hands{}; // Exact native empty-hand evidence, never squeeze.
    unsigned native_hands{}; // Bit per side: hand the arm back to the native animation entirely.
    // Native bind camera-to-shoulder forward component, in world units.
    // The caller qualifies body camera/spine binds; no invented torso depth.
    std::array<double,2> native_shoulder_forward{};
    bool has_native_shoulder_forward{};
    // Only then do empty_hands/native_hands apply; false keeps legacy behaviour.
    // Declared here to reuse existing padding, so this frame's layout is stable.
    bool has_hand_state{};
    // Producer origin retained for diagnostics only; grips are world poses and
    // each side's current native upload supplies its own conversion frame.
    Vec3 model_origin{};
    bool has_model_origin{};
};
// The render thread publishes only owned values. Animation workers copy them
// under this mutex; no XR calls, native calls or file operations hold the lock.
struct ArmTrackingSnapshot {
    std::uint64_t revision{};
    ArmTrackingFrame frame{};
    std::array<std::optional<NativeHandReference>,2> references{};
    std::optional<AvatarCalibrationResult> calibration;
    std::uint64_t mapping_revision{};
    std::array<Vec3,2> elbow_poles{};
    unsigned elbow_pole_hands{};
    double chest_yaw{};bool has_chest{}; // Smoothed chest heading (world yaw), see ChestYaw.
};
struct ArmTrackingStamp {
    std::uint64_t revision{},tick{},epoch{},reference_epoch{};
    unsigned hands{};bool head_anchor{},calibrated{};
    std::uint64_t mapping_revision{};
};
class ArmTracking {
    std::mutex mutex_;
    ArmTrackingFrame frame_{};
    std::array<std::optional<NativeHandReference>,2> references_{};
    std::optional<AvatarCalibrationResult> calibration_;
    std::uint64_t revision_{};
    std::uint64_t mapping_revision_{};
    std::array<Vec3,2> elbow_poles_{};
    unsigned elbow_pole_hands_{};
    ChestYaw chest_{};std::uint64_t chest_tick_{},chest_epoch_{};
    void advance_revision() {
        if(revision_==UINT64_MAX) throw std::overflow_error("Tracked publication revision exhausted");
        ++revision_;
    }
public:
    ArmTrackingSnapshot snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {revision_,frame_,references_,calibration_,mapping_revision_,elbow_poles_,elbow_pole_hands_,chest_.yaw,chest_.valid};
    }
    ArmTrackingStamp stamp() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {revision_,frame_.tick,frame_.epoch,frame_.reference_epoch,frame_.hands,frame_.has_head_anchor,calibration_.has_value(),mapping_revision_};
    }
    void configure_calibration(AvatarCalibrationResult calibration) {
        if(!calibration.reference_epoch || !std::isfinite(calibration.arm_scale) || calibration.arm_scale<.7 || calibration.arm_scale>1.45 ||
            !std::isfinite(calibration.shoulder_span_metres) || calibration.shoulder_span_metres<.1 || calibration.shoulder_span_metres>.95 ||
            !std::isfinite(calibration.eye_to_shoulder_metres) || calibration.eye_to_shoulder_metres<.05 || calibration.eye_to_shoulder_metres>.4)
            throw std::invalid_argument("Invalid measured arm calibration");
        std::lock_guard<std::mutex> lock(mutex_);
        if(calibration_ && same_avatar_calibration(*calibration_,calibration)) return;
        advance_revision();++mapping_revision_;elbow_pole_hands_=0;calibration_=calibration;
    }
    void clear_calibration() {std::lock_guard<std::mutex> lock(mutex_);advance_revision();++mapping_revision_;elbow_pole_hands_=0;calibration_.reset();}
    void install_reference(unsigned side,NativeHandReference reference) {
        if(side>=2 || !reference.fingers.native_reference) throw std::invalid_argument("Uncaptured native hand reference");
        // Revalidate even owned values at the installation boundary.
        reference=native_hand_reference(reference.fingers.open,reference.fingers.closed,reference.fingers.parents,
            reference.chains,reference.inverse,reference.palm_helpers);
        std::lock_guard<std::mutex> lock(mutex_);advance_revision();++mapping_revision_;elbow_pole_hands_=0;references_[side]=std::move(reference);
    }
    void clear_references() {std::lock_guard<std::mutex> lock(mutex_);advance_revision();++mapping_revision_;elbow_pole_hands_=0;references_={};}
    ArmTrackingStamp publish(ArmTrackingFrame frame) {
        if(frame.hands>3 || frame.empty_hands>3 || frame.native_hands>3 || (frame.hands && !frame.epoch)) throw std::invalid_argument("Invalid tracked arm publication");
        for(auto curl:frame.curls) if(!std::isfinite(curl) || curl<0 || curl>1) throw std::invalid_argument("Invalid tracked finger curl");
        for(auto curl:frame.index_curls) if(!std::isfinite(curl) || curl<0 || curl>1) throw std::invalid_argument("Invalid tracked index curl");
        for(const auto& hand:frame.finger_curls) for(auto curl:hand) if(!std::isfinite(curl) || curl<0 || curl>1) throw std::invalid_argument("Invalid finger contact curl");
        if(frame.has_head_anchor) {
            if(!frame.reference_epoch || !finite(frame.head_anchor.position) || !std::isfinite(frame.units_per_metre) ||
                frame.units_per_metre<=0 || frame.units_per_metre>1000) throw std::invalid_argument("Invalid calibrated head anchor");
            for(unsigned i=0;i<3;++i) {
                if(!finite(frame.head_anchor.basis[i])) throw std::invalid_argument("Invalid calibrated body axes");
                for(unsigned j=0;j<=i;++j) if(std::abs(dot(frame.head_anchor.basis[i],frame.head_anchor.basis[j])-(i==j?1.:0.))>.01)
                    throw std::invalid_argument("Calibrated body axes are not orthonormal");
            }
            if(dot(cross(frame.head_anchor.basis[0],frame.head_anchor.basis[1]),frame.head_anchor.basis[2])<.99 ||
                length(frame.head_anchor.basis[2]-Vec3{0,0,1})>.01) throw std::invalid_argument("Calibrated body anchor must be level");
        }
        if(frame.has_native_shoulder_forward) for(const auto offset:frame.native_shoulder_forward)
            if(!std::isfinite(offset) || std::abs(offset)>frame.units_per_metre*.3)
                throw std::invalid_argument("Invalid owned native shoulder depth");
        std::lock_guard<std::mutex> lock(mutex_);
        advance_revision();
        unsigned poles{};
        if(frame.has_head_anchor) {
            // Chest heading: dead-zone follow of the head plus hand pull; reset on a new space or a gap.
            const bool continuous=chest_.valid && chest_epoch_==frame.reference_epoch && frame_.has_head_anchor &&
                frame.tick>=chest_tick_ && frame.tick-chest_tick_<1000;
            if(!continuous) chest_.valid=false;
            const double dt=continuous?(frame.tick-chest_tick_)/1000.:0.;
            double hand{};const bool hands=frame.hands==3 && both_hands_yaw(frame.head_anchor.position,frame.grips[0].position,
                frame.grips[1].position,frame.head_anchor.basis[0],frame.units_per_metre,hand);
            chest_.step(yaw_of(frame.head_anchor.basis[0]),hands?&hand:nullptr,dt);
            chest_tick_=frame.tick;chest_epoch_=frame.reference_epoch;
            for(unsigned side=0;side<2;++side) if(frame.hands&(1u<<side)) {
                const auto shoulder=hand_shoulder(frame,calibration_,side,&chest_.yaw);
                const auto reach=frame.grips[side].position-shoulder;
                if(length(reach)<1e-6) continue;
                const Vec3* previous=(elbow_pole_hands_&(1u<<side)) && continuous && frame_.epoch==frame.epoch?&elbow_poles_[side]:nullptr;
                elbow_poles_[side]=elbow_pole(frame,calibration_,side,chest_.yaw,reach,previous,dt);
                poles|=1u<<side;
            }
        } else chest_.valid=false;
        elbow_pole_hands_=poles;
        frame_=frame;
        return {revision_,frame_.tick,frame_.epoch,frame_.reference_epoch,frame_.hands,frame_.has_head_anchor,calibration_.has_value(),mapping_revision_};
    }
    void clear() {publish({});}
    // Measured fit when it belongs to this reference space, otherwise typical
    // adult proportions. It anchors the rendered upper arm (full arms) and steers
    // the forearm; defaults are reasonable before calibration.
    // chest_yaw (smoothed, see ChestYaw) places the shoulders on the chest frame
    // instead of the head: neck below the head along gravity, shoulders across the chest.
    static bool fitted(const ArmTrackingFrame& frame,const std::optional<AvatarCalibrationResult>& c) {return c && c->reference_epoch==frame.reference_epoch;}
    static double arm_scale(const ArmTrackingFrame& frame,const std::optional<AvatarCalibrationResult>& c) {return fitted(frame,c)?c->arm_scale:1.;}
    // Shoulder-to-palm reach in metres: measured from the T pose, typical adult otherwise.
    static double arm_length_metres(const ArmTrackingFrame& frame,const std::optional<AvatarCalibrationResult>& c) {
        return fitted(frame,c)?std::max(.3,(c->palm_span_metres-c->shoulder_span_metres)*.5):.62;
    }
    static Vec3 shoulder_world(const ArmTrackingFrame& frame,const std::optional<AvatarCalibrationResult>& calibration,unsigned side,const double* chest_yaw=nullptr) {
        const bool fit=fitted(frame,calibration);
        const double span=fit?calibration->shoulder_span_metres:.36,drop=fit?calibration->eye_to_shoulder_metres:.2;
        const auto& anchor=frame.head_anchor;const auto u=frame.units_per_metre;
        const Vec3 forward=chest_yaw?yaw_forward(*chest_yaw):anchor.basis[0],left=chest_yaw?yaw_left(*chest_yaw):anchor.basis[1];
        // Native bind depth (world units) when the caller qualified it, scaled to the user; otherwise 6 cm behind.
        const double back=frame.has_native_shoulder_forward?std::clamp(frame.native_shoulder_forward[side&1]*arm_scale(frame,calibration),-.2*u,.05*u):-.06*u;
        return anchor.position+left*((side?-.5:.5)*span*u)-anchor.basis[2]*(drop*u)+forward*back;
    }
    // Shoulder after bounded clavicle elevation/protraction toward the hand.
    static Vec3 hand_shoulder(const ArmTrackingFrame& frame,const std::optional<AvatarCalibrationResult>& c,unsigned side,const double* chest_yaw) {
        const auto base=shoulder_world(frame,c,side,chest_yaw);const auto len=arm_length_metres(frame,c)*frame.units_per_metre;
        return base+shoulder_reach_offset(frame.grips[side].position-base,len,.08*len);
    }
    static Vec3 elbow_pole(const ArmTrackingFrame& frame,const std::optional<AvatarCalibrationResult>& c,unsigned side,double chest_yaw,
                           Vec3 reach,const Vec3* previous,double dt) {
        const auto& up=frame.head_anchor.basis[2];
        return boneworks_elbow_pole(reach,yaw_forward(chest_yaw),yaw_left(chest_yaw)*(side?-1.:1.),up,
            arm_length_metres(frame,c)*frame.units_per_metre,dot(frame.grips[side].basis[0],up),previous,dt);
    }
    std::optional<PosedArm> pose(const Upload& native,const ArmBinding& binding,unsigned side,std::uint64_t tick) {
        return pose_frozen(native,binding,side,tick,snapshot());
    }
    // Game-grip fists (default; build/fist-mirror-axis "off" restores the procedural curl).
    // Each hand caches its OWN closed native grip (right: sword, left: pistol/crossbow)
    // and closes into it when empty. Mirroring one hand onto the other splayed the fingers.
    inline static std::atomic<bool> game_grip_fists{true};
    inline static std::atomic<bool> full_arms{false};
    inline static std::mutex grip_mutex_;
    inline static std::array<std::optional<FingerGrip>,2> native_grips_{};
    // Index and middle fingertips pulled in toward the wrist: a held grip, not an open hand.
    static bool curled(const std::array<Transform,69>& joints,unsigned wrist,const FingerChains& chains) {
        double ratio=0;
        for(unsigned finger:{1u,2u}) {
            const auto& c=chains[finger];const auto w=position(joints[wrist]);
            const double path=length(position(joints[c[0]])-w)+length(position(joints[c[1]])-position(joints[c[0]]))+
                length(position(joints[c[2]])-position(joints[c[1]]));
            if(path<1e-6) return false;
            ratio+=length(position(joints[c[2]])-w)/path;
        }
        return ratio/2<.8;
    }
    static std::optional<PosedArm> pose_frozen(const Upload& native,const ArmBinding& binding,unsigned side,std::uint64_t tick,
        const ArmTrackingSnapshot& frozen) {
        return pose_frozen_with_arm_mode(native,binding,side,tick,frozen,false);
    }
    static std::optional<PosedArm> pose_frozen_with_arm_mode(const Upload& native,const ArmBinding& binding,unsigned side,std::uint64_t tick,
        const ArmTrackingSnapshot& frozen,bool force_full_arms) {
        const auto& frame=frozen.frame;
        if(side>=2) return {};
        const auto& chain=binding.chain;
        if(chain.upper>=69 || chain.elbow>=69 || chain.wrist>=69) throw std::invalid_argument("Invalid tracked arm chain bounds");
        const bool render_arms=force_full_arms || full_arms.load();
        if((frame.source_tick && (tick<frame.source_tick || tick-frame.source_tick>tracked_pose_freshness_ms)) || !(frame.hands&(1u<<side)) || !frame.epoch || (render_arms && !frame.has_head_anchor) || tick<frame.tick || tick-frame.tick>tracked_pose_freshness_ms) return {};
        // A scripted animation (takedown, finisher, mantle) owns this arm: the
        // caller submits the native palette.
        if(frame.has_hand_state && (frame.native_hands&(1u<<side))) return {};
        const ModelFrame model(native.model_frame);
        // Published grips already occupy world space. Each upload converts
        // through its own model frame; model motion must not move the target.
        const auto world=frame.grips[side];
        Transform grip{};
        const auto point=model.model_point(world.position);
        grip.f[12]=static_cast<float>(point.x);grip.f[13]=static_cast<float>(point.y);grip.f[14]=static_cast<float>(point.z);
        for(unsigned axis=0;axis<3;++axis) {
            const auto direction=model.model_direction(world.basis[axis]);
            grip.f[axis*4]=static_cast<float>(direction.x);grip.f[axis*4+1]=static_cast<float>(direction.y);grip.f[axis*4+2]=static_cast<float>(direction.z);
        }
        if(!valid(grip)) throw std::invalid_argument("Invalid tracked grip frame");
        const auto wrist=rigid_retarget(binding.bind_wrist,binding.bind_grip,grip);
        const auto posed_result=render_arms?[&]() -> std::optional<PosedArm> {
            // Legacy standalone arm mode keeps its anatomical shoulder anchor.
            // The live integrated body uses the shared BodySolver instead.
            const auto upper_length=length(position(native.joints[chain.elbow])-position(native.joints[chain.upper]));
            const auto lower_length=length(position(native.joints[chain.wrist])-position(native.joints[chain.elbow]));
            const double scale=arm_scale(frame,frozen.calibration);
            const double* chest=frozen.has_chest?&frozen.chest_yaw:nullptr;
            const auto shoulder_world_point=hand_shoulder(frame,frozen.calibration,side,chest);
            auto shoulder=model.model_point(shoulder_world_point);
            const auto reach=position(wrist)-shoulder;const auto total=(upper_length+lower_length)*scale;
            if(length(reach)<1e-6 || total<1e-6) return {};
            if(length(reach)>1.06*total) return {};
            const auto source=calibrated_arm_source(native,binding.parents,chain,shoulder,scale);
            const auto previous=(frozen.elbow_pole_hands&(1u<<side))?&frozen.elbow_poles[side]:nullptr;
            // The published pole is already continuity-limited; recompute (unlimited) only without one.
            const auto pole=model.model_direction(previous?*previous:
                elbow_pole(frame,frozen.calibration,side,chest?*chest:yaw_of(frame.head_anchor.basis[0]),world.position-shoulder_world_point,nullptr,0));
            return pose_arm(source,binding.parents,chain,wrist,pole,&binding.helpers,true,1.06);
        }():std::optional<PosedArm>(pose_hand(native,binding.parents,chain,wrist));
        if(!posed_result) return {};
        auto posed=*posed_result;
        const auto recovered=rigid_retarget(binding.bind_grip,binding.bind_wrist,posed.joints[chain.wrist]);
        if(length(position(recovered)-position(grip))>.001)
            throw std::invalid_argument("Tracked native palm missed the controller grip");
        const float squeeze=frame.curls[side],index=frame.independent_index?frame.index_curls[side]:squeeze;
        // Legacy frames (has_hand_state=false) always allow the finger override.
        // Otherwise only a side the game reports empty may be overridden; an
        // occupied side keeps its native fingers while the wrist still follows.
        const bool empty=!frame.has_hand_state || (frame.empty_hands&(1u<<side));
        const bool fists=game_grip_fists.load();
        // While the game animates this hand closed (holding a weapon), remember that grip.
        if(fists && curled(native.joints,chain.wrist,binding.finger_chains)) {
            auto captured=capture_finger_grip(native.joints,binding.parents,binding.finger_chains);
            if(captured.valid) {std::lock_guard<std::mutex> lock(grip_mutex_);native_grips_[side]=captured;}
        }
        if(!empty) {
            // Occupied/scripted hand: the squeeze never curls the native fingers.
        } else {
            std::optional<FingerGrip> fist;
            if(fists) {std::lock_guard<std::mutex> lock(grip_mutex_);fist=native_grips_[side];}
            // Finger contact IK (render thread) already holds each finger at the held object's surface.
            if(fist && fist->valid) posed.joints=apply_finger_grip(posed.joints,binding.parents,*fist,
                frame.has_finger_curls?frame.finger_curls[side]:std::array<float,5>{std::max(squeeze,.5f*index),index,squeeze,squeeze,squeeze});
            else posed.joints=procedural_curl(posed.joints,binding.parents,binding.finger_chains,binding.palm,chain.wrist,
                frame.has_finger_curls?frame.finger_curls[side]:std::array<float,5>{squeeze,index,squeeze,squeeze,squeeze});
        }
        if(render_arms) {
            // Experimental full arms: the clavicle and its
            // other children (shoulder helpers) move rigidly with the shoulder joint so the sleeve
            // stays attached instead of stretching back to the native clavicle.
            const int clavicle=binding.parents[chain.upper];
            unsigned clavicle_joints=0,upper_joints=0;
            if(clavicle>=0) for(unsigned joint=0;joint<69;++joint) {
                if(in_subtree(binding.parents,joint,static_cast<unsigned>(clavicle))) ++clavicle_joints;
                if(in_subtree(binding.parents,joint,chain.upper)) ++upper_joints;
            }
            // Only a true clavicle (a few helpers besides the arm), never a spine that also carries the other arm.
            if(clavicle>=0 && binding.parents[static_cast<unsigned>(clavicle)]>=0 && clavicle_joints<=upper_joints+6) {
                // Rotate the clavicle toward the new shoulder (bounded); translate only the residual.
                const auto pivot=position(native.joints[static_cast<unsigned>(clavicle)]);
                const auto native_upper=position(native.joints[chain.upper]),new_upper=position(posed.joints[chain.upper]);
                const auto swing=clavicle_swing(pivot,native_upper,new_upper);
                const auto residual=new_upper-(pivot+rotated(swing,native_upper-pivot));
                posed.clavicle=swing;
                for(unsigned joint=0;joint<69;++joint) {
                    if(in_subtree(binding.parents,joint,chain.upper)) continue;
                    if(joint!=static_cast<unsigned>(clavicle) && !in_subtree(binding.parents,joint,static_cast<unsigned>(clavicle))) continue;
                    const auto at=pivot+rotated(swing,position(native.joints[joint])-pivot)+residual;
                    posed.joints[joint]=turn_root(native.joints[joint],swing,at);
                }
            }
            return posed;
        }
        hide_arm(posed,binding.parents,chain.wrist);
        return posed;
    }
};
} // namespace d2vr::rig
