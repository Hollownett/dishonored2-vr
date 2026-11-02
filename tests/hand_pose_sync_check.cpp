// v830 (headset v815/v819, paired: "while running and turning the hands lag behind the body"; "look at the exact-pose technique"): the engine
// uploads the arm palettes of an image BEFORE that image's Setup publishes the head and hands (arm-origin-trace: u0 u1 p2 in every interval), so
// the arms were solved from the previous Setup's publication: the controllers of one locate earlier than the camera of the same image. Real
// Driver, engine order per interval: runtime locate at the Present (note_xr), arm uploads (note_root, body, arm_frozen), Setup (publish).
// The player runs, stick-turns and physically turns (head and controllers together, in the room) at 45 and 90 Hz. The drawn attachment joint
// must sit where the image's own camera places the controller.
#include "../src/full_body_driver.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace d2vr;using namespace d2vr::fullbody;
static const double kPi=3.14159265358979323846;
static std::array<float,15> frame_at(double yaw,Vec3 p) {
    const float c=static_cast<float>(std::cos(yaw)),s=static_cast<float>(std::sin(yaw));
    return {static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z), c,s,0, -s,c,0, 0,0,1, 1,1,1};
}
struct Case {const char* name;double hz,run_mps,stick_dps,body_dps;bool refresh;double max_mm;
    double snap_deg=0;unsigned snap_every=0;bool root_lags=false,heading=false;};   // v832: snap turns; the body model's yaw a frame behind the view; the camera heading passed
