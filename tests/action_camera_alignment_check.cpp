#include "../src/action_camera_alignment.hpp"
#include "../src/anchor_smoother.hpp"
#include "../src/authored_view.hpp"
#include "../src/audio_listener_pose.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
static GameCameraPose camera(double yaw=0) {
    GameCameraPose p{};p.position={1,2,3};
    p.basis={float(std::cos(yaw)),float(std::sin(yaw)),0,float(-std::sin(yaw)),float(std::cos(yaw)),0,0,0,1};return p;
}
int main() {
    ActionCameraKey key{1,2,3,4,5,false};
    ActionCameraContext context;
    assert(!context.sample(10,key,false).accepted); // no orphaned second-eye snapshot
    auto step=context.sample(10,key,true);assert(step.accepted && step.advance && step.changed);
    auto second=context.sample(10,key,false);assert(second.accepted && !second.advance && !second.changed);
    auto duplicate=context.sample(10,key,true);assert(duplicate.accepted && !duplicate.advance);
    auto changed=key;++changed.camera;
    assert(!context.sample(10,changed,false).accepted);
    assert(!context.sample(10,changed,true).accepted); // repeated first cannot split the frame either
    assert(context.sample(10,key,false).accepted); // the original frozen context stays intact
    auto next=context.sample(11,changed,true);assert(next.advance && next.changed && next.history_epoch!=step.history_epoch);
    // Each native owner component, XR reference/pose, and action handoff re-enters heading
    // and discards inherited height lag even with a native change below the teleport/snap limits.
    for(int field=0;field<6;++field) {
        ActionCameraContext c;AuthoredHeading h;AnchorSmoother smooth;
        auto k=key;k.authored=true;
        auto a=c.sample(1,k,true);h.update(camera(),true,a.history_epoch);smooth.update({0,0,1},.011,4);
        smooth.update({0,0,1.1},.011,4); // nonzero old lag
        if(field==0) ++k.player;
        if(field==1) ++k.camera;
        if(field==2) ++k.generation;
        if(field==3) ++k.pose_epoch;
        if(field==4) ++k.reference_epoch;
        if(field==5) k.authored=false;
        a=c.sample(2,k,true);assert(a.changed);if(a.changed) smooth.reset();
        assert(smooth.update({0,0,1.2},.011,4).z==1.2);
        auto yaw=h.update(camera(.2),k.authored,a.history_epoch);
        if(k.authored) assert(yaw && std::abs(*yaw-.2)<1e-6);else assert(!yaw);
        if(field==5) {k.authored=true;a=c.sample(3,k,true);assert(a.changed);
            yaw=h.update(camera(.3),true,a.history_epoch);assert(yaw && std::abs(*yaw-.3)<1e-6);}
    }
    // Unchanged in-mode inputs are bit-identical to baseline; two eyes sample once.
    for(bool authored: {false,true}) {
        ActionCameraContext c;AnchorSmoother baseline,aligned;auto k=key;k.authored=authored;
        double cached=0;
        for(std::uint64_t frame=1;frame<500;++frame) {
            const double raw=1.6+.2*std::floor(double(frame)/32)+.01*std::sin(double(frame));
            const double expected=baseline.update({.1,.2,raw},.011,4).z;
            auto a=c.sample(frame,k,true);if(a.changed) aligned.reset();
            if(a.advance) cached=aligned.update({.1,.2,raw},.011,4).z;
            assert(cached==expected);
            a=c.sample(frame,k,false);assert(a.accepted && !a.advance && cached==expected);
        }
    }
    // Actual audio conversion consumes the aligned authored centre, without adding the offset twice.
    const auto native=camera(.4);const TrackedPose centre{{0,1.6,0},{0,0,0,1}},origin{{0,1.6,0},{0,0,0,1}};
    const Vec3 offset{.3,-.1,.1};
    const auto middle=authored_head_view(native,.4,centre,centre,1,origin.orientation,offset);
    const auto audio=head_audio_pose(centre,origin,native,offset,&middle);assert(audio && audio->position==middle.position);
    const auto old=authored_head_view(native,.4,centre,centre);
    assert(old.position==native.position); // disabled feature keeps default zero-offset API
    std::cout<<"action_camera_alignment_check ok\n";
}
