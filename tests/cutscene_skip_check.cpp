#include "../src/cutscene_skip.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
int main() {
    using d2vr::SkipHold;
    {   // a short press passes through untouched; the hold injects Interact and masks A/X; release stops it
        SkipHold s;
        for(std::uint64_t t=0;t<590;t+=10) {const auto o=s.update(true,true,false,t);expect(!o.inject_x && !o.mask_a && !o.mask_x,"before 0.6 s nothing changes");}
        auto o=s.update(true,true,false,600);expect(o.inject_x && o.mask_a && o.mask_x && s.injecting(),"0.6 s: skip held");
        o=s.update(true,true,false,2500);expect(o.inject_x,"held for the whole ring");
        o=s.update(true,false,false,2510);expect(!o.inject_x && !o.mask_a && !s.injecting(),"release stops it");
        expect(s.holds()==1,"one hold counted");
    }
    {   // X works too, A and X together do not restart the timer
        SkipHold s;s.update(true,false,true,0);s.update(true,true,true,300);
        expect(s.update(true,true,true,650).inject_x,"X then A+X still counts from the first down");
    }
    {   // not a cutscene: nothing, ever
        SkipHold s;for(std::uint64_t t=0;t<3000;t+=50) {const auto o=s.update(false,true,true,t);expect(!o.inject_x && !o.mask_a && !o.mask_x,"no context, no change");}
    }
    {   // the scene ends while the skip is held: no inject, masked until released, then free again
        SkipHold s;s.update(true,true,false,0);s.update(true,true,false,700);
        auto o=s.update(false,true,false,720);expect(!o.inject_x && o.mask_a && o.mask_x,"scene ended: masked");
        o=s.update(false,true,false,5000);expect(o.mask_a && !o.inject_x,"still masked");
        o=s.update(true,true,false,5100);expect(o.mask_a && !o.inject_x,"a new cutscene does not re-arm under the same press");
        o=s.update(false,false,false,5200);expect(!o.mask_a,"released");
        o=s.update(false,true,false,5300);expect(!o.mask_a,"a fresh press afterwards is normal");
        expect(s.leaks_stopped()==1,"counted");
    }
    {   // the scene ends under a press that began inside it but is not yet 0.6 s old: latched until released, never leaks
        SkipHold s;s.update(true,false,true,0);
        auto o=s.update(false,false,true,200);expect(!o.inject_x && o.mask_x && o.mask_a,"short press, scene ended: masked");
        o=s.update(false,false,true,900);expect(o.mask_x,"still masked past 0.6 s");
        o=s.update(true,false,true,1500);expect(o.mask_x && !o.inject_x,"a new scene does not re-arm it");
        o=s.update(false,false,false,1600);expect(!o.mask_x,"released");
        expect(s.leaks_stopped()==1,"counted once");
        // a press that began outside any scene is never latched by the scene ending
        SkipHold t;t.update(false,true,false,0);auto p=t.update(false,true,false,50);expect(!p.mask_a,"outside press untouched");
    }
    {   // a press that began before the scene (the confirm that opened it) is not a skip
        SkipHold s;s.update(false,true,false,0);
        auto o=s.update(true,true,false,100);expect(!o.inject_x,"short");
        o=s.update(true,true,false,2000);expect(!o.inject_x && !o.mask_a,"a press already down when the scene starts never skips it");
        s.update(true,false,false,2100);s.update(true,true,false,2200);o=s.update(true,true,false,2800);expect(o.inject_x,"the next press does");
    }
    std::printf(failures?"cutscene_skip_check FAILED\n":"cutscene_skip_check ok\n");
    return failures?1:0;
}
