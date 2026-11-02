#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/world_ui_pose.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const auto straight=world_ui_front_pose({{1,1.72,2},{0,0,0,1}});
    assert(straight.position.x==1 && straight.position.y==1.72 && std::abs(straight.position.z-.95)<1e-9);
    const auto turned=world_ui_front_pose({{1,1.72,2},{0,std::sqrt(.5),0,std::sqrt(.5)}});
    assert(std::abs(turned.position.x+.05)<1e-9 && turned.position.y==1.72 && std::abs(turned.position.z-2)<1e-9);
    const auto tilted=world_ui_front_pose({{1,1.72,2},{std::sin(.3),0,std::sin(.2),std::sqrt(1-std::sin(.3)*std::sin(.3)-std::sin(.2)*std::sin(.2))}});
    assert(tilted.position.y==1.72 && tilted.orientation.x==0 && tilted.orientation.z==0);
    assert(std::abs(std::hypot(tilted.position.x-1,tilted.position.z-2)-1.05)<1e-9);
    bool rejected=false;try {world_ui_front_pose({{0,NAN,0},{}});}catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
    rejected=false;try {world_ui_front_pose({{0,1.7,0},{}},0);}catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
    for(const Vec3 hand:{Vec3{.2,1.1,-.35},Vec3{-.3,1.4,-.2},Vec3{0,1.0,-.6}}) { // held note: above the hand, face to the eyes
        const Vec3 head{0,1.7,0};const auto note=world_ui_hand_pose(head,hand);
        assert(std::abs(note.position.y-(hand.y+.12))<1e-9 && note.position.x==hand.x && note.position.z==hand.z);
        const auto face=rotated(note.orientation,{0,0,1}),to_head=(head-note.position)*(1/length(head-note.position));
        assert(length(face-to_head)<1e-9);
    }
    { // v722 dialogue at the speaker: on the line to the prompt point, 0.8-3 m away, its face (+Z) turned to the eyes
        const TrackedPose head{{0,1.7,0},{0,0,0,1}};
        const auto [near,dn]=world_ui_speaker_pose(head,{.3,1.6,-1.4});
        assert(std::abs(dn-length(Vec3{.3,-.1,-1.4}))<1e-9 && std::abs(near.position.x-.3)<1e-9 && near.orientation.x==0 && near.orientation.z==0);
        const auto face=rotated(near.orientation,{0,0,1});assert(std::abs(face.y)<1e-9 && dot(face,head.position-near.position)>0);
        const auto [far,df]=world_ui_speaker_pose(head,{0,1.7,-8});assert(df==3 && std::abs(far.position.z+3)<1e-9);
        const auto [close,dc]=world_ui_speaker_pose(head,{0,1.7,-.2});assert(dc==.8 && std::abs(close.position.z+.8)<1e-9);
        bool bad=false;try {world_ui_speaker_pose(head,{NAN,0,0});}catch(const std::invalid_argument&) {bad=true;}assert(bad);
    }
    { // dual wield: the wheel ring above the opening hand, plane following the hand, normal to the eyes, up tilted with the view
        const Vec3 head{0,1.7,0};
        for(const Vec3 hand:{Vec3{-.25,1.1,-.35},Vec3{.3,1.4,-.2},Vec3{0,1.0,-.6}}) {
            const auto pose=world_ui_hand_pose(head,hand,wheel_hand_lift_m);const auto pl=wheel_plane_of(pose,wheel_hand_width_m);
            assert(std::abs(pl.centre.y-(hand.y+wheel_hand_lift_m))<1e-9 && pl.centre.x==hand.x && pl.centre.z==hand.z && pl.width==wheel_hand_width_m);
            assert(length(pl.normal-(head-pl.centre)*(1/length(head-pl.centre)))<1e-9); // faces the eyes
            assert(std::abs(pl.right.y)<1e-9 && std::abs(length(pl.right)-1)<1e-9 && std::abs(length(pl.up)-1)<1e-9);
            assert(std::abs(dot(pl.right,pl.up))<1e-9 && std::abs(dot(pl.right,pl.normal))<1e-9 && std::abs(dot(pl.up,pl.normal))<1e-9 && pl.up.y>0);
            const Vec3 moved=hand+Vec3{.1,.05,-.1};const auto pm=wheel_plane_of(world_ui_hand_pose(head,moved,wheel_hand_lift_m),wheel_hand_width_m);
            assert(length(pm.centre-pl.centre-Vec3{.1,.05,-.1})<1e-9); // follows the hand
        }
        const auto flat=wheel_plane_of(world_ui_front_pose({{0,1.7,0},{0,0,0,1}},.9),.8); // head-front fallback: upright, 0.9 m ahead, facing back
        assert(std::abs(flat.centre.z+.9)<1e-9 && std::abs(flat.normal.z-1)<1e-9 && std::abs(flat.up.y-1)<1e-9 && std::abs(flat.right.x-1)<1e-9);
        bool bad=false;try {world_ui_hand_pose(head,{NAN,1,0},wheel_hand_lift_m);}catch(const std::invalid_argument&) {bad=true;}assert(bad); // untracked hand: the caller falls back
    }
    { // v776 dual wield: the ring in front of the hand that opened it. Its centre is on that hand's aim ray, wheel_hand_ahead_m (x the HUD factor) from the
      // aim origin, and it faces the eyes upright, whatever the hand's yaw and pitch.
        const Vec3 head{0,1.65,0};const double rad=3.14159265358979/180;
        for(const double scale:{1.,100./65.}) for(const double yaw:{0.,35.,-50.}) for(const double pitch:{0.,-30.,40.}) {
            const auto q=multiplied({0,std::sin(-yaw*rad/2),0,std::cos(yaw*rad/2)},{std::sin(pitch*rad/2),0,0,std::cos(pitch*rad/2)}); // yaw right, pitch up
            const TrackedPose aim{{-.2,1.3,-.4},q};const auto ray=rotated(q,{0,0,-1});
            const auto pl=wheel_plane_of(wheel_aim_pose(head,aim,scale),wheel_hand_width_m*scale);const auto to=pl.centre-aim.position;
            assert(std::abs(dot(to,ray)-wheel_hand_ahead_m*scale)<1e-9 && length(to-ray*dot(to,ray))<1e-9);          // on the aim ray, ahead_m along it
            assert(length(pl.normal-(head-pl.centre)*(1/length(head-pl.centre)))<1e-9 && std::abs(pl.right.y)<1e-9 && pl.up.y>0); // faces the eyes, upright
        }
        bool bad=false;try {wheel_aim_pose(head,{{0,1,0},{0,0,0,0}});}catch(const std::invalid_argument&) {bad=true;}assert(bad); // a degenerate aim pose has no ring
    }
    std::cout<<"PASS: centered eye-height world UI, upright yaw, held note facing the eyes and invalid pose rejection\n";
}
