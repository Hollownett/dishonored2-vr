#include "../src/full_body_driver.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
using namespace d2vr;
using namespace d2vr::fullbody;

const std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};

bool same_transform(const Transform& first,const Transform& second) {
    return std::memcmp(first.f.data(),second.f.data(),sizeof(first.f))==0;
}

template<std::size_t Size>
bool same_array(const std::array<Transform,Size>& first,const std::array<Transform,Size>& second) {
    for(std::size_t joint=0;joint<Size;++joint) if(!same_transform(first[joint],second[joint])) return false;
    return true;
}

struct Fixture {
    Driver driver;
    std::array<Transform,20> native_body{};
    std::array<Transform,69> native_arm[2]{};
    Tracking tracking{};
    body::Ownership ownership{};
    std::array<float,15> model_frame=identity_frame;
    bool last_raw[2]{};
    std::array<std::array<Transform,69>,2> last_arms{};
    Xf reference_eye{};
    Xf reference_grip[2]{};

    Fixture() {
        const auto body_skeleton=skeleton(data::kBody);
        const auto left=skeleton(data::kLeftArm);
        const auto right=skeleton(data::kRightArm);
        for(unsigned joint=0;joint<native_body.size();++joint) native_body[joint]=to_transform(body_skeleton.bind[joint]);
        const auto camera=body_skeleton.bind[body_skeleton.find("camera_jnt")];
        const auto chest=body_skeleton.bind[body_skeleton.find("center_spine03_jnt")];
        tracking.eye={camera.q,camera.p};tracking.epoch=7;tracking.pose_epoch=3;tracking.reference_epoch=5;tracking.mapping_revision=11;
        reference_eye=tracking.eye;
        tracking.tracked_hands=3;tracking.grip_valid[0]=tracking.grip_valid[1]=true;
        tracking.hand_empty[0]=tracking.hand_empty[1]=true;
        for(unsigned side=0;side<2;++side) {
            const auto& arm=side?right:left;
            const std::string prefix=side?"right":"left";
            for(unsigned joint=0;joint<native_arm[side].size();++joint) native_arm[side][joint]=to_transform(arm.bind[joint]);
            const auto wrist=static_cast<unsigned>(arm.find(prefix+"_wrist_jnt"));
            std::array<Transform,4> knuckles{};
            const char* fingers[]={"index","medium","ring","pinky"};
            for(unsigned finger=0;finger<4;++finger) knuckles[finger]=to_transform(arm.bind[arm.find(prefix+"_"+fingers[finger]+"_02_jnt")]);
            tracking.grip[side]=body::mul(chest,to_xf(rig::hand_bind_grip(native_arm[side][wrist],knuckles)));
            reference_grip[side]=tracking.grip[side];
            tracking.grip_valid[side]=true;
            static_cast<void>(driver.arm(side,native_arm[side],identity_frame));
        }
        tracking.tick=1000;tracking.seconds=1.;tracking.tracking_revision=1;tracking.source_tick=1000;
        driver.tune(0,.92);
        driver.publish(tracking,ownership);
        assert(driver.body(native_body,model_frame));
    }

