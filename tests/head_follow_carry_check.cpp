// v804 (headset v802/v803: "when I turn my head the sword turns a little too"): NativeTurning's head-follow turns the player to face the head and
// BodyAnchor takes that turn off the room anchor, so the camera and the controllers keep their world pose. The arm upload's root-motion carry
// must leave that turn out too, or the drawn sword hand swings about the player on every physics step of a head turn; locomotion and stick
// turns are still carried.
#include "../src/full_body_driver.hpp"
#include <cstdio>
#include <cmath>
using namespace d2vr;using namespace d2vr::fullbody;
static const double kPi=3.14159265358979323846;
static std::array<float,15> frame_at(double yaw,Vec3 p) {
    const float c=static_cast<float>(std::cos(yaw)),s=static_cast<float>(std::sin(yaw));
    return {static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z), c,s,0, -s,c,0, 0,0,1, 1,1,1};
}
// The drawn right-hand attachment joint (the sword's socket) after one publish and one arm upload, the root having turned by root_yaw in between.
static bool drawn_attachment(double root_yaw,double follow_at_upload,Xf& out) {
    const auto b=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
    std::array<Transform,20> native_body{};
    for(unsigned i=0;i<20;++i) native_body[i]=to_transform(b.bind[i]);
    std::array<Transform,69> native_arm[2];
    for(unsigned side=0;side<2;++side) {const auto& a=side?right:left;for(unsigned i=0;i<69;++i) native_arm[side][i]=to_transform(a.bind[i]);}
    const Vec3 root_at{3,4,0};
    const auto root0=frame_at(0.7,root_at),root1=frame_at(0.7+root_yaw,root_at);
    Driver d;d.tune(0,1);
    for(unsigned side=0;side<2;++side) (void)d.arm(side,native_arm[side],root0);
    d.note_root(root0);
    Tracking t;t.epoch=1;t.tick=1000;t.source_tick=990;t.seconds=1.;t.tracking_revision=1;t.pose_epoch=1;t.reference_epoch=2;t.mapping_revision=3;t.tracked_hands=3;
    const Xf at_root{body::rot_z(0.7),root_at};
    t.eye=body::mul(at_root,Xf{b.bind[b.find("camera_jnt")].q,b.bind[b.find("camera_jnt")].p});
    for(unsigned side=0;side<2;++side) {t.grip_valid[side]=true;t.grip[side]=body::mul(at_root,Xf{{},{0.45,side?-0.2:0.2,1.15}});}
    t.hand_empty[1]=false;t.native_socket_token[1]=1;t.item_key[1]=0x53574f5244ull;   // the right hand holds the sword (native socket path)
    t.hand_empty[0]=true;
    t.anchor_root=root0;t.anchor_root_valid=true;
    t.head_follow=0;
    d.publish(t,{});
    d.note_root(root1,follow_at_upload);
    if(!d.body(native_body,root1,91)) return false;
    const auto posed=d.arm_frozen(1,native_arm[1],root1,1000,91);
    if(!posed) return false;
    out=to_world(frame_of(root1),to_xf(posed->joints[static_cast<unsigned>(right.find("right_hand_attachment_jnt"))]));
    return true;
}
int main() {
    const Vec3 root_at{3,4,0};
    Xf base;
    if(!drawn_attachment(0,0,base)) {std::puts("FAIL: no baseline solve");return 1;}
    struct Case {const char* name;double root_deg,follow_deg,carry_deg;};   // carry_deg: what the hands are rightly turned by (about the root axis)
    const Case cases[]={
        {"head-follow 0.15 deg (median physics step)",0.15,0.15,0},
        {"head-follow 0.7 deg (p90 step)",0.7,0.7,0},
        {"head-follow 2.7 deg (p99 step)",2.7,2.7,0},
        {"stick snap-turn 5 deg stays carried",5.0,0.0,5.0},
        {"stick 0.7 + head-follow 0.7: only the stick is carried",1.4,0.7,0.7},
        {"root lags the head-follow (0.3 of 0.7 deg): never over-corrects",0.3,0.7,0},
    };
    int bad=0;
    for(const auto& c:cases) {
        Xf got;
        if(!drawn_attachment(c.root_deg*kPi/180,c.follow_deg*kPi/180,got)) {std::printf("FAIL %s: no solve\n",c.name);++bad;continue;}
        const Xf turn{body::rot_z(c.carry_deg*kPi/180),{}};
        const Vec3 expected_p=root_at+rotated(turn.q,base.p-root_at);
        const auto expected_q=body::mul(turn.q,base.q);
        const double err_mm=length(got.p-expected_p)*1000;
        const double err_deg=2*std::acos(std::min(1.,std::abs(got.q.x*expected_q.x+got.q.y*expected_q.y+got.q.z*expected_q.z+got.q.w*expected_q.w)))*180/kPi;
        const bool ok=err_mm<.05 && err_deg<.005;
        std::printf("%s %-62s grip %6.2f mm  turn %6.3f deg\n",ok?"ok  ":"FAIL",c.name,err_mm,err_deg);
        if(!ok) ++bad;
    }
    if(bad) {std::printf("FAIL: %d case(s): the player turning to follow the head turned the sword hand with it\n",bad);return 1;}
    std::puts("PASS: head-follow root turns are not carried into the hands; stick turns still are");
    return 0;
}
