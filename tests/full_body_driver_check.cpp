#include "../src/full_body_driver.hpp"
#include "../src/arm_tracking.hpp"
#include <cassert>
#include <iostream>
#include <thread>
int main() {
    // Synthetic clock: the solver reads Tracking::seconds (real steady time in production).
    const auto stamp=[](d2vr::fullbody::Tracking x){x.seconds=x.tick/1000.;return x;};
    using namespace d2vr;using namespace d2vr::fullbody;
    const std::array<float,15> frame{4,5,6,0,1,0,-1,0,0,0,0,1,1.2f,1.2f,1.2f};
    const Xf pose{body::rot_z(.4),{.2,.3,.4}};
    const auto f=frame_of(frame);const auto recovered=to_model(f,to_world(f,pose));
    assert(length(recovered.p-pose.p)<1e-6);
    auto bad=frame;bad[13]=1;bool rejected=false;
    try {(void)frame_of(bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    Driver driver;
    // Use the same native bind palm landmarks as arm_tracking, then verify the
    // solved hand subtree recovers the controller grip in both mirrored arms.
    const auto b=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
    Tracking tracking;tracking.eye={b.bind[b.find("camera_jnt")].q,b.bind[b.find("camera_jnt")].p};tracking.epoch=1;
    std::array<Transform,20> native_body{};
    for(unsigned i=0;i<20;++i)native_body[i]=to_transform(b.bind[i]);
    std::array<Transform,69> native_arm[2];Transform palms[2];
    const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?right:left;const std::string prefix=side?"right":"left";
        for(unsigned i=0;i<69;++i)native_arm[side][i]=to_transform(a.bind[i]);
        std::array<Transform,4> knuckles;const char* fingers[]={"index","medium","ring","pinky"};
        for(unsigned i=0;i<4;++i)knuckles[i]=to_transform(a.bind[a.find(prefix+"_"+fingers[i]+"_02_jnt")]);
        palms[side]=rig::hand_bind_grip(native_arm[side][a.find(prefix+"_wrist_jnt")],knuckles);
        tracking.grip[side]=body::mul(b.bind[b.find("center_spine03_jnt")],to_xf(palms[side]));
        tracking.grip_valid[side]=true;
        (void)driver.arm(side,native_arm[side],identity);
    }
    driver.tune(0,1);driver.publish(stamp(tracking),{});assert(driver.body(native_body,identity));
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?right:left;const std::string prefix=side?"right":"left";
        const auto solved=driver.arm(side,native_arm[side],identity);assert(solved);
        const auto wrist=static_cast<unsigned>(a.find(prefix+"_wrist_jnt"));
        const auto palm=rig::rigid_retarget(palms[side],native_arm[side][wrist],(*solved)[wrist]);
        assert(length(rig::position(palm)-tracking.grip[side].p)<.005);
        // Compare against the production ArmTracking retarget path, using its
        // same named _02 MCP landmarks rather than a separate palm convention.
        rig::ArmBinding binding{};
        binding.chain={static_cast<unsigned>(a.find(prefix+"_arm_jnt")),static_cast<unsigned>(a.find(prefix+"_elbow_jnt")),wrist};
        for(unsigned i=0;i<69;++i)binding.parents[i]=static_cast<std::int16_t>(a.parent[i]);
        binding.bind_wrist=native_arm[side][wrist];binding.bind_grip=palms[side];
        rig::ArmTrackingSnapshot frozen;frozen.frame.tick=100;frozen.frame.epoch=1;frozen.frame.hands=3;
        frozen.frame.has_hand_state=true; // Preserve the supplied native fingers.
        auto& grip=frozen.frame.grips[side];grip.position=tracking.grip[side].p;
        grip.basis={rotated(tracking.grip[side].q,{1,0,0}),rotated(tracking.grip[side].q,{0,1,0}),rotated(tracking.grip[side].q,{0,0,1})};
        const bool arms=rig::ArmTracking::full_arms.exchange(false),fists=rig::ArmTracking::game_grip_fists.exchange(false);
        const auto legacy=rig::ArmTracking::pose_frozen({native_arm[side],identity,0},binding,side,100,frozen);
        rig::ArmTracking::full_arms=arms;rig::ArmTracking::game_grip_fists=fists;
        assert(legacy);
        const auto native_palm=rig::rigid_retarget(binding.bind_grip,binding.bind_wrist,legacy->joints[wrist]);
        assert(length(rig::position(palm)-rig::position(native_palm))<.005);
        for(unsigned axis=0;axis<3;++axis)assert(length(rig::column(palm,axis)-rig::column(native_palm,axis))<.01);
    }
    // Arm-first callbacks solve the newest publication from the retained native body.
    tracking.tick=1000;driver.publish(stamp(tracking),{});
    assert(driver.arm(0,native_arm[0],identity));
    assert(driver.diagnostic().arm_gate[0]==ArmGate::ready);
    assert(driver.diagnostic().solved_revision==driver.diagnostic().revision);
    assert(driver.body(native_body,identity));assert(driver.arm(0,native_arm[0],identity));
    // One engine frame, one solution: a publication landing between uploads of the same frame is ignored until the next frame.
    {tracking.tick+=5;driver.publish(stamp(tracking),{});const auto before=driver.solves();
     assert(driver.body(native_body,identity,77));assert(driver.solves()==before+1);
     tracking.tick+=5;tracking.eye.p.x+=.05;driver.publish(stamp(tracking),{});
     assert(driver.arm_frozen(0,native_arm[0],identity,0,77));assert(driver.arm_frozen(1,native_arm[1],identity,0,77));
     assert(driver.solves()==before+1);assert(driver.diagnostic().arm_gate[1]==ArmGate::ready);
     assert(driver.body(native_body,identity,78));assert(driver.solves()==before+2);}
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    assert(!driver.arm(0,native_arm[0],identity));
    assert(driver.diagnostic().arm_gate[0]==ArmGate::stale_age);
    assert(!driver.body(native_body,identity));
    tracking.tick+=260;driver.publish(stamp(tracking),{});assert(driver.body(native_body,identity));
    assert(driver.arm(0,native_arm[0],identity));
    // Native bind geometry must meet the measured shoulder landmarks while
    // retaining the camera and the complete native coordinate conventions.
    body::BodySolver fitted(b,left,right,{tracking.eye.p.z,0,.3324,.148397});
    const auto& fb=fitted.skeleton(0);const auto chest=fb.bind[fb.find("center_spine03_jnt")];
    const auto ls=body::mul(chest,fitted.skeleton(1).bind[left.find("left_arm_jnt")]).p;
    const auto rs=body::mul(chest,fitted.skeleton(2).bind[right.find("right_arm_jnt")]).p;
    assert(std::abs(std::abs(ls.y-rs.y)-.3324)<1e-6);
    assert(std::abs(fb.bind[fb.find("camera_jnt")].p.z-(ls.z+rs.z)*.5-.148397)<1e-6);
    assert(length(fb.bind[fb.find("camera_jnt")].p-b.bind[b.find("camera_jnt")].p)<1e-6);
    for(unsigned hz:{90u,30u,10u,5u}) {
        Driver walk;walk.tune(0,1);auto t=tracking;t.tick=1000;
        for(unsigned side=0;side<2;++side) (void)walk.arm(side,native_arm[side],identity);
        walk.publish(stamp(t),{});assert(walk.body(native_body,identity));
        double consumed=0,max_lag=0;
        for(unsigned i=1;i<=hz*3;++i) {
            const double x=1.4*i/hz;t=tracking;t.eye.p.x+=x;
            for(auto& grip:t.grip) grip.p.x+=x;
            t.tick=1000+static_cast<std::uint64_t>(std::llround(1000.*i/hz));
            walk.publish(stamp(t),{});assert(walk.body(native_body,identity));
            const auto d=walk.diagnostic();consumed+=d.consumed_dt;
            assert(d.substep_dt<=1/30.+1e-9 && d.substeps<=15 && !d.gap_reset);
            max_lag=std::max(max_lag,std::abs(d.pelvis.p.x-(d.feet[0].p.x+d.feet[1].p.x)*.5));
        }
        assert(std::abs(consumed-3)<1e-6);assert(max_lag<.8);
        t.tick+=5000;t.eye.p.x+=10;walk.publish(stamp(t),{});assert(walk.body(native_body,identity));
        assert(walk.diagnostic().gap_reset && walk.diagnostic().substeps==1);
        t.epoch++;t.tick+=10000;walk.publish(stamp(t),{});assert(walk.body(native_body,identity));
        assert(walk.diagnostic().wall_dt<.02);
    }
    // Actual native cadence around archived JSONL lines 142-178 includes fresh
    // revisions with equal ms ticks (lines 163 and 178). The archive samples only
    // some uploads; replay its consumed intervals with actual arm-first ordering.
    const unsigned captured_ms[]={15,15,16,16,15,16,16,0,16,15,16,16,0};
    Driver ordered;ordered.tune(0,1);auto replay=tracking;replay.tick=2000;
    replay.pose_epoch=4;replay.reference_epoch=7;replay.mapping_revision=9;
    for(unsigned side=0;side<2;++side)(void)ordered.arm(side,native_arm[side],identity);
    ordered.publish(stamp(replay),{});assert(ordered.body(native_body,identity));
    double consumed=0,elapsed=0;unsigned advancing_swings=0;
    for(unsigned repeat=0;repeat<12;++repeat)for(unsigned ms:captured_ms) {
        const auto before=ordered.diagnostic();
        replay.tick+=ms;elapsed+=ms/1000.;replay.eye.p.x=tracking.eye.p.x+elapsed*1.4;
        for(unsigned side=0;side<2;++side)replay.grip[side].p.x=tracking.grip[side].p.x+elapsed*1.4;
        // Equal ms publications can still carry newer hand poses.
        if(!ms)replay.grip[0].p.z+=.001;
        ++replay.tracking_revision;ordered.publish(stamp(replay),{});const auto solves=ordered.solves();
        // Independently moving model frames must recover the same world palms.
        for(unsigned side=0;side<2;++side) {
            auto moving=frame;moving[0]+=static_cast<float>(elapsed*.3+side*.2);
            moving[1]+=static_cast<float>(elapsed*.1-side*.1);
            const auto posed=ordered.arm_frozen(side,native_arm[side],moving);assert(posed);
            assert(posed->tracking.tick==replay.tick && posed->tracking.epoch==replay.epoch);
            assert(posed->tracking.tracking_revision==replay.tracking_revision && posed->tracking.pose_epoch==4 &&
                   posed->tracking.reference_epoch==7 && posed->tracking.mapping_revision==9);
            assert(posed->owned==ordered.diagnostic().owned[2+side]);
            assert(posed->revision==ordered.diagnostic().revision);
            assert(ordered.diagnostic().arm_gate[side]==ArmGate::ready);
            assert(ordered.diagnostic().palm_residual[side]<.005);
            assert(ordered.solves()==solves+1);
        }
        assert(ordered.body(native_body,identity));assert(ordered.solves()==solves+1);
        const auto after=ordered.diagnostic();consumed+=after.consumed_dt;
        assert(!after.gap_reset && std::abs(after.consumed_dt-ms/1000.)<1e-9);
        if(!ms)for(unsigned side=0;side<2;++side) {
            assert(after.phase[side]==before.phase[side]);
            assert(length(after.plant[side]-before.plant[side])<1e-12);
            assert(length(after.swing_from[side]-before.swing_from[side])<1e-12);
            assert(length(after.swing_to[side]-before.swing_to[side])<1e-12);
        }
        else for(unsigned side=0;side<2;++side)if(after.phase[side]>before.phase[side])++advancing_swings;
    }
    assert(std::abs(consumed-elapsed)<1e-9 && advancing_swings>20);
    // Duplicate ticks preserve state; genuine backwards and reference changes reset it.
    replay.tick-=100;ordered.publish(stamp(replay),{});assert(ordered.arm(0,native_arm[0],identity));
    assert(ordered.diagnostic().gap_reset);
    replay.epoch++;ordered.publish(stamp(replay),{});assert(ordered.arm(0,native_arm[0],identity));
    assert(ordered.diagnostic().wall_dt<.02 && !ordered.diagnostic().gap_reset);
    // Exact callback/source monotonic clock, independently of publication wall age.
    auto t=tracking;t.tick=3000;ordered.publish(stamp(t),{});const auto before_age=ordered.solves();
    assert(!ordered.arm_frozen(0,native_arm[0],identity,2999));
    assert(!ordered.arm_frozen(0,native_arm[0],identity,3251));
    assert(ordered.solves()==before_age && ordered.diagnostic().arm_gate[0]==ArmGate::stale_age);
    assert(ordered.arm_frozen(0,native_arm[0],identity,3250));
    Driver invalid_clock;auto invalid_tracking=t;invalid_tracking.tick=0;invalid_clock.publish(stamp(invalid_tracking),{});
    assert(!invalid_clock.arm_frozen(0,native_arm[0],identity,3000) && invalid_clock.solves()==0);
    auto xr_age=t;xr_age.source_tick=2900;ordered.publish(xr_age,{});
    assert(!ordered.arm_frozen(0,native_arm[0],identity,3151));
    assert(ordered.arm_frozen(0,native_arm[0],identity,3150));
    xr_age.source_tick=3100;ordered.publish(xr_age,{});
    assert(!ordered.arm_frozen(0,native_arm[0],identity,3000));
    // Native ownership can blend the rendered legs on duplicate publications,
    // but may not migrate planted/swing targets or advance ownership weights.
    for(unsigned i=0;i<30;++i) {t.tick+=33;ordered.publish(stamp(t),body::native_ownership("Mantle"));assert(ordered.body(native_body,identity));}
    const auto native_before=ordered.diagnostic();assert(native_before.owned[0]>0);
    t.eye.p.x+=.1;ordered.publish(stamp(t),body::native_ownership("Mantle"));assert(ordered.arm(0,native_arm[0],identity));
    const auto native_after=ordered.diagnostic();assert(native_after.consumed_dt==0 && !native_after.gap_reset);
    for(unsigned side=0;side<2;++side) {
        assert(native_after.phase[side]==native_before.phase[side]);
        assert(length(native_after.plant[side]-native_before.plant[side])<1e-12);
        assert(length(native_after.swing_from[side]-native_before.swing_from[side])<1e-12);
        assert(length(native_after.swing_to[side]-native_before.swing_to[side])<1e-12);
    }
    for(unsigned k=0;k<4;++k)assert(native_after.owned[k]==native_before.owned[k]);
    // Empty hand curls from the controller grip; a held hand keeps the native (flat) fingers.
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?right:left;const std::string prefix=side?"right":"left";
        const auto wrist=static_cast<unsigned>(a.find(prefix+"_wrist_jnt")),index=static_cast<unsigned>(a.find(prefix+"_index_03_jnt"));
        const auto local=[&](bool empty) {
            Driver d;d.tune(0,1);auto tr=tracking;tr.tick=5000;tr.grip_value[side]=tr.trigger[side]=1;tr.hand_empty[side]=empty;
            for(unsigned k=0;k<2;++k)(void)d.arm(k,native_arm[k],identity);
            d.publish(stamp(tr),{});assert(d.body(native_body,identity));
            const auto r=d.arm(side,native_arm[side],identity);assert(r);
            return body::mul(body::inverse(to_xf((*r)[wrist])),to_xf((*r)[index]));
        };
        const auto empty=local(true),held=local(false);
        const auto flat=body::mul(body::inverse(a.bind[wrist]),a.bind[index]);
        assert(length(held.p-flat.p)<1e-3);assert(length(empty.p-flat.p)>.003);
    }
    {   // Stick locomotion at 5 m/s, engine order: a frame's animation uploads run before that frame's
        // render-thread publication, so uploads of frame n use the solution published at n-1. The
        // solved pose is carried by the player-root motion since that publication: the wrist stays
        // glued to the moving player (constant offset to the eye, no trailing or ghosting).
        Driver d;d.tune(0,1);const double speed=5,dt=1/90.;
        for(unsigned k=0;k<2;++k)(void)d.arm(k,native_arm[k],identity);
        double min_rel[2]{1e9,1e9},max_rel[2]{-1e9,-1e9};
        for(unsigned n=0;n<240;++n) {
            const double x=n<60?0:speed*(n-60)*dt;
            const std::array<float,15> moving{static_cast<float>(x),0,0,1,0,0,0,1,0,0,0,1,1,1,1};
            if(n) {
                assert(d.body(native_body,moving));
                for(unsigned side=0;side<2;++side) {
                    const auto r=d.arm(side,native_arm[side],moving);assert(r);
                    const auto& a=side?right:left;
                    const auto w=to_world(frame_of(moving),to_xf((*r)[a.find(side?"right_wrist_jnt":"left_wrist_jnt")]));
                    if(n<80) continue;
                    const double rel=w.p.x-(tracking.eye.p.x+x);
                    min_rel[side]=std::min(min_rel[side],rel);max_rel[side]=std::max(max_rel[side],rel);
                }
            }
            auto tr=tracking;tr.eye.p.x+=x;tr.tick=1000+n*11;
            for(unsigned k=0;k<2;++k) tr.grip[k].p.x+=x;
            d.publish(stamp(tr),{});
        }
        for(unsigned side=0;side<2;++side) assert(max_rel[side]-min_rel[side]<.01);
    }
    AvatarCalibrationResult fit{7,1.6,.36,.22,1.08,{}};
    driver.configure_calibration(fit);assert(driver.fit_epoch()==7);
    assert(std::abs(driver.diagnostic().configured_palm_span-fit.palm_span_metres)<.002);
    driver.configure_calibration(fit);assert(driver.fit_epoch()==7);
    fit.eye_to_shoulder_metres=0;rejected=false;
    try {driver.configure_calibration(fit);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    assert(driver.fit_epoch()==7);
    // Replay accepted v220 dimensions, without fabricating a floor height.
    fit={1,1.46747*1.06811,.311208*1.06811,.148397,1.06811,{}};
    driver.configure_calibration(fit);
    assert(std::abs(driver.diagnostic().configured_palm_span-fit.palm_span_metres)<.002);
    assert(!driver.diagnostic().fit->standing_eye_height_metres);
    driver.clear_calibration();assert(driver.fit_epoch()==0);
    std::cout<<"PASS: full-body verified palm mapping construction, accepted fit validation and native model/world roundtrip\n";
}
