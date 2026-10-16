#include "../src/grip_session.hpp"
#include "../src/full_body_driver.hpp"
#include <cassert>
#include <iostream>

using namespace d2vr;
using namespace d2vr::fullbody;
namespace {
constexpr std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
WorldTrackedPose world_pose(const Xf& pose) {
    return {pose.p,{rotated(pose.q,{1,0,0}),rotated(pose.q,{0,1,0}),rotated(pose.q,{0,0,1})}};
}
Xf rigid(const WorldTrackedPose& pose) {return from_basis(pose.position,pose.basis[0],pose.basis[1],pose.basis[2]);}
std::array<float,15> model_frame(const Xf& pose) {
    const auto p=world_pose(pose);
    return {static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z),
        static_cast<float>(p.basis[0].x),static_cast<float>(p.basis[0].y),static_cast<float>(p.basis[0].z),
        static_cast<float>(p.basis[1].x),static_cast<float>(p.basis[1].y),static_cast<float>(p.basis[1].z),
        static_cast<float>(p.basis[2].x),static_cast<float>(p.basis[2].y),static_cast<float>(p.basis[2].z),1,1,1};
}
void same_pose(const Xf& actual,const Xf& expected) {
    assert(length(actual.p-expected.p)<.005);
    for(const auto axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
        assert(length(rotated(actual.q,axis)-rotated(expected.q,axis))<.01);
}
// Fixture authority only: no claim that synthetic metadata establishes a native shape lease.
struct FixtureContact final : GripContactLease {
    GripAnchor anchor;
    explicit FixtureContact(GripAnchor value):anchor(value) {}
    bool current(std::uint64_t receipt,const GripAnchor& candidate) const noexcept override {return receipt==17 && anchor.same(candidate);}
};
struct Rig {
    body::Skeleton body=skeleton(data::kBody);
    body::Skeleton arms[2]{skeleton(data::kLeftArm),skeleton(data::kRightArm)};
    std::array<Transform,20> native_body{};
    std::array<Transform,69> native_arm[2]{};
    Transform bind_palm[2]{};
    unsigned wrist[2]{};
    Tracking tracking{};
    Rig() {
        tracking.eye=body.bind[body.find("camera_jnt")];
        tracking.epoch=1;tracking.pose_epoch=2;tracking.reference_epoch=3;tracking.mapping_revision=4;
        tracking.tick=100;tracking.source_tick=100;tracking.seconds=.1;tracking.tracked_hands=3;tracking.raw_grip_hands=3;
        for(unsigned j=0;j<20;++j)native_body[j]=to_transform(body.bind[j]);
        for(unsigned side=0;side<2;++side) {
            const auto& arm=arms[side];const std::string prefix=side?"right":"left";
            for(unsigned j=0;j<69;++j)native_arm[side][j]=to_transform(arm.bind[j]);
            wrist[side]=static_cast<unsigned>(arm.find(prefix+"_wrist_jnt"));
            std::array<Transform,4> knuckles{};const char* fingers[]{"index","medium","ring","pinky"};
            for(unsigned f=0;f<4;++f)knuckles[f]=native_arm[side][arm.find(prefix+"_"+fingers[f]+"_02_jnt")];
            bind_palm[side]=rig::hand_bind_grip(native_arm[side][wrist[side]],knuckles);
            tracking.grip[side]=body::mul(body.bind[body.find("center_spine03_jnt")],to_xf(bind_palm[side]));
            // Put the test hand comfortably inside the actual rig's reachable workspace.
            tracking.grip[side].p.y+=side?.12:-.12;
            tracking.grip[side].p.x+=.08;
            tracking.raw_grip[side]=tracking.grip[side];
            tracking.grip_valid[side]=true;tracking.hand_empty[side]=true;
        }
    }
    void seed(Driver& driver) const {
        driver.tune(0,1);driver.note_root(identity_frame);
        for(unsigned side=0;side<2;++side)(void)driver.arm(side,native_arm[side],identity_frame);
        driver.publish(tracking,{});assert(driver.body(native_body,identity_frame,1));
    }
    Xf palm(const std::array<Transform,69>& palette,const std::array<float,15>& frame,unsigned side=0) const {
        const auto local=rig::rigid_retarget(bind_palm[side],native_arm[side][wrist[side]],palette[wrist[side]]);
        return to_world(frame_of(frame),to_xf(local));
    }
};
struct Hold {
    GripBodyId id{1,2,3,4,5,6,7};
    GripEpochs epochs{1,2,3,4};
    WorldTrackedPose initial_body;
    GripAnchor anchor;
    std::shared_ptr<FixtureContact> contact;
    GripBodyMotion motion;
    explicit Hold(const WorldTrackedPose& hand):
        initial_body(grip_compose(hand,world_pose(Xf{body::rot_z(.13),{-.03,.015,0}}))),
        anchor(*GripAnchor::capture(id,initial_body,hand,hand.position+hand.basis[0]*.02,hand.basis[0])),
        contact(std::make_shared<FixtureContact>(anchor)),
        motion{id,initial_body,initial_body.position,{},{},1,{1,1,1}} {}
    bool admit(GripSession& session) const {return session.admit(1,epochs,anchor,{contact,17},motion,100,100,100);}
    GripSession::Snapshot snapshot(std::uint64_t tick) const {return {1,epochs,anchor,motion,tick,tick};}
};

fullbody::ArmPose solve(const Rig& rig,Driver& driver,const GripSession::Snapshot& s,const WorldTrackedPose& raw,
        const Xf& world_from_solve,std::uint64_t frame_key,bool compensate=true,unsigned side=0) {
    const auto desired=s.anchor.hand_target(s.body.id,s.body.pose);assert(desired);
    auto t=rig.tracking;t.tick=s.sample_tick;t.source_tick=s.source_tick;t.seconds=t.tick/1000.;++t.tracking_revision;
    const auto solve_from_world=body::inverse(world_from_solve);
    t.grip[side]=compensate?body::mul(solve_from_world,rigid(*desired)):rigid(*desired);
    t.raw_grip[side]=body::mul(solve_from_world,rigid(raw));
    driver.note_root(identity_frame);driver.publish(t,{});
    driver.note_root(model_frame(world_from_solve));
    const auto pose=driver.arm_frozen(side,rig.native_arm[side],model_frame(world_from_solve),t.tick,frame_key);assert(pose);
    assert(pose->root_mapping_valid);same_pose(pose->world_from_solve,world_from_solve);
    return *pose;
}
// This owned input stays inside its live Admission callback. It grants no right
// to publish a result: the same admission must accept the final short commit.
GripSession::Snapshot inputs(GripSession::Admission& admission,std::uint64_t now) {
    std::optional<GripSession::Snapshot> value;
    assert(admission.commit(now,[&](const GripSession::Snapshot& current){value=current;}));
    return *value;
}
std::array<double,3> right_hand_check() {
    Rig rig;Hold hold(world_pose(rig.tracking.grip[1]));GripSession session;assert(hold.admit(session));
    auto raw=world_pose(rig.tracking.grip[1]);raw.position.x+=.09;
    std::array<double,3> errors{};unsigned commits{};
    for(unsigned phase=0;phase<2;++phase) {
        const std::uint64_t tick=100+phase*10;
        if(phase) {
            hold.motion.pose=grip_compose(world_pose(Xf{body::rot_z(-.2),{}}),hold.initial_body);
            hold.motion.pose.position=hold.initial_body.position+Vec3{.04,-.01,.02};
            hold.motion.centre_of_mass=hold.motion.pose.position;
            assert(session.publish(hold.snapshot(tick),tick));
        }
        assert(session.consume(1,tick,[&](GripSession::Admission& admission) {
            const auto s=inputs(admission,tick);
            Driver driver;rig.seed(driver);const auto posed=solve(rig,driver,s,raw,{},20+phase,true,1);
            const auto desired=s.anchor.hand_target(s.body.id,s.body.pose);assert(desired);
            const auto visible=rig.palm(posed.joints,identity_frame,1);
            assert(posed.raw_joints && posed.raw_hand_reached);
            const auto raw_palm=rig.palm(*posed.raw_joints,identity_frame,1);
            assert(admission.commit(tick,[&](const GripSession::Snapshot&) {
                same_pose(visible,rigid(*desired));same_pose(raw_palm,rigid(raw));
                errors[phase]=length(visible.p-desired->position);
                errors[2]=std::max(errors[2],length(raw_palm.p-raw.position));
                assert(length(s.anchor.hand_contact(world_pose(visible))-s.anchor.body_contact(s.body.pose))<.005);
                if(!phase)assert(length(visible.p-raw.position)>.07);
                else assert(length(visible.p-rig.tracking.grip[1].p)>.03);
                ++commits;
            }));
            if(phase) {
                assert(session.revoke()==GripSession::Drain::pending);
                assert(!admission.commit(tick,[](const auto&){assert(false);}));
            }
        }));
    }
    assert(commits==2 && session.revoke()==GripSession::Drain::drained);return errors;
}
}
int main() {
    Rig rig;Hold hold(world_pose(rig.tracking.grip[0]));GripSession session;assert(hold.admit(session));
    unsigned commits{};double blocked_error{},raw_error{},uncompensated_root_error{},corrected_root_error{};
    std::optional<std::array<Transform,69>> skin_palette,attachment_palette;
    const auto publish_owned=[&](const fullbody::ArmPose& pose) {
        // Stand-in for the final two owned consumers, fed by exactly one palette.
        skin_palette=pose.joints;attachment_palette=pose.joints;++commits;
    };
    // A wall blocks the body while the physical controller continues forward.
    auto raw=world_pose(rig.tracking.grip[0]);raw.position.x+=.09;
    assert(session.consume(1,100,[&](GripSession::Admission& admission) {
        const auto s=inputs(admission,100);
        const auto motor_goal=s.anchor.body_target(raw);assert(motor_goal);
        assert(length(motor_goal->position-s.body.pose.position)>.08);
        Driver driver;rig.seed(driver);const auto posed=solve(rig,driver,s,raw,{},2);
        assert(admission.commit(100,[&](const GripSession::Snapshot&) {
            const auto visible=rig.palm(posed.joints,identity_frame);
            const auto desired=s.anchor.hand_target(s.body.id,s.body.pose);assert(desired);
            blocked_error=length(visible.p-desired->position);same_pose(visible,rigid(*desired));
            assert(length(visible.p-raw.position)>.07);
            // Keep real raw tracking enabled: the diagnostic candidate must not replace visible IK.
            assert(posed.raw_joints && posed.raw_hand_reached);
            const auto raw_palm=rig.palm(*posed.raw_joints,identity_frame);
            raw_error=length(raw_palm.p-raw.position);same_pose(raw_palm,rigid(raw));publish_owned(posed);
        }));
    }));
    // Exactly the retained local anchor follows translation AND rotation of the actual body.
    auto rotation=world_pose(Xf{body::rot_z(.2),{}});
    hold.motion.pose=grip_compose(rotation,hold.initial_body);
    // Rotate about the contact region rather than moving the hand outside rig reach.
    hold.motion.pose.position=hold.initial_body.position+Vec3{.04,.01,.02};
    hold.motion.centre_of_mass=hold.motion.pose.position;
    assert(session.publish(hold.snapshot(110),110));
    assert(session.consume(1,110,[&](GripSession::Admission& admission) {
        const auto s=inputs(admission,110);
        Driver driver;rig.seed(driver);const auto posed=solve(rig,driver,s,raw,{},3);
        assert(admission.commit(110,[&](const GripSession::Snapshot&) {
            const auto visible=rig.palm(posed.joints,identity_frame);
            const auto desired=s.anchor.hand_target(s.body.id,s.body.pose);assert(desired);
            same_pose(visible,rigid(*desired));
            assert(length(visible.p-rig.tracking.grip[0].p)>.03);
            assert(length(s.anchor.hand_contact(world_pose(visible))-s.anchor.body_contact(s.body.pose))<.005);publish_owned(posed);
        }));
    }));
    // The Driver carries ordinary solved poses with player-root movement. A held
    // world body needs inverse(world_from_solve) before IK, including orientation.
    const Xf delta{body::rot_z(.08),{.08,.03,0}};
    assert(session.consume(1,110,[&](GripSession::Admission& admission) {
        const auto s=inputs(admission,110);
        const auto desired=s.anchor.hand_target(s.body.id,s.body.pose);assert(desired);
        Driver wrong;rig.seed(wrong);const auto uncorrected=solve(rig,wrong,s,raw,delta,4,false);
        const auto wrong_palm=rig.palm(uncorrected.joints,model_frame(delta));
        uncompensated_root_error=length(wrong_palm.p-desired->position);assert(uncompensated_root_error>.015);
        Driver corrected;rig.seed(corrected);const auto posed=solve(rig,corrected,s,raw,delta,5);
        const auto visible=rig.palm(posed.joints,model_frame(delta));
        assert(admission.commit(110,[&](const GripSession::Snapshot&) {
            corrected_root_error=length(visible.p-desired->position);same_pose(visible,rigid(*desired));publish_owned(posed);
        }));
    }));
    // Age expiration rejects the final commit, even when admission began fresh.
    assert(session.consume(1,110,[&](GripSession::Admission& a) {
        const auto s=inputs(a,110);Driver driver;rig.seed(driver);const auto posed=solve(rig,driver,s,raw,{},6);
        assert(!a.commit(143,[&](const auto&){publish_owned(posed);assert(false);}));
    }));
    // A solved/latching engine frame can survive publication or release. Its
    // admission must not survive to the final shared skin/attachment publication.
    assert(session.consume(1,110,[&](GripSession::Admission& frozen) {
        const auto s=inputs(frozen,110);
        Driver driver;rig.seed(driver);const auto old=solve(rig,driver,s,raw,{},77);
        assert(session.publish(hold.snapshot(111),111));
        auto newer=rig.tracking;newer.tick=111;newer.source_tick=111;newer.seconds=.111;
        driver.publish(newer,{});
        const auto retained=driver.arm_frozen(0,rig.native_arm[0],identity_frame,111,77);assert(retained);
        assert(retained->revision==old.revision && retained->tracking.tick==110);
        assert(!frozen.commit(111,[&](const auto&){++commits;assert(false);}));
    }));
    assert(session.consume(1,111,[&](GripSession::Admission& a) {
        const auto s=inputs(a,111);Driver driver;rig.seed(driver);const auto posed=solve(rig,driver,s,raw,{},78);
        assert(session.revoke()==GripSession::Drain::pending);
        assert(!a.commit(111,[&](const auto&){publish_owned(posed);assert(false);}));
    }));
    assert(session.revoke()==GripSession::Drain::drained);
    assert(!session.consume(1,111,[](auto&){assert(false);}));assert(commits==3);
    assert(skin_palette && attachment_palette);
    for(unsigned joint=0;joint<69;++joint)for(unsigned component=0;component<16;++component)
        assert((*skin_palette)[joint].f[component]==(*attachment_palette)[joint].f[component]);
    const auto right=right_hand_check();
    std::cout<<"PASS composed grip/session/native-rig IK: blocked="<<blocked_error<<" raw="<<raw_error
        <<" root_without_compensation="<<uncompensated_root_error<<" root_compensated="<<corrected_root_error
        <<" right_blocked="<<right[0]<<" right_moved_rotated="<<right[1]<<" right_raw="<<right[2]
        <<"; moved/rotated anchor, raw-candidate separation, frozen/stale/released final commits rejected\n";
}