    void publish_pose(unsigned frame,const std::string& scenario,bool candidate_enabled,bool candidate_equal) {
        const double hertz=scenario=="substeps"?20.:90.;
        const double seconds=static_cast<double>(frame+1)/hertz;
        const double phase=seconds*6.2831853071795864769;
        tracking.tick=1000+static_cast<std::uint64_t>(std::llround((frame+1)*1000/hertz));
        tracking.source_tick=tracking.tick;tracking.seconds=1.+seconds;
        tracking.tracking_revision=100+frame;
        tracking.eye=reference_eye;
        tracking.eye.p.x+=.11*seconds;
        tracking.eye.p.y+=.035*std::sin(phase);
        tracking.eye.p.z-=scenario=="crouch"?.2*std::min(1.,seconds/.45):0.;
        tracking.eye.q=body::mul(body::rot_z(scenario=="turn"?.72*std::min(1.,seconds/.5):.1*std::sin(phase)),reference_eye.q);
        for(unsigned side=0;side<2;++side) {
            tracking.grip[side]=reference_grip[side];
            tracking.grip[side].p.x+=tracking.eye.p.x-reference_eye.p.x+.02*std::sin(phase+(side?.3:0));
            tracking.grip[side].p.y+=tracking.eye.p.y-reference_eye.p.y+(side?-.006:.006)*std::sin(phase*.7);
            tracking.grip[side].p.z+=tracking.eye.p.z-reference_eye.p.z+.01*std::cos(phase+(side?.6:0));
            tracking.grip[side].q=body::mul(body::rot_z(.18*std::sin(phase+(side?.4:0))),tracking.grip[side].q);
            tracking.grip_value[side]=.25+.65*(.5+.5*std::sin(phase+(side?.8:0)));
            tracking.trigger[side]=.1+.75*(.5+.5*std::cos(phase+(side?.2:0)));
            tracking.has_finger[side]=true;tracking.has_finger_axis[side]=(frame%2)==0;
            for(unsigned finger=0;finger<5;++finger) for(unsigned joint=0;joint<3;++joint) {
                tracking.finger_turn[side][finger][joint]=static_cast<float>(.09*std::sin(phase+finger*.19+joint*.11+side*.27));
                tracking.finger_axis[side][finger][joint][0]=0;
                tracking.finger_axis[side][finger][joint][1]=static_cast<float>((finger%2)?1:0);
                tracking.finger_axis[side][finger][joint][2]=static_cast<float>((finger%2)?0:1);
            }
            tracking.hand_empty[side]=(frame%37)<30;
            tracking.grip_valid[side]=!(scenario=="invalid" && frame>=8 && frame<15 && side==1);
            tracking.raw_grip[side]=tracking.grip[side];
            if(candidate_enabled && candidate_equal) {
                tracking.raw_grip[side]=tracking.grip[side];
            } else if(candidate_enabled) {
                const Vec3 offset=scenario=="reach"?Vec3{.72,side?-.32:.32,.6}:Vec3{side?-.025:.04,.018*std::sin(phase),.025};
                tracking.raw_grip[side].p=tracking.raw_grip[side].p+offset;
                tracking.raw_grip[side].q=body::mul(body::rot_z(.16),tracking.raw_grip[side].q);
            }
        }
        if(scenario=="native" && frame<32) ownership=body::native_ownership("CarryCorpse");
        else if(scenario=="native" && frame<60) ownership=body::native_ownership("Mantle");
        else ownership={};
        tracking.raw_grip_hands=candidate_enabled?3u:0u;
        if(scenario=="invalid" && frame>=8 && frame<15) {
            tracking.tracked_hands=1;
        } else tracking.tracked_hands=3;
        if(scenario=="calibration" && frame==0) {
            AvatarCalibrationResult fit{13,1.65,.37,.21,1.08,{}};
            driver.configure_calibration(fit);
        }
    }

    std::array<float,15> frame_for() const {
        auto result=identity_frame;
        result[0]=static_cast<float>(tracking.eye.p.x);
        result[1]=static_cast<float>(tracking.eye.p.y);
        result[2]=0.f;
        const auto forward=rotated(tracking.eye.q,{1,0,0});
        const auto left=rotated(tracking.eye.q,{0,1,0});
        const auto up=rotated(tracking.eye.q,{0,0,1});
        result[3]=static_cast<float>(forward.x);result[4]=static_cast<float>(forward.y);result[5]=static_cast<float>(forward.z);
        result[6]=static_cast<float>(left.x);result[7]=static_cast<float>(left.y);result[8]=static_cast<float>(left.z);
        result[9]=static_cast<float>(up.x);result[10]=static_cast<float>(up.y);result[11]=static_cast<float>(up.z);
        result[12]=result[13]=result[14]=1.f;
        return result;
    }

