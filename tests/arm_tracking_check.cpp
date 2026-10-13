#include "../src/arm_tracking.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>

namespace r=d2vr::rig;
int main(int argc,char** argv) {
    if(argc!=1 && argc!=2) return 2;
    {
        r::ArmTracking tracking;r::ArmTrackingFrame frame{};frame.tick=10;frame.epoch=10;frame.hands=1;
        assert(tracking.snapshot().revision==0);
        const auto published=tracking.publish(frame);const auto frozen=tracking.snapshot();
        assert(published.revision==frozen.revision && published.tick==frame.tick && published.epoch==frame.epoch);
        tracking.publish(frame);assert(tracking.stamp().revision==frozen.revision+1); // Same millisecond still distinct.
        tracking.configure_calibration({10,1.6,.32,.22,1,{}});
        assert(tracking.snapshot().calibration && !frozen.calibration);
        const auto configured=tracking.stamp();
        tracking.configure_calibration({10,1.6,.32,.22,1,{}});
        assert(tracking.stamp().revision==configured.revision && tracking.stamp().mapping_revision==configured.mapping_revision);

        tracking.clear_calibration();const auto before=tracking.stamp().revision;
        tracking.clear_references();tracking.clear();assert(tracking.stamp().revision==before+2);
        assert(frozen.frame.hands==1 && frozen.frame.epoch==10 && !tracking.snapshot().frame.hands);
        bool rejected=false;frame.hands=4;
        try {tracking.publish(frame);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected && tracking.stamp().revision==before+2);
        std::atomic<bool> done{false};std::atomic<unsigned> samples{0};
        std::thread writer([&] {for(unsigned i=1;i<=2000;++i) {
            r::ArmTrackingFrame update{};update.tick=update.epoch=i;update.hands=1;
            const auto stamp=tracking.publish(update);
            assert(stamp.tick==i && stamp.epoch==i && stamp.hands==1);
        }done=true;});
        std::thread reader([&] {std::uint64_t last{};do {
            const auto snapshot=tracking.snapshot();assert(snapshot.revision>=last);last=snapshot.revision;
            assert(snapshot.frame.tick==snapshot.frame.epoch);++samples;
        }while(!done.load());});
        writer.join();reader.join();assert(samples>0);
    }
    std::ifstream fixture;
    if(argc==2) {fixture.open(argv[1],std::ios::binary);char header[8]{};fixture.read(header,8);assert(fixture && std::memcmp(header,"D2VRAF01",8)==0);}
    for(unsigned side=0;side<2;++side) {
        r::ArmBinding binding{};std::array<std::uint32_t,3> chain{};r::Upload native{};
        if(argc==2) {
            fixture.read(reinterpret_cast<char*>(binding.parents.data()),sizeof(binding.parents));
            fixture.read(reinterpret_cast<char*>(chain.data()),sizeof(chain));binding.chain={chain[0],chain[1],chain[2]};
            fixture.read(reinterpret_cast<char*>(native.joints.data()),sizeof(native.joints));
            fixture.read(reinterpret_cast<char*>(native.model_frame.data()),sizeof(native.model_frame));assert(fixture);
        } else {
            binding.parents.fill(4);binding.parents[0]=-1;binding.parents[1]=0;binding.parents[2]=1;binding.parents[3]=2;binding.parents[4]=3;
            binding.chain={2,3,4};native.model_frame={10,20,30,0,1,0,-1,0,0,0,0,1,1,1,1};
            for(auto& joint:native.joints) {joint.f[0]=joint.f[5]=joint.f[10]=1;joint.f[14]=-.5f;}
            native.joints[2].f[14]=0;native.joints[3].f[14]=-.3f;
            // Real finger chains off the wrist, so the finger override is an
            // observable change instead of a degenerate no-op.
            const std::array<std::array<unsigned,3>,5> digits{{{10,11,12},{13,14,15},{16,17,18},{19,20,21},{22,23,24}}};
            const std::array<std::array<float,3>,15> knuckles{{
                {.03f,.02f,-.51f},{.04f,.03f,-.53f},{.05f,.04f,-.56f},
                {.02f,-.005f,-.52f},{.02f,0,-.55f},{.02f,.005f,-.58f},
                {.005f,-.008f,-.525f},{.005f,-.005f,-.555f},{.005f,0,-.585f},
                {-.01f,-.008f,-.522f},{-.01f,-.005f,-.552f},{-.01f,0,-.582f},
                {-.025f,-.005f,-.518f},{-.025f,0,-.545f},{-.025f,.003f,-.57f}}};
            for(unsigned finger=0;finger<5;++finger) {
                binding.finger_chains[finger]=digits[finger];
                binding.parents[digits[finger][0]]=static_cast<std::int16_t>(binding.chain.wrist);
                binding.parents[digits[finger][1]]=static_cast<std::int16_t>(digits[finger][0]);
                binding.parents[digits[finger][2]]=static_cast<std::int16_t>(digits[finger][1]);
            }
            for(unsigned joint=0;joint<15;++joint) {
                native.joints[10+joint].f[12]=knuckles[joint][0];
                native.joints[10+joint].f[13]=knuckles[joint][1];
                native.joints[10+joint].f[14]=knuckles[joint][2];
            }
        }
        const auto original=native;
        binding.helpers={64,65,66,67};
        for(auto joint:binding.helpers) binding.parents[joint]=static_cast<std::int16_t>(binding.chain.elbow);
        r::ArmTracking tracking;r::ModelFrame model(native.model_frame);
        binding.bind_wrist=native.joints[binding.chain.wrist];
        binding.bind_grip=r::turn_root(binding.bind_wrist,{0,0,std::sin(.3),std::cos(.3)},r::position(binding.bind_wrist)+d2vr::Vec3{.02,.06,0});
        const auto publish_grip=[&](const r::Transform& grip,r::ArmTrackingFrame& frame) {
            frame.grips[side].position=model.world_point(r::position(grip));
            for(unsigned axis=0;axis<3;++axis) frame.grips[side].basis[axis]=model.world_direction(r::column(grip,axis));
        };
        const auto check=[&](const r::PosedArm& posed,const r::Transform& grip) {
            const auto desired=r::rigid_retarget(binding.bind_wrist,binding.bind_grip,grip);
            assert(d2vr::length(r::position(posed.joints[binding.chain.wrist])-r::position(desired))<1e-4);
            // Every non-hand joint is suppressed at the wrist, including forearm helpers.
            for(unsigned joint=0;joint<69;++joint) if(!r::in_subtree(binding.parents,joint,binding.chain.wrist)) {
                assert(d2vr::length(r::position(posed.joints[joint])-r::position(desired))<1e-4);
                assert(std::abs(d2vr::length(r::column(posed.joints[joint],0))-.02)<1e-4);
            }
        };
        r::ArmTrackingFrame frame{};frame.tick=100;frame.epoch=1;frame.hands=1u<<side;
        publish_grip(binding.bind_grip,frame);tracking.publish(frame);
        assert(tracking.pose(native,binding,side,100)); // Hands-only needs no anatomical fit or body anchor.
        r::ArmTracking::full_arms=true;
        assert(!tracking.pose(native,binding,side,100)); // Experimental arms still require the anchor.
        r::ArmTracking::full_arms=false;
        frame.has_head_anchor=true;frame.reference_epoch=10;
        frame.head_anchor.basis={d2vr::Vec3{1,0,0},d2vr::Vec3{0,1,0},d2vr::Vec3{0,0,1}};
        const auto shoulder=model.world_point(r::position(native.joints[binding.chain.upper]));
        frame.head_anchor.position=shoulder-frame.head_anchor.basis[1]*(side?-.18:.18)+d2vr::Vec3{0,0,.2};
        tracking.publish(frame);
        assert(!tracking.pose(native,binding,1-side,100));
        const auto rest=tracking.pose(native,binding,side,100);assert(rest);check(*rest,binding.bind_grip);
        const auto frozen_rest=r::ArmTracking::pose_frozen(native,binding,side,100,tracking.snapshot());
        assert(frozen_rest && !std::memcmp(rest->joints.data(),frozen_rest->joints.data(),sizeof(rest->joints)));
        if(argc==1) {
            const auto frozen=tracking.snapshot();
            const auto arms=r::ArmTracking::pose_frozen_with_arm_mode(native,binding,side,100,frozen,true);assert(arms);
            const auto anchor=r::ArmTracking::hand_shoulder(frozen.frame,frozen.calibration,side,
                frozen.has_chest?&frozen.chest_yaw:nullptr);
            assert(d2vr::length(model.world_point(r::position(arms->joints[binding.chain.upper]))-anchor)<1e-4);
            auto far=frozen;far.frame.grips[side].position.z+=3;
            assert(!r::ArmTracking::pose_frozen_with_arm_mode(native,binding,side,100,far,true));
        }
        {
            // Independent translated/rotated native uploads must recover the
            // same published world palm, including an unrelated old origin.
            auto frozen=tracking.snapshot();frozen.frame.has_model_origin=true;frozen.frame.model_origin={-77,34,2};
            auto upload=native;upload.model_frame={3,-4,9,1,0,0,0,1,0,0,0,1,1,1,1};
            const r::ModelFrame moved_model(upload.model_frame);
            const auto moved_pose=r::ArmTracking::pose_frozen(upload,binding,side,100,frozen);assert(moved_pose);
            const auto palm=r::rigid_retarget(binding.bind_grip,binding.bind_wrist,moved_pose->joints[binding.chain.wrist]);
            assert(d2vr::length(moved_model.world_point(r::position(palm))-frozen.frame.grips[side].position)<1e-4);
            for(unsigned axis=0;axis<3;++axis)
                assert(d2vr::length(moved_model.world_direction(r::column(palm,axis))-frozen.frame.grips[side].basis[axis])<1e-4);
        }
        const auto moved_grip=r::turn_root(binding.bind_grip,{0,0,std::sin(.3),std::cos(.3)},r::position(binding.bind_grip)+d2vr::Vec3{.03,0,0});
        frame.tick=101;publish_grip(moved_grip,frame);tracking.publish(frame);
        const auto moved=tracking.pose(native,binding,side,101);assert(moved);check(*moved,moved_grip);
        assert(!tracking.pose(native,binding,side,99));
        assert(!tracking.pose(native,binding,side,352));
        // World menus must continue publishing current controllers. A stale
        // gameplay publication falls back to native arms, while a fresh wheel
        // publication moves the palm and keeps hands-only suppression active.
        frame.tick=400;publish_grip(moved_grip,frame);tracking.publish(frame);
        auto wheel_grip=moved_grip;wheel_grip.f[13]+=.08f;
        frame.tick=700;publish_grip(wheel_grip,frame);tracking.publish(frame);
        const auto wheel=tracking.pose(native,binding,side,700);assert(wheel);check(*wheel,wheel_grip);
        assert(!std::memcmp(&native,&original,sizeof(native)));
        frame.tick=101;publish_grip(moved_grip,frame);tracking.publish(frame);
        auto extended=moved_grip;extended.f[12]+=.5f;
        publish_grip(extended,frame);frame.tick=401;tracking.publish(frame);
        const auto followed=tracking.pose(native,binding,side,401);assert(followed);check(*followed,extended);
        // A fit from another reference space falls back to default proportions.
        tracking.configure_calibration({11,1.6,.32,.22,1,{}});tracking.publish(frame);
        const auto fallback=tracking.pose(native,binding,side,401);assert(fallback);check(*fallback,extended);
        tracking.configure_calibration({10,1.6,.32,.22,1,{}});tracking.publish(frame);
        const auto calibrated=tracking.pose(native,binding,side,401);assert(calibrated);check(*calibrated,extended);
        assert(std::memcmp(&native,&original,sizeof(native))==0);
        tracking.clear();assert(!tracking.pose(native,binding,side,401));
        // Render publication and simultaneous animation callbacks must never
        // combine fields from different snapshots or mutate a native palette.
        std::atomic<bool> done{false};std::atomic<unsigned> accepted{0};
        std::thread publisher([&] {
            for(unsigned i=0;i<2000;++i) {frame.epoch=3+i%2;frame.tick=500;tracking.publish(frame);}
            done=true;
        });
        std::thread worker([&] {do {if(tracking.pose(native,binding,side,500)) ++accepted;} while(!done.load());});
        publisher.join();worker.join();assert(accepted>0 && std::memcmp(&native,&original,sizeof(native))==0);
        // Hand state: native_hands returns this side to the game animation, and
        // the empty-hand bit gates the finger override while the wrist follows.
        publish_grip(extended,frame);frame.epoch=9;frame.tick=600;frame.hands=1u<<side;frame.has_hand_state=false;
        frame.curls={};frame.index_curls={};frame.curls[side]=1;frame.index_curls[side]=1;frame.independent_index=true;
        frame.empty_hands=0;frame.native_hands=0;tracking.publish(frame);
        const auto override_allowed=tracking.pose(native,binding,side,600);assert(override_allowed);check(*override_allowed,extended);
        const auto native_fingers=[&](const r::PosedArm& posed) {
            for(unsigned joint=0;joint<69;++joint) {
                if(joint==binding.chain.wrist || !r::in_subtree(binding.parents,joint,binding.chain.wrist)) continue;
                const auto expected=r::rigid_retarget(native.joints[joint],native.joints[binding.chain.wrist],posed.joints[binding.chain.wrist]);
                if(d2vr::length(r::position(posed.joints[joint])-r::position(expected))>1e-6) return false;
            }
            return true;
        };
        // An empty hand of either side curls (only the zero-chain fixture cannot),
        // so this assertion cannot pass vacuously.
        assert(native_fingers(*override_allowed)==(argc==2));
        frame.has_hand_state=true;frame.empty_hands=1u<<side;tracking.publish(frame);
        const auto empty=tracking.pose(native,binding,side,600);assert(empty);check(*empty,extended);
        assert(!std::memcmp(override_allowed->joints.data(),empty->joints.data(),sizeof(empty->joints)));
        frame.empty_hands=0;tracking.publish(frame);
        const auto native_hand=tracking.pose(native,binding,side,600);assert(native_hand);check(*native_hand,extended);
        assert(native_fingers(*native_hand));
        frame.native_hands=1u<<side;tracking.publish(frame);
        assert(!tracking.pose(native,binding,side,600));
        frame.has_hand_state=false;tracking.publish(frame);
        assert(tracking.pose(native,binding,side,600)); // Legacy frames never read the hand-state bits.
        if(argc==1) {
            // Cached game fists require rigid palettes while blending. Hiding
            // arm joints before blending would reject the entire palette.
            const auto cached=r::capture_finger_grip(native.joints,binding.parents,binding.finger_chains);
            assert(cached.valid);
            {std::lock_guard<std::mutex> lock(r::ArmTracking::grip_mutex_);r::ArmTracking::native_grips_[side]=cached;}
            const auto fist=tracking.pose(native,binding,side,600);assert(fist);check(*fist,extended);
            {std::lock_guard<std::mutex> lock(r::ArmTracking::grip_mutex_);r::ArmTracking::native_grips_[side].reset();}
        }
    }
    std::cout<<"PASS: tracked publication on both "<<(argc==2?"captured native":"synthetic")<<" arm palettes, hand on grip, hidden forearm and upper arm, calibration fallback, concurrent callbacks, native-finger gating and untouched source\n";
}
