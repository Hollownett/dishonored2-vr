// Host check of src/voltaic_arms.hpp (DOTO dual wield: which forearms carry the Voltaic Gun).
// cl /nologo /std:c++17 /EHsc /W4 /I. tests\voltaic_arms_check.cpp && voltaic_arms_check.exe
#include "src/voltaic_arms.hpp"
#include <cstdio>
#include <cstdlib>

using d2vr::VoltaicArms;
static int failures=0;
#define EXPECT(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

struct Sim {
    VoltaicArms v;std::uint64_t t=1000,rev=1;bool native=false;
    unsigned step(std::uint64_t ms=50) {unsigned a=0;for(std::uint64_t e=0;e<ms;e+=10) {t+=10;a=v.update(native,t,rev);}return a;}
    void commit(unsigned h) {v.commit(h,t,rev,native);}
    void equip_gun() {++rev;step(200);native=true;}
    void equip_other() {++rev;step(200);native=false;}
};

int main() {
    {   // right hand's wheel brings the gun: right only
        Sim s;s.commit(1);s.step(100);s.equip_gun();EXPECT(s.step(100)==2u);EXPECT(s.v.take_arrival()==2u);EXPECT(s.v.take_gain()==2u);
        // left chooses the same gun: both
        s.commit(0);EXPECT(s.step(1000)==3u);
        // left chooses a power: the right keeps its copy
        s.commit(0);s.step(100);s.equip_other();EXPECT(s.step(100)==2u);EXPECT(s.v.keeps==1);
        // the right (copy) chooses another power: its copy goes
        s.commit(1);s.step(100);s.equip_other();EXPECT(s.step(2000)==0u);
    }
    {   // left brings it, right chooses a power: the left keeps its gun (now a copy)
        Sim s;s.commit(0);s.equip_gun();EXPECT(s.step()==1u);
        s.commit(1);s.step(100);s.equip_other();EXPECT(s.step(100)==1u);
    }
    {   // a copy hand chooses the gun itself, its equip slower than the displacement test: it keeps (gets back) the gun
        Sim s;s.commit(0);s.equip_gun();s.step();s.commit(1);s.step(1000);EXPECT(s.step()==3u);   // both
        s.commit(0);s.equip_other();EXPECT(s.step()==2u);                                          // left took a power, right copy
        s.commit(1);++s.rev;s.step(1800);s.native=true;EXPECT(s.step()==2u);                       // right chose the gun again, slow equip
        EXPECT(s.v.native);
    }
    {   // the game puts it away (a carry) and gives it back: the same arms
        Sim s;s.commit(1);s.equip_gun();EXPECT(s.step()==2u);
        s.step(5000);s.equip_other();EXPECT(s.step()==0u);s.step(3000);s.equip_gun();EXPECT(s.step()==2u);
    }
    {   // restore: the left held a power, the right chose the gun; the caller re-equips the power, the right keeps its copy
        Sim s;s.commit(1);s.equip_gun();EXPECT(s.step()==2u);s.v.restore(s.t);s.equip_other();EXPECT(s.step(200)==2u);
    }
    {   // the exact same-item hook
        Sim s;s.commit(0);s.equip_gun();s.step();s.commit(1);s.v.same(1);EXPECT(s.step()==3u);
    }
    {   // a gun equipped with no wheel at first (a save load): the left
        Sim s;s.native=true;EXPECT(s.step()==1u);
    }
    {   // the charge: plain without the upgrade (fires while pulled), with it: tap = plain on release, 2 s = charged on release, 4 s = overcharged by itself
        using d2vr::VoltaicCharge;VoltaicCharge c;
        EXPECT(c.step(true,0,false).fire);EXPECT(!c.step(false,10,false).fire);
        EXPECT(!c.step(true,100,true).fire);auto s=c.step(false,500,true);EXPECT(s.fire && !s.charged);
        c.step(true,1000,true);s=c.step(true,3000,true);EXPECT(s.charge_started && s.charging && !s.fire);s=c.step(false,3500,true);EXPECT(s.fire && s.charged && !s.overcharged);
        c.step(true,4000,true);c.step(true,6000,true);s=c.step(true,8000,true);EXPECT(s.fire && s.charged && s.overcharged);
        s=c.step(true,8100,true);EXPECT(!s.fire);s=c.step(false,8200,true);EXPECT(!s.fire);   // spent: nothing on release
    }
    std::printf(failures?"voltaic_arms_check: %d failure(s)\n":"voltaic_arms_check: ok\n",failures);
    return failures?1:0;
}