    void replay_frame(unsigned frame,const std::string& scenario,bool candidate_enabled,bool candidate_equal,
                      bool arm_first,bool use_latch,bool verify_raw) {
        publish_pose(frame,scenario,candidate_enabled,candidate_equal);
        const auto current_frame=frame_for();
        model_frame=current_frame;
        const std::uint64_t frame_key=use_latch?frame+100:0;
        if(arm_first) {
            for(unsigned side=0;side<2;++side)
                static_cast<void>(driver.arm_frozen(side,native_arm[side],current_frame,tracking.tick,frame_key));
        }
        driver.publish(tracking,ownership);
        const auto body_pose=driver.body(native_body,current_frame,frame_key);
        assert(body_pose);
        std::array<std::optional<fullbody::ArmPose>,2> saved_arms{};
        for(unsigned side=0;side<2;++side) {
            const auto arm_pose=driver.arm_frozen(side,native_arm[side],current_frame,tracking.tick,frame_key);
            assert(arm_pose);
            saved_arms[side]=arm_pose;
            last_arms[side]=arm_pose->joints;
            last_raw[side]=arm_pose->raw_joints.has_value();
            if(verify_raw && !arm_first) {
                const bool blocked=!tracking.grip_valid[side] || !(tracking.raw_grip_hands&(1u<<side)) ||
                    !(tracking.tracked_hands&(1u<<side)) || !tracking.hand_empty[side] || ownership.arm[side]!=0 ||
                    !body::valid_pose(tracking.raw_grip[side]);
                if(blocked) assert(!arm_pose->raw_joints);
                else if(scenario!="native") assert(arm_pose->raw_joints);
                if(arm_pose->raw_joints && candidate_equal) assert(same_array(*arm_pose->raw_joints,arm_pose->joints));
                if(arm_pose->raw_joints && scenario=="reach") assert(!arm_pose->raw_hand_reached);
            }
        }
        if(arm_first && use_latch && frame==5) {
            auto changed_native=native_arm[0];
            changed_native[0].f[12]+=0.125f;
            const auto changed_upload=driver.arm_frozen(0,changed_native,current_frame,tracking.tick,frame_key);
            assert(changed_upload && saved_arms[0]);
            assert(same_array(changed_upload->joints,saved_arms[0]->joints));
            assert(changed_upload->raw_joints.has_value()==saved_arms[0]->raw_joints.has_value());
            if(changed_upload->raw_joints) assert(same_array(*changed_upload->raw_joints,*saved_arms[0]->raw_joints));
            const auto restored_upload=driver.arm_frozen(0,native_arm[0],current_frame,tracking.tick,frame_key);
            assert(restored_upload && same_array(restored_upload->joints,saved_arms[0]->joints));
        }
    }
};

void compare_pair(const std::string& scenario,bool arm_first,bool latched,bool candidate_equal) {
    Fixture baseline;
    Fixture candidate;
    bool observed_raw=false;
    bool observed_raw_reached=false;
    bool observed_raw_clamped=false;
    bool observed_candidate_difference=false;
    bool observed_rotated_target=false;
    double largest_translation_shortcut_error=0;
    for(unsigned frame=0;frame<100;++frame) {
        baseline.replay_frame(frame,scenario,false,candidate_equal,arm_first,latched,false);
        candidate.replay_frame(frame,scenario,true,candidate_equal,arm_first,latched,true);
        const auto base_diag=baseline.driver.diagnostic();
        const auto candidate_diag=candidate.driver.diagnostic();
        assert(baseline.driver.solves()==candidate.driver.solves());
        assert(baseline.driver.failures()==candidate.driver.failures());
        assert(base_diag.revision==candidate_diag.revision && base_diag.solved_revision==candidate_diag.solved_revision);
        assert(base_diag.tracking.tracking_revision==candidate_diag.tracking.tracking_revision);
        assert(base_diag.owned[0]==candidate_diag.owned[0] && base_diag.owned[1]==candidate_diag.owned[1]);
        const auto base_body=baseline.driver.body(baseline.native_body,baseline.model_frame,latched?frame+100:0);
        const auto candidate_body=candidate.driver.body(candidate.native_body,candidate.model_frame,latched?frame+100:0);
        assert(base_body && candidate_body && same_array(*base_body,*candidate_body));
        for(unsigned side=0;side<2;++side) {
            const auto base_arm=baseline.driver.arm_frozen(side,baseline.native_arm[side],baseline.model_frame,0,latched?frame+100:0);
            const auto candidate_arm=candidate.driver.arm_frozen(side,candidate.native_arm[side],candidate.model_frame,0,latched?frame+100:0);
            assert(base_arm && candidate_arm && same_array(base_arm->joints,candidate_arm->joints));
            if(candidate_arm->raw_joints) {
                observed_raw=true;
                if(candidate_equal) assert(same_array(*candidate_arm->raw_joints,candidate_arm->joints));
                observed_raw_reached=observed_raw_reached || candidate_arm->raw_hand_reached;
                observed_raw_clamped=observed_raw_clamped || !candidate_arm->raw_hand_reached;
                observed_candidate_difference=observed_candidate_difference || !same_array(*candidate_arm->raw_joints,candidate_arm->joints);
                const auto mapping=candidate_diag.grip_to_attachment[side];
                const auto expected_target=body::mul(candidate.tracking.raw_grip[side],mapping);
                const auto translation_only=candidate.tracking.raw_grip[side].p+mapping.p;
                observed_rotated_target=observed_rotated_target || length(expected_target.p-translation_only)>.001;
                if(scenario=="turn" && candidate_arm->raw_hand_reached) {
                    const auto model=frame_of(candidate.model_frame);
                    const auto grip_delta_world=candidate.tracking.grip[side].p-candidate.tracking.raw_grip[side].p;
                    const auto grip_delta_model=rotated(body::conj(model.rigid.q),grip_delta_world)*(1/model.scale);
                    for(std::size_t joint=0;joint<candidate_arm->joints.size();++joint) {
                        const auto shortcut_position=rig::position(candidate_arm->joints[joint])-grip_delta_model;
                        const auto raw_position=rig::position((*candidate_arm->raw_joints)[joint]);
                        largest_translation_shortcut_error=std::max(largest_translation_shortcut_error,length(shortcut_position-raw_position));
                    }
                }
            }
        }
    }
    if(scenario!="native" && scenario!="invalid") assert(observed_raw);
    if(candidate_equal) assert(observed_raw_reached);
    if(scenario=="reach") assert(observed_raw_clamped && observed_candidate_difference);
    if(!candidate_equal && scenario!="native" && scenario!="invalid") assert(observed_candidate_difference);
    if(scenario=="reach") assert(observed_rotated_target);
    if(scenario=="turn") assert(largest_translation_shortcut_error>.001);
}

