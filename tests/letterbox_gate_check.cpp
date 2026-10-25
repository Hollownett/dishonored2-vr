#include "../src/letterbox_gate.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
int main() {
    using namespace d2vr;
    const LetterboxDraw bar{39,1,true,false};
    {   LetterboxGate g;
        expect(g.skip(true,1,bar) && g.skip(true,1,bar),"both bars of a frame are skipped");
        expect(!g.skip(true,1,bar),"a third matching draw in the frame is not");
        expect(g.skip(true,2,bar),"next frame starts again");
    }
    {   // the cap is per eye pass: a UI drawn once per eye in one frame loses its bars in both eyes
        LetterboxGate g;
        g.begin_pass();expect(g.skip(true,7,bar) && g.skip(true,7,bar) && !g.skip(true,7,bar),"eye 0: two bars, then capped");
        g.begin_pass();expect(g.skip(true,7,bar) && g.skip(true,7,bar) && !g.skip(true,7,bar),"eye 1 (same frame): two bars again");
    }
    {   LetterboxGate g;
        expect(!g.skip(false,1,bar),"inactive (not an immersive cutscene, or the knob): never");
        expect(!g.skip(true,1,{38,1,true,false}) && !g.skip(true,1,{40,1,true,false}),"only 39 indices");
        expect(!g.skip(true,1,{39,2,true,false}),"instanced draws are not bars");
        expect(!g.skip(true,1,{39,1,false,false}),"only triangle lists");
        expect(!g.skip(true,1,{39,1,true,true}),"a textured draw is a mesh, not a bar");
        expect(g.skip(true,1,bar),"and none of those used up the frame's budget");
    }
    std::printf(failures?"letterbox_gate_check FAILED\n":"letterbox_gate_check ok\n");
    return failures?1:0;
}
