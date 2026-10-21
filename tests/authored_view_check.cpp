#include "../src/authored_view.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace d2vr;
static constexpr double kPi=3.14159265358979323846;
static GameCameraPose native_yaw_pitch_roll(double yaw,double pitch=0,double roll=0) {
    // forward from yaw/pitch; left is horizontal-rolled by `roll` about forward
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const Vec3 f{cy*cp,sy*cp,sp},l0{-sy,cy,0};
    const Vec3 u0{-cy*sp,-sy*sp,cp};
    const double cr=std::cos(roll),sr=std::sin(roll);
    const Vec3 l=l0*cr+u0*sr,u=u0*cr-l0*sr;
    GameCameraPose p{};p.position={1,2,3};p.horizontal_fov=1.4f;p.vertical_fov=.9f;
    p.basis={float(f.x),float(f.y),float(f.z),float(l.x),float(l.y),float(l.z),float(u.x),float(u.y),float(u.z)};
    return p;
}
static Quaternion about_y(double a){return {0,std::sin(a/2),0,std::cos(a/2)};} // XR yaw (left turn is +a)
static Quaternion about_x(double a){return {std::sin(a/2),0,0,std::cos(a/2)};}
static Quaternion about_z(double a){return {0,0,std::sin(a/2),std::cos(a/2)};}
static Quaternion mul(Quaternion a,Quaternion b){return multiplied(a,b);}
static double yaw_of(const GameCameraPose& p){return std::atan2(p.basis[1],p.basis[0]);}
static bool near(double a,double b,double e=1e-4){return std::fabs(a-b)<e;}
int main(){
    {AuthoredHeading h; // enter_takes_native_yaw_once
     assert(!h.update(native_yaw_pitch_roll(.4),false,1));
     auto y=h.update(native_yaw_pitch_roll(.4),true,1);assert(y && near(*y,.4));
     y=h.update(native_yaw_pitch_roll(.5),true,1);assert(y && near(*y,.4) && h.snaps()==0);}
    {AuthoredHeading h; // small_native_yaw_changes_do_not_rotate
     h.update(native_yaw_pitch_roll(0),true,1);
     for(int i=1;i<=90;++i){auto y=h.update(native_yaw_pitch_roll(25.*kPi/180*i/90),true,1);assert(y && near(*y,0));}
     assert(h.snaps()==0);}
    {AuthoredHeading h; // large_change_snaps_once
     h.update(native_yaw_pitch_roll(0),true,1);
     auto y=h.update(native_yaw_pitch_roll(40.*kPi/180),true,1);assert(y && near(*y,40.*kPi/180) && h.snaps()==1);
     y=h.update(native_yaw_pitch_roll(41.*kPi/180),true,1);assert(near(*y,40.*kPi/180) && h.snaps()==1);}
    {AuthoredHeading h; // vertical_native_forward_keeps_previous_yaw
     h.update(native_yaw_pitch_roll(.3),true,1);
     auto y=h.update(native_yaw_pitch_roll(2.0,1.45),true,1);assert(y && near(*y,.3) && h.snaps()==0);
     y=h.update(native_yaw_pitch_roll(2.0,-1.45),true,1);assert(y && near(*y,.3));}
    {AuthoredHeading h; // epoch_change_reenters; leaving authored re-enters too
     h.update(native_yaw_pitch_roll(0),true,1);
     auto y=h.update(native_yaw_pitch_roll(.2),true,2);assert(near(*y,.2) && h.snaps()==0);
     assert(!h.update(native_yaw_pitch_roll(.2),false,2));
     y=h.update(native_yaw_pitch_roll(-.7),true,2);assert(near(*y,-.7));
     h.reset();y=h.update(native_yaw_pitch_roll(.9),true,2);assert(near(*y,.9));}
    {AuthoredHeading h; // wrap: 170 deg to -170 deg is a 20 degree change, not a snap
     h.update(native_yaw_pitch_roll(170.*kPi/180),true,1);
     auto y=h.update(native_yaw_pitch_roll(-170.*kPi/180),true,1);assert(near(*y,170.*kPi/180) && h.snaps()==0);}
    { // head_rotation_moves_view: native roll and pitch do not appear; HMD yaw +20 degrees turns the basis +20 degrees
     const auto nat=native_yaw_pitch_roll(.3,-30.*kPi/180,15.*kPi/180);
     const TrackedPose centre{{0,1.6,0},{0,0,0,1}};
     const auto a=authored_head_view(nat,.3,centre,centre);
     assert(near(yaw_of(a),.3) && near(a.basis[2],0) && near(a.basis[8],1) && near(a.basis[4],std::cos(.3)));
     const TrackedPose turned{{0,1.6,0},about_y(20.*kPi/180)};
     const auto b=authored_head_view(nat,.3,turned,turned);
     assert(near(yaw_of(b),.3+20.*kPi/180) && near(b.basis[8],1) && near(b.basis[2],0));
     assert(near(b.position[0],1) && near(b.position[1],2) && near(b.position[2],3));
     assert(b.horizontal_fov==nat.horizontal_fov);}
    { // ipd_offset_preserved: eyes 64 mm apart produce outputs 64 mm apart, along the head's own right
     const auto nat=native_yaw_pitch_roll(.1,.2,.1);
     const Quaternion q=mul(about_y(.7),Quaternion{std::sin(-.2),0,0,std::cos(-.2)}); // yaw and pitch
     const TrackedPose centre{{0,1.6,0},q};
     const Vec3 right=rotated(q,{1,0,0});
     const TrackedPose l{centre.position-right*.032,q},r{centre.position+right*.032,q};
     const auto a=authored_head_view(nat,.1,centre,l,1),b=authored_head_view(nat,.1,centre,r,1);
     const Vec3 d{double(b.position[0])-a.position[0],double(b.position[1])-a.position[1],double(b.position[2])-a.position[2]};
     assert(near(length(d),.064,1e-5));
     const auto c=authored_head_view(nat,.1,centre,l,100),e=authored_head_view(nat,.1,centre,r,100);
     assert(near(std::hypot(std::hypot(double(e.position[0])-c.position[0],double(e.position[1])-c.position[1]),double(e.position[2])-c.position[2]),6.4,1e-3));}
    { // the 0.15 m rejection and non-finite input throw
     const auto nat=native_yaw_pitch_roll(0);const TrackedPose centre{{0,1.6,0},{0,0,0,1}};
     const double nan=std::numeric_limits<double>::quiet_NaN();
     int thrown=0;
     auto t=[&](auto f){try{f();}catch(const std::invalid_argument&){++thrown;}};
     t([&]{authored_head_view(nat,nan,centre,centre);});
     t([&]{authored_head_view(nat,0,centre,TrackedPose{{nan,1.6,0},{0,0,0,1}});});
     t([&]{authored_head_view(nat,0,TrackedPose{{0,nan,0},{0,0,0,1}},centre);});
     t([&]{authored_head_view(nat,0,centre,TrackedPose{{.2,1.6,0},{0,0,0,1}});});
     t([&]{authored_head_view(nat,0,centre,centre,0);});
     auto bad=nat;bad.position[1]=std::numeric_limits<float>::quiet_NaN();
     t([&]{authored_head_view(bad,0,centre,centre);});
     assert(thrown==6);}
    { // a non-finite native basis never produces a heading
     auto bad=native_yaw_pitch_roll(0);bad.basis[0]=std::numeric_limits<float>::quiet_NaN();
     AuthoredHeading h;assert(!h.update(bad,true,1));}
    { // heading_yaw differs from the native yaw: the output follows the held heading, never the live native camera
     const auto nat=native_yaw_pitch_roll(.9,-.4,.3);
     const TrackedPose c0{{0,1.6,0},{0,0,0,1}},c1{{0,1.6,0},about_y(.2)};
     assert(near(yaw_of(authored_head_view(nat,.1,c0,c0)),.1));
     assert(near(yaw_of(authored_head_view(nat,.1,c1,c1)),.3));}
    { // non-identity origin: the recenter yaw is removed (origin .5, eye .85 -> heading + .35)
     const auto nat=native_yaw_pitch_roll(.9);
     const TrackedPose c{{0,1.6,0},about_y(.85)};
     assert(near(yaw_of(authored_head_view(nat,.1,c,c,1,about_y(.5))),.1+.35));
     assert(near(yaw_of(authored_head_view(nat,.1,c,c)),.1+.85));}
    { // HMD pitch and roll reach the output basis; native pitch and roll do not
     const auto nat=native_yaw_pitch_roll(.9,-.5,.4);
     const Quaternion qs[2]={mul(about_y(.3),about_x(.4)),mul(about_y(.3),about_z(.5))};
     for(const auto& q:qs){
      const TrackedPose c{{0,1.6,0},q};
      const auto o=authored_head_view(nat,0,c,c);
      const Vec3 f=rotated(q,{0,0,-1}),l=rotated(q,{-1,0,0}),u=rotated(q,{0,1,0});
      const Vec3 want[3]={{-f.z,-f.x,f.y},{-l.z,-l.x,l.y},{-u.z,-u.x,u.y}};
      for(int i=0;i<3;++i){
       const Vec3 w=want[i];
       const Vec3 got{o.basis[i*3],o.basis[i*3+1],o.basis[i*3+2]};
       assert(near(got.x,w.x) && near(got.y,w.y) && near(got.z,w.z));}
      assert(std::fabs(o.basis[2])>.01 || std::fabs(o.basis[8]-1)>.01); // the pose is genuinely tilted
     }}
    { // IPD direction: the right eye sits along the output's right (minus left)
     const auto nat=native_yaw_pitch_roll(.5,.3,.2);
     const Quaternion q=mul(about_y(.7),about_x(-.2));
     const TrackedPose centre{{0,1.6,0},q};
     const Vec3 right=rotated(q,{1,0,0});
     const TrackedPose l{centre.position-right*.032,q},r{centre.position+right*.032,q};
     const auto a=authored_head_view(nat,.1,centre,l),b=authored_head_view(nat,.1,centre,r);
     const Vec3 d{double(b.position[0])-a.position[0],double(b.position[1])-a.position[1],double(b.position[2])-a.position[2]};
     const Vec3 lb{b.basis[3],b.basis[4],b.basis[5]};
     assert(dot(d,lb*-1.)>.99*length(d));}
    { // entering with a vertical native forward takes the yaw from the left axis
     AuthoredHeading h;auto y=h.update(native_yaw_pitch_roll(1.2,1.5),true,1);assert(y && near(*y,1.2,1e-3));}
    { // reset() clears the snap count
     AuthoredHeading h;h.update(native_yaw_pitch_roll(0),true,1);h.update(native_yaw_pitch_roll(1),true,1);assert(h.snaps()==1);
     h.reset();assert(h.snaps()==0);}
    std::cout<<"authored_view_check ok\n";
}
