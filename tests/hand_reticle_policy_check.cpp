// Host check of src/hand_reticle_policy.hpp (which crosshairs show when a hand holds the sword / is busy).
// cl /nologo /std:c++17 /EHsc /W4 /I. tests\hand_reticle_policy_check.cpp && hand_reticle_policy_check.exe
#include "src/hand_reticle_policy.hpp"
#include <cstdio>
#include <cstring>

using d2vr::ReticleInputs;
using d2vr::ReticlePolicy;
static int failures=0;
#define EXPECT(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

namespace {
ReticleInputs base(bool pinned=true) {
    ReticleInputs in{};
    in.copy_at[0]=in.copy_at[1]=true;in.voltaic_arms=3;in.aim_hand=0;in.pinned=pinned;
    return in;
}
}

int main() {
    {   // ring_hidden_while_sword_held
        ReticlePolicy p;auto in=base();p.update(in,0);
        EXPECT(p.ring_visible(0) && p.ring_visible(1));
        in.sword_held[1]=true;p.update(in,10);
        EXPECT(p.ring_visible(0) && !p.ring_visible(1));
        p.update(in,5000);
        EXPECT(!p.ring_visible(1));
    }
    {   // ring_release_hysteresis: back only >=150 ms after release; 40 ms flutter never shows it
        ReticlePolicy p;auto in=base();in.sword_held[1]=true;p.update(in,0);
        in.sword_held[1]=false;p.update(in,1000);
        EXPECT(!p.ring_visible(1));
        p.update(in,1100);EXPECT(!p.ring_visible(1));
        p.update(in,1149);EXPECT(!p.ring_visible(1));
        p.update(in,1150);EXPECT(p.ring_visible(1));
        // flutter: held, 40 ms open, held again
        ReticlePolicy q;in.sword_held[1]=true;q.update(in,0);
        in.sword_held[1]=false;q.update(in,100);EXPECT(!q.ring_visible(1));
        q.update(in,139);EXPECT(!q.ring_visible(1));
        in.sword_held[1]=true;q.update(in,140);EXPECT(!q.ring_visible(1));
        in.sword_held[1]=false;q.update(in,141);
        q.update(in,180);EXPECT(!q.ring_visible(1));
        q.update(in,290);EXPECT(!q.ring_visible(1));
        q.update(in,291);EXPECT(q.ring_visible(1));
    }
    {   // main_hidden_when_aim_hand_holds_sword
        ReticlePolicy p;auto in=base();p.update(in,0);
        EXPECT(p.main_visible() && p.main_blocked_reason()==nullptr);
        in.sword_held[0]=true;p.update(in,10);
        EXPECT(!p.main_visible() && std::strcmp(p.main_blocked_reason(),"sword")==0);
        in.sword_held[0]=false;in.sword_held[1]=true;p.update(in,20);
        EXPECT(p.main_visible());   // sword in the other hand: the aim hand's gun still aims
    }
    {   // main_hidden_when_engine_gun_hidden
        ReticlePolicy p;auto in=base();in.voltaic_arms=2;p.update(in,0);
        EXPECT(!p.main_visible() && std::strcmp(p.main_blocked_reason(),"gun_hidden")==0);
        in.voltaic_arms=3;p.update(in,10);EXPECT(p.main_visible());
    }
    {   // busy_hand_hides_ring
        ReticlePolicy p;auto in=base();in.busy[0]=true;p.update(in,0);
        EXPECT(!p.ring_visible(0) && p.ring_visible(1));
        in.busy[0]=false;p.update(in,50);EXPECT(!p.ring_visible(0));
        p.update(in,200);EXPECT(p.ring_visible(0));
    }
    {   // d2_ring_unchanged_without_sword
        ReticlePolicy p;auto in=base(false);in.voltaic_arms=0;p.update(in,0);
        EXPECT(p.ring_visible(0) && p.ring_visible(1) && p.main_visible());
    }
    {   // d2_main_never_hidden
        ReticlePolicy p;auto in=base(false);in.voltaic_arms=0;in.sword_held[0]=true;in.sword_held[1]=true;p.update(in,0);
        EXPECT(p.main_visible() && p.main_blocked_reason()==nullptr);
    }
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("hand_reticle_policy_check ok\n");
    return 0;
}
