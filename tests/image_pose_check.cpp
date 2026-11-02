// v861 (paired, user: "when I turn with the stick the body and hands must turn together with the camera; now the camera turns, then the body,
// then the hands"): an image's arms are drawn from that image's own publication (Driver::begin_image at the render's eye-0 begin, image_arm at its
// GPU skin copy, expressed in the arm frame the copy draws with). Real Driver. Per frame: the Present locates the room, the render draws the
// previous Setup's image (its skin copy sees the arm frame either before the next tick or after it: both engine orders), the tick moves and turns
// the player and uploads the arms (the old path), the Setup publishes the camera and hands on the tick's anchor.
// The controllers are held still in the room while the player runs, stick-turns and snap-turns: the drawn attachment must not move against the
// image's camera, and must sit exactly on the image's solved joint whatever frame it is drawn in.
#include "../src/full_body_driver.hpp"
#include <cstdio>
#include <cmath>
using namespace d2vr;using namespace d2vr::fullbody;
static const double kPi=3.14159265358979323846;
static std::array<float,15> frame_at(double yaw,Vec3 p) {
    const float c=static_cast<float>(std::cos(yaw)),s=static_cast<float>(std::sin(yaw));
    return {static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z), c,s,0, -s,c,0, 0,0,1, 1,1,1};
}
struct Result {double map_mm{},camera_mm{},tick_camera_mm{},target_mm{};unsigned images{};};
static Result run(double hz,double run_mps,double stick_dps,double snap_deg,unsigned snap_every,bool copy_after_tick) {
    const auto b=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
    std::array<Transform,20> native_body{};for(unsigned i=0;i<20;++i) native_body[i]=to_transform(b.bind[i]);
    std::array<Transform,69> native_arm[2];
    for(unsigned side=0;side<2;++side) {const auto& a=side?right:left;for(unsigned i=0;i<69;++i) native_arm[side][i]=to_transform(a.bind[i]);}
    const unsigned att[2]={static_cast<unsigned>(left.find("left_hand_attachment_jnt")),static_cast<unsigned>(right.find("right_hand_attachment_jnt"))};
    const Xf camera_local{b.bind[b.find("camera_jnt")].q,b.bind[b.find("camera_jnt")].p};
    Driver d;d.tune(0,1);
    const double dt=1/hz;double yaw=.3;Vec3 root{3,4,0};
    const Xf head=camera_local;Xf grips[2];
    for(unsigned side=0;side<2;++side) grips[side]=Xf{{},{0.45,side?-0.2:0.2,1.15}};
    std::array<float,15> frame=frame_at(yaw,root);
    Xf image_map{};bool have_image=false;std::array<Transform,69> tick_palette[2];std::array<float,15> tick_frame=frame;bool have_tick=false;
    Xf first_rel[2];bool have_first[2]{};Result r;
    for(unsigned k=0;k<120;++k) {
        const double t=k*dt;
        const auto render=[&](const std::array<float,15>& live) {   // the render of the previous Setup's image: begin, then the skin copies
            if(!have_image) return;
            d.begin_image(k);
            for(unsigned side=0;side<2;++side) {
                const auto pose=d.image_arm(side,live,k);const auto solved=d.image_joint(side,att[side]);
                if(!pose || !solved) {r.map_mm=1e9;continue;}
                const auto drawn=to_world(frame_of(live),to_xf((*pose)[att[side]]));
                r.map_mm=std::max(r.map_mm,length(drawn.p-*solved)*1000);
                const auto target=d.attachment_target(side,body::mul(image_map,grips[side]));
                if(k>20) r.target_mm=std::max(r.target_mm,length(drawn.p-target.p)*1000);
                const auto rel=body::mul(body::inverse(image_map),drawn);   // the attachment in the image's camera (room) frame
                if(!have_first[side]) {first_rel[side]=rel;have_first[side]=true;}
                else if(k>20) r.camera_mm=std::max(r.camera_mm,length(rel.p-first_rel[side].p)*1000);
                if(have_tick && k>20) {   // the old path: the tick's upload as drawn in the same render
                    const auto old=body::mul(body::inverse(image_map),to_world(frame_of(live),to_xf(tick_palette[side][att[side]])));
                    r.tick_camera_mm=std::max(r.tick_camera_mm,length(old.p-first_rel[side].p)*1000);
                }
            }
            ++r.images;
        };
        if(!copy_after_tick) render(frame);
        yaw+=stick_dps*kPi/180*dt;if(snap_every && k%snap_every==snap_every-1) yaw+=snap_deg*kPi/180;   // the tick: run and turn
        root=root+Vec3{std::cos(yaw),std::sin(yaw),0}*(run_mps*dt);
        frame=frame_at(yaw,root);
        const std::uint64_t key=k+100,tick=1000+static_cast<std::uint64_t>(t*1000);
        d.note_root(frame,0,yaw);(void)d.body(native_body,frame,key);
        for(unsigned side=0;side<2;++side) if(const auto p=d.arm_frozen(side,native_arm[side],frame,tick,key)) tick_palette[side]=p->joints;
        tick_frame=frame;have_tick=true;
        if(copy_after_tick) render(frame);
        const Xf map{body::rot_z(yaw),root};   // the Setup: camera and hands on this tick's anchor
        Tracking tr;tr.epoch=1;tr.tick=tick;tr.source_tick=tick;tr.seconds=1+t;tr.tracking_revision=k+1;tr.pose_epoch=1;tr.reference_epoch=2;tr.mapping_revision=3;tr.tracked_hands=3;
        tr.eye=body::mul(map,head);
        for(unsigned side=0;side<2;++side) {tr.grip_valid[side]=true;tr.grip[side]=body::mul(map,grips[side]);tr.hand_empty[side]=true;}
        tr.anchor_root=frame;tr.anchor_root_valid=true;tr.camera_heading=yaw;tr.camera_heading_valid=true;
        tr.rig_yaw=yaw;tr.rig_pivot=root;tr.rig_valid=true;   // the tracking space's world yaw and where it sits
        d.publish(tr,{});image_map=map;have_image=true;
    }
    return r;
}
int main() {
    struct Case {const char* name;double hz,run,stick,snap;unsigned every;};
    const Case cases[]={{"45 Hz still",45,0,0,0,0},{"45 Hz run 5 m/s",45,5,0,0,0},{"45 Hz stick 90 deg/s",45,0,90,0,0},{"45 Hz 45 deg snaps",45,0,0,45,15},
                        {"45 Hz run 5 m/s + stick 90 deg/s",45,5,90,0,0},{"90 Hz run 5 m/s + stick 90 deg/s",90,5,90,0,0},
                        {"45 Hz run 5 m/s + 45 deg snaps",45,5,0,45,15},{"90 Hz stick 180 deg/s + 30 deg snaps",90,0,180,30,10}};
    int bad=0;
    for(const auto& c:cases) for(const bool after:{false,true}) {
        const auto r=run(c.hz,c.run,c.stick,c.snap,c.every,after);
        const bool ok=r.images>100 && r.map_mm<.01 && r.camera_mm<1.;
        std::printf("%s %-36s copy %s tick: on its solve %.4f mm, moved vs camera %.2f mm (tick upload %.1f), off target %.1f mm, %u images\n",
            ok?"ok  ":"FAIL",c.name,after?"after ":"before",r.map_mm,r.camera_mm,r.tick_camera_mm,r.target_mm,r.images);
        if(!ok) ++bad;
    }
    if(bad) {std::printf("FAIL: %d case(s)\n",bad);return 1;}
    std::puts("PASS: an image's arms are drawn from its own publication, still against its camera while the player runs and turns");
    return 0;
}
