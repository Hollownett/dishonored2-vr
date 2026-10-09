#include "../src/tracking_offset.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace d2vr;
static void close(Vec3 a,Vec3 b) {assert(length(a-b)<1e-5);}
int main() {
    GameCameraPose anchor;anchor.position={10,20,30};anchor.basis={1,0,0,0,1,0,0,0,1};
    TrackingOffsetOwner owner{1,2,3};TrackedPose origin{{0,1.7,0},{}},head=origin,grip{{.3,1.2,-.4},{}};
    auto snapshot=[&](TrackedPose h) {return tracking_offset_snapshot(owner,1,1000,origin,h,anchor,2,true,true,true);};
    const auto baseline=snapshot(head);assert(baseline);close(baseline->requested_world_offset,{});
    const auto original_grip=to_game_pose(grip,origin,anchor,2);
    head.position=head.position+Vec3{.4,.15,-.2};const auto lean=snapshot(head);assert(lean);
    close(lean->requested_world_offset,{.4,-.8,.3});
    // Stationary controller remains world stationary with free common offset.
    close(lean->relative_pose(grip,lean->requested_world_offset).position,original_grip.position);
    close(lean->relative_pose(head,lean->requested_world_offset).position,{10.4,19.2,30.3});
    // A common physical translation moves head and hand equally.
    auto moved_grip=grip;moved_grip.position=moved_grip.position+Vec3{.4,.15,-.2};
    close(lean->relative_pose(moved_grip,lean->requested_world_offset).position,original_grip.position+lean->requested_world_offset);
    // If a future collision result restricts the offset, hand-to-head separation remains exact.
    const Vec3 constrained{.1,-.2,.05};
    close(lean->relative_pose(grip,constrained).position-lean->relative_pose(head,constrained).position,
          original_grip.position-to_game_pose(head,origin,anchor,2).position);
    head=origin;head.position.y-=.4;const auto crouch=snapshot(head);assert(crouch);
    close(crouch->requested_world_offset,{0,0,-.8});
    close(crouch->relative_pose(grip,crouch->requested_world_offset).position,original_grip.position);
    // World heading and XR reference yaw both participate; HMD tilt never rotates translation.
    const auto s=std::sqrt(.5);origin.orientation={0,s,0,s};head=origin;head.position.x+=.3;
    const auto yaw=snapshot(head);assert(yaw);close(yaw->requested_world_offset,{-.6,0,0});
    anchor.basis={0,1,0,-1,0,0,0,0,1};const auto world_yaw=snapshot(head);assert(world_yaw);
    close(world_yaw->requested_world_offset,{0,-.6,0});
    head.orientation={s,0,0,s};const auto tilted=snapshot(head);assert(tilted);
    close(tilted->requested_world_offset,world_yaw->requested_world_offset);
    assert(lean->current_for(owner,1,1100));assert(!lean->current_for(owner,1,1101));
    assert(!lean->current_for(owner,1,999));assert(!lean->current_for(owner,2,1001));
    for(auto changed:{TrackingOffsetOwner{9,2,3},TrackingOffsetOwner{1,9,3},TrackingOffsetOwner{1,2,9}})
        assert(!lean->current_for(changed,1,1001));
    for(unsigned fail=0;fail<3;++fail) assert(!tracking_offset_snapshot(owner,1,1000,origin,head,anchor,2,fail!=0,fail!=1,fail!=2));
    assert(!tracking_offset_snapshot({0,2,3},1,1000,origin,head,anchor,2,true,true,true));
    assert(!tracking_offset_snapshot(owner,0,1000,origin,head,anchor,2,true,true,true));
    head.position.x=std::numeric_limits<double>::quiet_NaN();assert(!snapshot(head));
    head=origin;assert(!tracking_offset_snapshot(owner,1,1000,origin,head,anchor,0,true,true,true));
    std::cout<<"PASS: common tracking offset, stationary/common controller translation, crouch, yaw, constrained coherence, tracking and owner/reference freshness rejection\n";
}