// Worst drawn-attachment error (mm) against the image's own camera placement, frames 10.. of 90.
static double g_item_new,g_item_old;   // v835: the item a weapon reader places before the upload (item_advance) / as the engine did (last joint, live frame) vs the drawn hand, mm
static double run_case(const Case& c) {
    g_item_new=g_item_old=-1;Xf prev_drawn[2];bool have_prev[2]{};std::array<float,15> prev_frame{};
    const auto b=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
    std::array<Transform,20> native_body{};for(unsigned i=0;i<20;++i) native_body[i]=to_transform(b.bind[i]);
    std::array<Transform,69> native_arm[2];
    for(unsigned side=0;side<2;++side) {const auto& a=side?right:left;for(unsigned i=0;i<69;++i) native_arm[side][i]=to_transform(a.bind[i]);}
    const Xf camera_local{b.bind[b.find("camera_jnt")].q,b.bind[b.find("camera_jnt")].p};
    Driver d;d.tune(0,1);d.legacy_hand_sample(!c.refresh);
    const double dt=1/c.hz;
    double yaw=.3;Vec3 root{3,4,0};
    // The room: the head and both controllers turn together about the head (the player physically turning), the hands held out in front.
    const auto room=[&](double t,Xf& head,Xf grips[2]) {
        const double phi=c.body_dps*kPi/180*t;
        head=Xf{body::rot_z(phi),{0,0,0}};head=body::mul(head,camera_local);
        for(unsigned side=0;side<2;++side) grips[side]=body::mul(Xf{body::rot_z(phi),{}},Xf{{},{0.45,side?-0.2:0.2,1.15}});
    };
    // The XR->world map of an image's Setup: the camera anchor on the player root (rigid, as the engine's view rides the player).
    const auto world_from_room=[&](double root_yaw,Vec3 at) {return Xf{body::rot_z(root_yaw),at};};
    std::array<float,15> frame=frame_at(yaw,root);
    for(unsigned side=0;side<2;++side) (void)d.arm(side,native_arm[side],frame);
    Xf map=world_from_room(yaw,root);std::uint64_t map_key=0;bool have_map=false;
    double worst=0;
    for(unsigned k=0;k<90;++k) {
        const double t=k*dt;
        // game tick k: the player moves and stick-turns
        const double yaw_before=yaw;
        yaw+=c.stick_dps*kPi/180*dt;if(c.snap_every && k%c.snap_every==c.snap_every-1) yaw+=c.snap_deg*kPi/180;
        root=root+Vec3{std::cos(yaw),std::sin(yaw),0}*(c.run_mps*dt);
        frame=frame_at(c.root_lags?yaw_before:yaw,root);
        // the Present before this interval: the runtime locates the head and grips this image is shown with
        Xf head,grips[2];room(t,head,grips);
        if(have_map) {
            Driver::XrSample s;s.head=body::mul(map,head);for(unsigned side=0;side<2;++side) s.grip[side]=body::mul(map,grips[side]);
            s.hands=3;s.map=map_key;s.serial=k+1;d.note_xr(s);
        }
        // a weapon reader of tick k (after the tick, before its arm upload): the previous tick's drawn joint moved on by the hand's motion
        Xf predicted[2];bool have_pred[2]{};
        if(c.heading) for(unsigned side=0;side<2;++side) if(have_prev[side]) if(const auto mv=d.item_advance(side,frame,0,yaw)) {predicted[side]=body::mul(*mv,prev_drawn[side]);have_pred[side]=true;}
        // the arm uploads of image k (before its Setup)
        const std::uint64_t key=k+100,tick=1000+static_cast<std::uint64_t>(t*1000);
        if(c.heading) d.note_root(frame,0,yaw);else d.note_root(frame);
        (void)d.body(native_body,frame,key);
        double err=0;bool posed_both=true;
        for(unsigned side=0;side<2;++side) {
            const auto posed=d.arm_frozen(side,native_arm[side],frame,tick,key);
            if(!posed) {posed_both=false;continue;}
            const auto& a=side?right:left;
            const auto got=to_world(frame_of(frame),to_xf(posed->joints[static_cast<unsigned>(a.find(side?"right_hand_attachment_jnt":"left_hand_attachment_jnt"))]));
            const auto want_grip=body::mul(world_from_room(yaw,root),grips[side]);
            const auto diag=d.diagnostic();
            // the hand target the drawn arm was solved for, carried into this tick's world (DrawnPalmSource::grip): the timing alone, not the IK's reach
            if(!diag.palm_source[side].valid) {posed_both=false;continue;}
            const auto& g=diag.palm_source[side].grip.position;
            err=std::max(err,length(Vec3{g.x,g.y,g.z}-want_grip.p)*1000);
            // the hand target this tick's upload aims the attachment joint at (the IK reaches it in game; this rig's IK does not, so targets are compared)
            const auto target=body::mul(want_grip,d.diagnostic().grip_to_attachment[side]);
            if(k>=10 && have_pred[side]) {g_item_new=std::max(g_item_new,length(predicted[side].p-target.p)*1000);
                const auto old=body::mul(body::mul(frame_of(frame).rigid,body::inverse(frame_of(prev_frame).rigid)),prev_drawn[side]);g_item_old=std::max(g_item_old,length(old.p-target.p)*1000);}
            prev_drawn[side]=target;have_prev[side]=true;
        }
        if(k>=10 && have_map && posed_both) worst=std::max(worst,err);
        if(k>=10 && have_map && !posed_both) return 1e9;
        prev_frame=frame;
        // the Setup of image k: the camera and the hands of this locate, on this tick's anchor
        map=world_from_room(yaw,root);map_key=k+1;have_map=true;
        Tracking tr;tr.epoch=1;tr.tick=tick;tr.source_tick=tick;tr.seconds=1+t;tr.tracking_revision=k+1;tr.pose_epoch=1;tr.reference_epoch=2;tr.mapping_revision=3;tr.tracked_hands=3;
        tr.eye=body::mul(map,head);
        for(unsigned side=0;side<2;++side) {tr.grip_valid[side]=true;tr.grip[side]=body::mul(map,grips[side]);tr.hand_empty[side]=true;tr.xr_grip_world[side]=tr.grip[side];}
        tr.xr_head_world=tr.eye;tr.xr_hands=3;tr.xr_map=map_key;tr.xr_serial=k+1;
        tr.anchor_root=frame;tr.anchor_root_valid=true;tr.camera_heading=yaw;tr.camera_heading_valid=c.heading;
        d.publish(tr,{});
    }
    return worst;
}
int main() {
    const Case cases[]={
        {"45 Hz run 5 m/s, stick 90 deg/s, still in the room (fixed)",45,5,90,0,true,1.0},
        {"45 Hz run 5 m/s, physical turn 120 deg/s (old order)",45,5,0,120,false,-1},
        {"45 Hz run 5 m/s, physical turn 120 deg/s (fixed)",45,5,0,120,true,1.0},
        {"45 Hz run + stick 90 + physical 120 deg/s (fixed)",45,5,90,120,true,1.0},
        {"90 Hz run + stick 90 + physical 120 deg/s (old order)",90,5,90,120,false,-1},
        {"90 Hz run + stick 90 + physical 120 deg/s (fixed)",90,5,90,120,true,1.0},
        {"45 Hz run + 45 deg snaps, body yaw a frame late (root carry)",45,5,0,0,true,-1,45,15,true,false},
        {"45 Hz run + 45 deg snaps, body yaw a frame late (heading carry)",45,5,0,0,true,1.0,45,15,true,true},
        {"45 Hz run + stick + physical + snaps, heading carry",45,5,90,120,true,1.0,45,15,true,true},
    };
    int bad=0;
    for(const auto& c:cases) {
        const double mm=run_case(c);
        // the old order shows the defect (a whole frame of the room motion, > 8 mm at 45 Hz), the fixed one is within max_mm
        const bool ok=c.max_mm<0?mm>(c.hz<60?15.:7.):mm<=c.max_mm;
        std::printf("%s %-62s drawn attachment off its camera's controller by %7.2f mm\n",ok?"ok  ":"FAIL",c.name,mm);
        if(!ok) ++bad;
        if(g_item_new>=0) {const bool item_ok=g_item_new<=1.0;
            std::printf("%s   item read before the upload: %7.2f mm from the drawn hand (as the engine did: %7.2f mm)\n",item_ok?"ok  ":"FAIL",g_item_new,g_item_old);if(!item_ok) ++bad;}
    }
    if(bad) {std::printf("FAIL: %d case(s)\n",bad);return 1;}
    std::puts("PASS: the arms of an image come from the locate of that image's camera");
    return 0;
}
