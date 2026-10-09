#include "../src/menu_chord.hpp"
#include "../src/menu_tap.hpp"
#include "../src/turn_control.hpp"
#include <cassert>
#include <limits>
#include <utility>

int main() {
    using namespace d2vr;
    MenuChord chord;MenuTap tap;
    auto sample=[&](bool held,bool active,bool game,float x,float y,std::uint64_t t) {
        const auto out=chord.update(held,active,game,x,y,t);
        const bool pause=tap.update(held,active && !out.consumed,t);
        return std::pair<MenuChordOutput,bool>{out,pause};
    };
    sample(false,true,true,0,0,1000);
    assert(!sample(true,true,true,0,0,1010).second);
    assert(sample(false,true,true,0,0,1050).second); // Existing short tap.
    assert(sample(false,true,true,0,0,1199).second);
    assert(!sample(false,true,true,0,0,1200).second);
    sample(true,true,true,0,0,1250);
    for(unsigned t=1350;t<1900;t+=100) assert(!sample(true,true,true,0,0,t).first.back);
    auto out=sample(true,true,true,0,0,1900);assert(out.first.back && out.first.consumed && out.first.menu_modifier && !out.second);
    assert(sample(false,true,true,0,0,1910).first.back && !sample(false,true,true,0,0,1911).second);
    assert(!sample(false,true,true,0,0,2050).first.back);
    // A fresh short press after a handled release remains available.
    sample(true,true,true,0,0,2060);assert(sample(false,true,true,0,0,2070).second);
    chord={};tap={};sample(false,true,true,0,0,3000);
    sample(true,true,true,.9f,0,3010); // Held direction at modifier entry ignored.
    assert(!sample(true,true,true,.9f,0,3020).first.dpad[0]);
    sample(true,true,true,0,0,3030);
    out=sample(true,true,true,.8f,.8f,3040);assert(out.first.dpad[0] && out.first.consumed); // Tie is horizontal.
    assert(!sample(true,true,true,.8f,.8f,3190).first.dpad[0]); // No held repeats.
    sample(true,true,true,.29f,0,3200);
    out=sample(true,true,true,.2f,.9f,3210);assert(out.first.dpad[1] && !out.first.dpad[0]);
    for(unsigned t=3300;t<=3800;t+=100) assert(!sample(true,true,true,0,0,t).first.back); // DPAD blocks Journal for press.
    sample(true,true,true,0,0,3810);assert(sample(true,true,true,-.9f,.1f,3820).first.dpad[2]);
    sample(true,true,true,0,0,3830);assert(sample(true,true,true,0,-.9f,3840).first.dpad[3]);
    assert(!sample(false,true,true,0,0,3850).second);
    chord={};tap={};sample(false,true,true,0,0,3900);sample(true,true,true,0,0,3910);
    assert(sample(true,true,true,.9f,0,3920).first.dpad[0]);
    assert(!sample(true,true,true,0,0,3930).first.dpad[0]); // Native release edge before same direction repeats.
    assert(sample(true,true,true,.9f,0,3940).first.dpad[0]);
    // Nongameplay retains Pause tap, but never emits Journal/shortcuts.
    chord={};tap={};sample(false,true,false,0,0,4000);sample(true,true,false,0,0,4010);
    for(unsigned t=4110;t<4810;t+=100) {out=sample(true,true,false,.9f,0,t);assert(!out.first.back && !out.first.dpad[0]);}
    assert(sample(false,true,false,0,0,4810).second);
    // Focus, context, stale input and backwards clocks require release.
    for(unsigned mode=0;mode<4;++mode) {
        chord={};tap={};sample(false,true,true,0,0,5000);sample(true,true,true,0,0,5010);
        bool game=true;std::uint64_t t=5100;
        if(mode==0) sample(true,false,true,0,0,t);
        if(mode==1) {game=false;sample(true,true,false,0,0,t);}
        if(mode==2) {t=5400;sample(true,true,true,0,0,t);}
        if(mode==3) {t=4900;sample(true,true,true,0,0,t);}
        for(unsigned n=1;n<9;++n) {out=sample(true,true,game,.9f,0,t+n*100);assert(!out.first.back && !out.first.dpad[0] && !out.second);}
        assert(!sample(false,true,game,0,0,t+900).second);
        sample(true,true,game,0,0,t+910);assert(sample(false,true,game,0,0,t+920).second);
    }
    chord={};tap={};sample(false,true,true,0,0,7000);sample(true,true,true,0,0,7010);
    out=sample(true,true,true,std::numeric_limits<float>::quiet_NaN(),0,7020);assert(out.first.consumed && !out.first.back);
    assert(!sample(false,true,true,0,0,7030).second);
    // Released committed pulse cancels on context transition, not a held auto-action.
    sample(true,true,true,0,0,7100);sample(true,true,true,0,0,7300);sample(true,true,true,0,0,7500);sample(true,true,true,0,0,7700);
    assert(sample(true,true,true,0,0,7750).first.back);
    assert(!sample(false,true,false,0,0,7760).first.back);
    TurnControl turning;turning.configure({TurnControl::Mode::snap,45,90});
    turning.publish(0,true,true,true,true,8000,1);
    turning.publish(0,true,false,true,true,8010,1); // Modifier interval cancels, rather than synthetic centering.
    turning.publish(.9,true,true,true,true,8020,1);
    assert(!turning.consume(.016,true,8021,1,1)); // Held flick on modifier exit cannot snap.
    turning.publish(0,true,true,true,true,8030,1);
    turning.publish(.9,true,true,true,true,8040,1);
    assert(turning.consume(.016,true,8041,1,2));
    MenuChord disconnected;
    disconnected.update(false,true,true,0,0,9000,false);
    disconnected.update(true,true,true,0,0,9010,false); // Inactive stick is not a center observation.
    assert(!disconnected.update(true,true,true,.9f,0,9020,true).dpad[0]);
    disconnected.update(true,true,true,0,0,9030,true);
    assert(disconnected.update(true,true,true,.9f,0,9040,true).dpad[0]);
    assert(!disconnected.update(true,true,true,0,0,9050,false).dpad[0]);
}
