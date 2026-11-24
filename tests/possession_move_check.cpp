// Host check of src/possession_move.hpp (plan 2026-11-24-doto-billie-vr Task 15b: D2 possession moves where the player looks).
// cl /nologo /std:c++17 /EHsc /W4 /I. tests\possession_move_check.cpp && possession_move_check.exe
#include "src/possession_move.hpp"
#include <cmath>
#include <cstdio>

using d2vr::PossessionGate;
using d2vr::PossessionMove;
static int failures=0;
#define EXPECT(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static const double pi=3.14159265358979323846;
static double rad(double deg) {return deg*pi/180.;}

int main() {
    {   // forward_follows_view: positive yaw turns left (tracked_yaw), so a view 60 deg right of the host is -60 deg
        float x=0,y=1;PossessionMove::orient(x,y,rad(-60),0);
        EXPECT(std::abs(x-std::sin(rad(60)))<1e-5 && std::abs(y-std::cos(rad(60)))<1e-5);
        float lx=0,ly=1;PossessionMove::orient(lx,ly,rad(60),0);   // 60 deg left: x negative
        EXPECT(std::abs(lx+std::sin(rad(60)))<1e-5 && std::abs(ly-std::cos(rad(60)))<1e-5);
        float sx=1,sy=0;PossessionMove::orient(sx,sy,rad(-90),0);  // strafe right with the view 90 right: moves backwards
        EXPECT(std::abs(sx)<1e-5 && std::abs(sy+1)<1e-5);
    }
    {   // no_rotation_when_aligned
        float x=.3f,y=.8f;PossessionMove::orient(x,y,rad(25),rad(25));
        EXPECT(x==.3f || std::abs(x-.3f)<1e-6);EXPECT(std::abs(y-.8f)<1e-6);
        EXPECT(PossessionMove::rotation(0,0)==0);
    }
    {   // wraps_across_pi: view 170, host -170 -> rotate by -20 (not +340)
        EXPECT(std::abs(PossessionMove::rotation(rad(170),rad(-170))-rad(-20))<1e-9);
        EXPECT(std::abs(PossessionMove::rotation(rad(-170),rad(170))-rad(20))<1e-9);
        EXPECT(PossessionMove::rotation(rad(179),rad(-179))>-pi && PossessionMove::rotation(rad(179),rad(-179))<=pi);
        float x=0,y=1;PossessionMove::orient(x,y,rad(170),rad(-170));
        EXPECT(std::abs(x-std::sin(rad(20)))<1e-5 && std::abs(y-std::cos(rad(20)))<1e-5);   // -20 deg = 20 right
    }
    {   // gate_exit_hold: on at once, off only after 300 ms without the bit; a 100 ms dropout does not exit
        PossessionGate g;
        EXPECT(!g.update(false,1000) && g.entries()==0);
        EXPECT(g.update(true,1050) && g.entries()==1);
        EXPECT(g.update(false,1100) && g.update(false,1150));     // 100 ms: still on
        EXPECT(g.update(true,1160) && g.entries()==1);            // dropout over, no new entry
        EXPECT(g.update(false,1200) && g.update(false,1499));     // 299 ms
        EXPECT(!g.update(false,1500));                            // 300 ms: off
        EXPECT(!g.update(false,1600));
        EXPECT(g.update(true,2000) && g.entries()==2);
    }
    {   // outside_possession_unchanged: a gate that never saw the bit leaves the stick alone; NaN view/host passes through
        PossessionGate g;float x=.4f,y=-.7f;
        if(g.update(false,10)) PossessionMove::orient(x,y,rad(80),0);
        EXPECT(x==.4f && y==-.7f);
        float a=.4f,b=-.7f;PossessionMove::orient(a,b,NAN,0);EXPECT(a==.4f && b==-.7f);
        PossessionMove::orient(a,b,0,NAN);EXPECT(a==.4f && b==-.7f);
    }
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("possession_move_check: ok\n");
    return 0;
}
