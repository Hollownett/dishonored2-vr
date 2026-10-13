#define _USE_MATH_DEFINES
#include "../src/hud_window.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
static TrackedPose head(double yaw_deg,double x=0,double pitch_deg=0,double roll_deg=0) {
    const double y=yaw_deg*M_PI/180,p=pitch_deg*M_PI/180,r=roll_deg*M_PI/180;
    const Quaternion qy{0,std::sin(y/2),0,std::cos(y/2)},qp{std::sin(p/2),0,0,std::cos(p/2)},qr{0,0,std::sin(r/2),std::cos(r/2)};
    return {{x,1.7,0},multiplied(multiplied(qy,qp),qr)};
}
int main() {
    HudWindow w;
    auto a=w.update(head(0),.016);
    assert(std::fabs(a.position.z+1.3*std::cos(-d2vr::HudWindowConfig{}.pitch_deg*M_PI/180))<1e-9 && std::fabs(a.position.y-(1.7-1.3*std::sin(-d2vr::HudWindowConfig{}.pitch_deg*M_PI/180)))<1e-9);
    assert(std::hypot(a.position.x,a.position.y-1.7,a.position.z+0)>1.29 && std::hypot(a.position.x,a.position.y-1.7,a.position.z)<1.31);
    // Pitching/rolling the head never moves or rolls the panel, and does not recentre.
    auto b=w.update(head(0,0,-60,25),.016);
    assert(!w.following() && b.position.x==a.position.x && b.orientation.x==0 && b.orientation.z==0);
    // 30 deg turn: stays put. 40 deg: starts following, converges to the new heading.
    auto c=w.update(head(30),.016);assert(!w.following() && c.position.x==a.position.x);
    w.update(head(40),.016);assert(w.following());
    TrackedPose p{};for(int i=0;i<300;++i)p=w.update(head(40),.016);
    assert(!w.following());
    const double yaw=40*M_PI/180;assert(std::fabs(p.position.x+std::sin(yaw)*1.3*std::cos(-d2vr::HudWindowConfig{}.pitch_deg*M_PI/180))<.06);
    // Shortest way round the +-180 seam.
    HudWindow s;s.update(head(170),.016);s.update(head(-150),.016);assert(s.following());
    for(int i=0;i<300;++i)p=s.update(head(-150),.016);assert(!s.following());
    // Walking away recentres the panel.
    HudWindow m;m.update(head(0),.016);m.update(head(0,.5),.016);assert(m.following());
    for(int i=0;i<400;++i)p=m.update(head(0,.5),.016);assert(std::fabs(p.position.x-.5)<.05);
    // Facing straight up keeps heading (no spin).
    HudWindow u;auto before=u.update(head(0),.016);auto up=u.update(head(0,0,89.9),.016);assert(up.position.x==before.position.x);
    // FOV calibration: content width subtends 38 deg at the panel distance for any texture size / hud scale.
    for(const unsigned tw:{1920u,2560u}) for(const double k:{.5,.65,.8}) {
        const auto cr=hud_window_crop(tw,tw*9/16,k,1.3);
        assert(std::fabs(2*std::atan(cr.width_m/2/1.3)*180/M_PI-38)<1e-6);
        assert(std::fabs(cr.w-tw*k*1.1)<2 && std::abs(cr.x*2+cr.w-static_cast<int>(tw))<=1 && cr.y>=0 && cr.h<=static_cast<int>(tw*9/16));
        assert(std::fabs(cr.height_m/cr.width_m-double(cr.h)/cr.w)<1e-9);
    }
    {const auto cr=hud_window_crop(1920,1080,.65,1.3,52); // enlarged panel: 52 deg wide, ~2x apparent element size vs 38 deg @ 1.0x
     assert(std::fabs(2*std::atan(cr.width_m/2/1.3)*180/M_PI-52)<1e-6);
     assert(std::fabs((1.46*52)/(38.)-2.)<.02);}
    assert(hud_window_crop(1920,1080,1.,1.3).w==1920); // no relayout: whole band
    std::cout<<"hud_window_check ok\n";
}
