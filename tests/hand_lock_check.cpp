// v1243 pure check of the hand-lock measurement (src/weapon_draw_place.hpp HandLock): a held thing's pose in the DRAWN hand's joint-59 frame.
// Player walking at 1.4 m/s and a moving hand, 90 Hz renders. (1) B from the image joint and every arm draw in step with the copy: 0.
// (2) the binding taken from a hand sampled one render earlier (a world target of an older sample) in every other render: velocity x age.
// (3) B from the image but the arm draw composed from the previous tick's arm model frame in every other render (a late re-sample between
// the composition and the GPU skin copy): the arm shift |E| and the thing's wobble in the drawn hand are player velocity x tick.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/weapon_draw_place.hpp"
using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static Mat rigid(double yaw,double x,double y,double z) {Mat m=identity();const double c=std::cos(yaw),s=std::sin(yaw);m[0]=c;m[1]=-s;m[4]=s;m[5]=c;m[3]=x;m[7]=y;m[11]=z;return m;}
static double field(const std::string& j,const char* kind,const char* cell,const char* what) {   // "kind":{..."cell":{"mm":{..."max":X
    auto k=j.find(std::string("\"")+kind+"\":{");if(k==std::string::npos) return -1;
    auto c=j.find(std::string("\"")+cell+"\":{",k);if(c==std::string::npos) return -1;
    auto w=j.find(std::string("\"")+what+"\":{",c);if(w==std::string::npos) return -1;
    return std::strtod(j.c_str()+j.find("\"max\":",w)+6,nullptr);
}
static double arm_max(const std::string& j,const char* speed) {
    auto a=j.find(std::string("\"")+speed+"\":{",j.find("arm_e_mm"));return std::strtod(j.c_str()+j.find("\"max\":",a)+6,nullptr);
}
int main() {
    const double v=1.4,dt=1./90,hand_v=.3;   // player m/s, render s, hand m/s along y in the model frame
    const Mat g=rigid(.4,.05,-.02,.08);       // grip -> thing, rigid for the hold
    const auto f_at=[&](int k) {return rigid(0,v*k*dt,0,0);};                                        // the arm model's frame (moves with the player)
    const auto j_at=[&](int k) {return mul(f_at(k),rigid(.2+.5*k*dt,.4,.2+hand_v*k*dt,1.2));};     // the image joint 59 (world)
    // (1)+(2): hand 0 binds from the image (kind prop), hand 1 from a one-render-old hand sample in every other render (kind use)
    HandLock::clear();
    for(int k=1;k<=60;++k) {
        const double t=k*dt;const auto f=f_at(k);
        HandLock::note_copy(0,static_cast<std::uint32_t>(k),f);HandLock::note_copy(1,static_cast<std::uint32_t>(k),f);
        HandLock::note_publish(0,0,static_cast<std::uint32_t>(k),Place::kind_prop,0x100,j_at(k),mul(j_at(k),g),v,.1,t);
        HandLock::note_publish(1,1,static_cast<std::uint32_t>(k),Place::kind_use,0x200,j_at(k),mul(j_at(k&1?k-1:k),g),v,.1,t);
        HandLock::on_draw(static_cast<std::uint32_t>(k),f);   // the arm draw, composed from the copy's frame
        HandLock::on_draw(static_cast<std::uint32_t>(k),f);   // a second draw of the render: sampled once
    }
    std::string j=HandLock::json();
    const double image_mm=field(j,"prop","walk_slow","mm"),old_mm=field(j,"use","walk_slow","mm");
    const double step=std::hypot(v*dt,hand_v*dt)*1000.;   // the joint's world motion over one render
    std::printf("image binding %.4f mm, one-render-old binding %.3f mm (joint step %.3f mm)\n",image_mm,old_mm,step);
    CHECK(image_mm>=0 && image_mm<1e-6);
    CHECK(std::abs(old_mm-step)<.2*step);
    CHECK(arm_max(j,"walk")<1e-6);
    // (3): image binding, but every other arm draw was composed from the previous tick's arm model frame
    HandLock::clear();
    for(int k=1;k<=60;++k) {
        const double t=k*dt;
        HandLock::note_copy(0,static_cast<std::uint32_t>(k),f_at(k));
        HandLock::note_publish(0,0,static_cast<std::uint32_t>(k),Place::kind_severed,0x300,j_at(k),mul(j_at(k),g),v,.1,t);
        HandLock::on_draw(static_cast<std::uint32_t>(k),f_at(k&1?k-1:k));
    }
    j=HandLock::json();
    const double late_mm=field(j,"severed","walk_slow","mm"),arm_mm=arm_max(j,"walk");
    std::printf("late arm composition: arm shift %.3f mm, thing in drawn hand %.3f mm (player step %.3f mm)\n",arm_mm,late_mm,v*dt*1000.);
    CHECK(std::abs(arm_mm-v*dt*1000.)<.01);
    CHECK(std::abs(late_mm-v*dt*1000.)<.01);
    // a draw far from the arm frame (another object) is not an arm draw; a render of another frame is not sampled
    HandLock::clear();
    HandLock::note_publish(0,0,4,Place::kind_prop,0x100,j_at(4),mul(j_at(4),g),0,0,1.);
    HandLock::note_copy(0,5,f_at(5));HandLock::note_publish(0,0,5,Place::kind_prop,0x100,j_at(5),mul(j_at(5),g),0,0,1.2);
    HandLock::on_draw(5,f_at(5));HandLock::note_publish(0,0,6,Place::kind_prop,0x100,j_at(6),mul(j_at(6),g),0,0,1.21);HandLock::note_copy(0,6,f_at(6));   // reference taken
    HandLock::on_draw(6,rigid(0,3,0,0));HandLock::on_draw(7,f_at(6));
    j=HandLock::json();
    CHECK(j.find("\"still_slow\":{\"mm\":{\"n\":0,")!=std::string::npos && j.find("\"still\":{\"n\":1,")!=std::string::npos);   // only the reference render
    std::printf(failures?"hand_lock_check: %d failure(s)\n":"hand_lock_check: all passed\n",failures);
    return failures?1:0;
}