void raw_equal_matches_actual_and_invalid_gates_fail_closed() {
    compare_pair("walk",true,true,true);
    compare_pair("invalid",false,false,false);
}

void distinct_candidate_isolation_and_cached_upload() {
    compare_pair("turn",true,true,false);
    compare_pair("crouch",false,false,false);
    compare_pair("reach",false,false,false);
    compare_pair("substeps",false,false,false);
    compare_pair("calibration",false,false,true);
}

void native_ownership_releases_candidate_only_after_blends() {
    Fixture baseline;
    Fixture candidate;
    bool saw_suppressed_native=false;
    bool saw_release=false;
    bool saw_hand_only_tail=false;
    for(unsigned frame=0;frame<180;++frame) {
        baseline.replay_frame(frame,"native",false,false,false,false,false);
        candidate.replay_frame(frame,"native",true,false,false,false,true);
        const auto baseline_diag=baseline.driver.diagnostic();
        const auto candidate_diag=candidate.driver.diagnostic();
        assert(baseline.driver.solves()==candidate.driver.solves());
        assert(baseline.driver.failures()==candidate.driver.failures());
        assert(baseline_diag.owned[2]==candidate_diag.owned[2] && baseline_diag.owned[3]==candidate_diag.owned[3]);
        const auto baseline_body=baseline.driver.body(baseline.native_body,baseline.model_frame);
        const auto candidate_body=candidate.driver.body(candidate.native_body,candidate.model_frame);
        assert(baseline_body && candidate_body && same_array(*baseline_body,*candidate_body));
        for(unsigned side=0;side<2;++side)
            assert(same_array(baseline.last_arms[side],candidate.last_arms[side]));
        if(frame>=32 && frame<60 && (!candidate.last_raw[0] || !candidate.last_raw[1])) saw_suppressed_native=true;
        if(frame>=150 && candidate.last_raw[0] && candidate.last_raw[1]) saw_release=true;
        if(frame>=60 && candidate.ownership.arm[1]==0 && candidate_diag.owned[3]==0 &&
           candidate.tracking.grip_valid[1] && (candidate.tracking.raw_grip_hands&2u) &&
           (candidate.tracking.tracked_hands&2u) && candidate.tracking.hand_empty[1] && !candidate.last_raw[1])
            saw_hand_only_tail=true;
    }
    assert(saw_suppressed_native && saw_release && saw_hand_only_tail);
}
}

int main() {
    raw_equal_matches_actual_and_invalid_gates_fail_closed();
    distinct_candidate_isolation_and_cached_upload();
    native_ownership_releases_candidate_only_after_blends();
    std::cout<<"PASS raw-hand candidate output isolation, eligibility, cache and transition replay\n";
}
