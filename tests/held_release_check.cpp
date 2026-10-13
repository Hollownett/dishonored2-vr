#include "../src/held_release.hpp"
#include <cstdio>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    using d2vr::HeldRelease;const HeldRelease::V still{},fast{0,1,-3};
    {   // held grip: slow release drops
        HeldRelease h;std::uint64_t t=0;
        for(;t<1000;t+=11) expect(!h.update(true,.9f,still,0,t).drop,"held keeps");
        const auto o=h.update(true,.1f,still,0,t);expect(o.drop && !o.toss,"slow release drops");
        expect(!h.update(true,.1f,still,0,t+11).drop,"drops once");
    }
    {   // fast release throws with the recent peak velocity, even as the hand slows at the release
        HeldRelease h;std::uint64_t t=0;
        for(;t<600;t+=11) h.update(true,.9f,still,0,t);
        h.update(true,.9f,fast,3.2,t);t+=11;
        const auto o=h.update(true,.1f,{0,0,-.5},.5,t+30);
        expect(o.toss && !o.drop && o.velocity[2]==-3,"fast release throws along the hand");
    }
    {   // click pickup stays in hand; the next squeeze and release drops
        HeldRelease h;
        expect(!h.update(true,.9f,still,0,0).drop,"pickup");
        expect(!h.update(true,.1f,still,0,150).drop,"click release keeps it");
        expect(!h.update(true,.1f,still,0,2000).drop,"stays without grip");
        h.update(true,.9f,still,0,2100);
        expect(h.update(true,.1f,still,0,2200).drop,"next release drops");
    }
    {   // pickup without the grip held (e.g. Use from the other hand): first squeeze then release lets go
        HeldRelease h;
        expect(!h.update(true,0,still,0,0).drop,"no grip at start");
        h.update(true,.9f,still,0,500);
        expect(h.update(true,.1f,still,0,800).drop,"release drops");
        expect(!h.update(false,0,still,0,900).drop && !h.update(true,0,still,0,1000).drop,"new carry rearms cleanly");
    }
    if(!failures) std::printf("PASS: held object drop/throw on grip release, click pickup sticky, throw uses recent peak\n");
    return failures?1:0;
}
