// Host check of src/teleport_fade.hpp (plan 2026-11-24-doto-billie-vr Task 12: a short black fade over a Displace swap or a Foresight exit).
// cl /nologo /std:c++17 /EHsc /W4 /I. tests\teleport_fade_check.cpp && teleport_fade_check.exe
#include "src/teleport_fade.hpp"
#include <cstdio>

using d2vr::TeleportFade;
static int failures=0;
#define EXPECT(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

int main() {
    {   // fade_shape: 0 before the edge, ramps in over 30 %, holds 1 to 70 %, ramps out, 0 after the window
        TeleportFade f;EXPECT(f.alpha(1000)==0.f);
        f.edge(1000,200);
        EXPECT(f.alpha(1000)==0.f && f.fades()==1);
        EXPECT(f.alpha(1030)>.45f && f.alpha(1030)<.55f);          // half way up the 60 ms ramp
        EXPECT(f.alpha(1080)>.99f && f.alpha(1080)<=1.f);          // 40 % of the window
        EXPECT(f.alpha(1140)>.99f);                                  // 70 %: still held
        EXPECT(f.alpha(1170)>.45f && f.alpha(1170)<.55f);          // half way down
        EXPECT(f.alpha(1200)==0.f && f.alpha(5000)==0.f);
        EXPECT(f.alpha(900)==0.f);                                   // a clock before the edge
    }
    {   // fade_window_expires: no edge for 10 s -> 0
        TeleportFade f;f.edge(0,120);EXPECT(f.alpha(10000)==0.f);
        TeleportFade g;EXPECT(g.alpha(10000)==0.f && g.fades()==0);
    }
    {   // retrigger_extends_not_stacks: a second edge mid-window never exceeds 1 and extends the end without restarting the ramp-in
        TeleportFade f;f.edge(0,200);
        const float before=f.alpha(100);EXPECT(before>.99f);
        f.edge(100,200);                                              // new end 300
        EXPECT(f.alpha(100)>.99f && f.alpha(100)<=1.f);               // no restart: still full
        for(int t=0;t<=400;t+=5) EXPECT(f.alpha(static_cast<std::uint64_t>(t))<=1.f && f.alpha(static_cast<std::uint64_t>(t))>=0.f);
        EXPECT(f.alpha(250)>0.f && f.alpha(300)==0.f && f.fades()==2);
        TeleportFade g;g.edge(0,200);g.edge(50,100);                  // a shorter one never shortens the window
        EXPECT(g.alpha(190)>0.f && g.alpha(200)==0.f);
    }
    {   // zero_duration_ignored
        TeleportFade f;f.edge(10,0);f.edge(10,-5);
        EXPECT(f.alpha(10)==0.f && f.alpha(20)==0.f && f.fades()==0);
    }
    {   // edge_detection: swapper rising = 120 ms, Foresight 3/4 -> 5/0 = 220 ms, anything else (or an invalid sample) = none
        using d2vr::TeleportSample;using d2vr::teleport_fade_duration_ms;
        const TeleportSample idle{true,false,-1},swap{true,true,-1};
        EXPECT(teleport_fade_duration_ms(idle,swap)==120 && teleport_fade_duration_ms(swap,swap)==0 && teleport_fade_duration_ms(swap,idle)==0);
        EXPECT(teleport_fade_duration_ms({true,false,3},{true,false,5})==220 && teleport_fade_duration_ms({true,false,4},{true,false,0})==220);
        EXPECT(teleport_fade_duration_ms({true,false,1},{true,false,3})==0 && teleport_fade_duration_ms({true,false,0},{true,false,5})==0);
        EXPECT(teleport_fade_duration_ms({false,false,3},{true,false,5})==0 && teleport_fade_duration_ms(idle,{false,true,-1})==0);
    }
    if(failures) {std::printf("teleport_fade_check: %d failure(s)\n",failures);return 1;}
    std::printf("teleport_fade_check: ok\n");
    return 0;
}
