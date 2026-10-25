#include "../src/cutscene_policy.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace d2vr;
static CutsceneFrame good(unsigned logic=32){return {logic,true,true,true,false};}
static CutsceneFrame unpaired(){return {32,false,true,true,false};}
int main(){
    static_assert(CutscenePolicy::entry_grace_frames==30,"entry grace");
    static_assert(CutscenePolicy::tolerated_misses==2,"Ruling 9: two held misses, the third falls back");
    static_assert(cinematic_logic_mask==32,"Cinematic is exact mask 32");
    {CutscenePolicy p; // exact_32_complete_pair_immersive
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::none && p.active() && !p.pending() && p.cutscenes()==1);
     assert(p.immersive(good(),CutsceneMode::vr_full,false));}
    {CutscenePolicy p; // mixed_mask_rejected: 2|32, 4|32, 256|32, and never gameplay, loading, pause or menus
     for(unsigned logic:{34u,36u,288u,2u,0u,4u,16u,128u,32768u,33u}) assert(!p.immersive(good(logic),CutsceneMode::vr_comfort,false));
     assert(p.reason()==CutsceneFallback::none && !p.active() && p.cutscenes()==0);
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false));}
    {CutscenePolicy p; // theater_mode_never_immersive
     for(int i=0;i<5;++i) assert(!p.immersive(good(),CutsceneMode::theater,false));
     assert(p.reason()==CutsceneFallback::mode_theater && p.last_fallback()==CutsceneFallback::mode_theater && !p.pending());
     assert(!p.immersive(good(),CutsceneMode::vr_comfort,false)); // switching mode mid-cutscene: still theater until it ends
     p.end();assert(p.immersive(good(),CutsceneMode::vr_comfort,false));
     CutscenePolicy k; // the knob off
     assert(!k.immersive(good(),CutsceneMode::vr_full,true) && k.reason()==CutsceneFallback::knob_off);}
    {CutscenePolicy p; // fallback_sticks_until_exit
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false));
     for(int i=0;i<2;++i) assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.holding() && p.reason()==CutsceneFallback::none);
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::incomplete_pair && !p.holding()); // no entry grace once paired
     for(int i=0;i<50;++i) assert(!p.immersive(good(),CutsceneMode::vr_comfort,false));
     assert(p.reason()==CutsceneFallback::incomplete_pair && !p.pending());
     assert(!p.immersive(good(36),CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::incomplete_pair); // a mixed frame does not end it
     assert(!p.immersive(good(),CutsceneMode::vr_comfort,false) && p.cutscenes()==1);
     CutscenePolicy f; // fault and camera reasons stick too
     assert(f.immersive(good(),CutsceneMode::vr_comfort,false));
     assert(!f.immersive({32,true,true,true,true},CutsceneMode::vr_comfort,false) && f.reason()==CutsceneFallback::fault);
     assert(!f.immersive(good(),CutsceneMode::vr_comfort,false));
     CutscenePolicy c;assert(!c.immersive({32,true,true,false,false},CutsceneMode::vr_comfort,false) && c.reason()==CutsceneFallback::camera_unreadable);}
    {CutscenePolicy p; // end_resets
     assert(!p.immersive({32,true,false,true,false},CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::duplicate_eyes);
     p.end();assert(p.reason()==CutsceneFallback::none && !p.active() && p.last_fallback()==CutsceneFallback::duplicate_eyes);
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false) && p.cutscenes()==2);
     p.end();for(unsigned i=0;i<CutscenePolicy::entry_grace_frames;++i) assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false)); // the grace re-arms too
     assert(p.pending() && p.reason()==CutsceneFallback::none && p.immersive(good(),CutsceneMode::vr_comfort,false));}
    {CutscenePolicy p; // duplicate_eyes_rejected: one texture or one pose for both eyes is never stereo, not even during the entry grace
     assert(!p.immersive({32,true,false,true,false},CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::duplicate_eyes);
     assert(!p.immersive(good(),CutsceneMode::vr_comfort,false));}
    {CutscenePolicy p; // entry grace: pending (no fallback) for exactly 30 unpaired frames, then incomplete_pair
     for(unsigned i=0;i<CutscenePolicy::entry_grace_frames;++i) {assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false));assert(p.pending());}
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::incomplete_pair && !p.pending());
     CutscenePolicy q;for(unsigned i=0;i+1<CutscenePolicy::entry_grace_frames;++i) q.immersive(unpaired(),CutsceneMode::vr_comfort,false);
     assert(q.immersive(good(),CutsceneMode::vr_comfort,false)); // the 30th frame may still pair
     CutscenePolicy r;assert(r.immersive(good(),CutsceneMode::vr_comfort,false)); // an interruption (mixed mask) re-arms the grace, keeps the cutscene
     assert(!r.immersive(good(36),CutsceneMode::vr_comfort,false));
     for(int i=0;i<3;++i) assert(!r.immersive(unpaired(),CutsceneMode::vr_comfort,false));
     assert(r.pending() && r.immersive(good(),CutsceneMode::vr_comfort,false) && r.cutscenes()==1);}
    {CutscenePolicy p; // active_tolerates_two_misses_falls_back_on_third (Ruling 9)
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false));
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.holding() && !p.pending());
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false) && !p.holding()); // a good frame resets the run
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.holding());
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.holding() && p.reason()==CutsceneFallback::none);
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.reason()==CutsceneFallback::incomplete_pair && !p.holding());
     assert(!p.immersive(good(),CutsceneMode::vr_comfort,false) && p.last_fallback()==CutsceneFallback::incomplete_pair);
     CutscenePolicy d;assert(d.immersive(good(),CutsceneMode::vr_comfort,false)); // only a missing pair is held: duplicate eyes fall back at once
     assert(!d.immersive({32,true,false,true,false},CutsceneMode::vr_comfort,false) && d.reason()==CutsceneFallback::duplicate_eyes && !d.holding());}
    {CutscenePolicy p; // re-entry 32 -> 36 -> 32: the 36 frame is no cutscene frame; back on 32 the pair must form again (pending, not held)
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false));
     assert(!p.immersive(good(36),CutsceneMode::vr_comfort,false) && !p.holding() && p.pending() && p.active()); // the pair must form again
     assert(!p.immersive(unpaired(),CutsceneMode::vr_comfort,false) && p.pending() && !p.holding()); // shown black by the caller, never a replay
     assert(p.immersive(good(),CutsceneMode::vr_comfort,false) && p.cutscenes()==1);
     CutscenePolicy q; // 2 -> 34 -> 32: the mixed frames start nothing; the cutscene starts pending at its first exact frame
     assert(!q.immersive(good(2),CutsceneMode::vr_comfort,false) && !q.immersive(good(34),CutsceneMode::vr_comfort,false) && !q.active());
     assert(!q.immersive(unpaired(),CutsceneMode::vr_comfort,false) && q.pending() && q.active() && q.cutscenes()==1);
     assert(q.immersive(good(),CutsceneMode::vr_comfort,false));}
    {CutscenePolicy p; // reject: a session that can never pair goes to the theater at once
     p.reject(CutsceneFallback::incomplete_pair);assert(p.reason()==CutsceneFallback::incomplete_pair && p.active() && p.cutscenes()==1);
     assert(!p.immersive(good(),CutsceneMode::vr_comfort,false));p.reject(CutsceneFallback::fault);assert(p.reason()==CutsceneFallback::incomplete_pair);}
    {// the gates: open only for a VR mode, without the knob, before a fallback, on the paired native-eye path
     assert(cutscene_gates_open(CutsceneMode::vr_comfort,false,CutsceneFallback::none,true));
     assert(cutscene_gates_open(CutsceneMode::vr_full,false,CutsceneFallback::none,true));
     assert(!cutscene_gates_open(CutsceneMode::theater,false,CutsceneFallback::none,true));
     assert(!cutscene_gates_open(CutsceneMode::vr_comfort,true,CutsceneFallback::none,true));
     assert(!cutscene_gates_open(CutsceneMode::vr_comfort,false,CutsceneFallback::incomplete_pair,true));
     assert(!cutscene_gates_open(CutsceneMode::vr_comfort,false,CutsceneFallback::none,false));}
    {// the mode text (build/cutscene-mode): exact names, surrounding blanks ignored, anything else is no mode (the caller uses vr_comfort)
     assert(parse_cutscene_mode("vr_comfort")==CutsceneMode::vr_comfort && parse_cutscene_mode("vr_full\r\n")==CutsceneMode::vr_full);
     assert(parse_cutscene_mode(" theater ")==CutsceneMode::theater);
     for(const char* bad:{"","Theater","vr","vr_full2","theatre","vr comfort"}) assert(!parse_cutscene_mode(bad));
     for(auto mode:{CutsceneMode::vr_comfort,CutsceneMode::vr_full,CutsceneMode::theater}) assert(parse_cutscene_mode(to_string(mode))==mode);
     assert(!std::strcmp(to_string(CutsceneFallback::incomplete_pair),"incomplete_pair") && !std::strcmp(to_string(CutsceneFallback::none),"none"));}
    {   // Track T C4: a transient mixed mask inside an active immersive cutscene is held (no Theater flicker), at most 0.5 s
        auto at=[](unsigned logic,std::uint64_t ms,bool pair=true){CutsceneFrame f{logic,pair,true,true,false};f.now_ms=ms;return f;};
        CutscenePolicy p;
        assert(p.immersive(at(32,1000),CutsceneMode::vr_comfort,false));
        for(std::uint64_t ms=1010;ms<=1500;ms+=10) assert(!p.immersive(at(32|256,ms),CutsceneMode::vr_comfort,false) && p.holding() && p.reason()==CutsceneFallback::none && !p.pending());
        assert(!p.immersive(at(32|256,1511),CutsceneMode::vr_comfort,false) && !p.holding() && p.reason()==CutsceneFallback::none); // past 0.5 s: the policy takes over (no hold)
        assert(!p.immersive(at(32,1520,false),CutsceneMode::vr_comfort,false) && p.pending()); // grace re-armed as before
        assert(p.immersive(at(32,1530),CutsceneMode::vr_comfort,false) && p.cutscenes()==1);
        CutscenePolicy q; // back to exact 32 within the hold: immersive again, the pair gets the entry grace (not 2 misses) to re-form
        assert(q.immersive(at(32,0+1),CutsceneMode::vr_full,false));
        assert(!q.immersive(at(34,100),CutsceneMode::vr_full,false) && q.holding());
        for(int i=0;i<10;++i) assert(!q.immersive(at(32,200+i,false),CutsceneMode::vr_full,false) && q.holding() && q.reason()==CutsceneFallback::none);
        assert(q.immersive(at(32,300),CutsceneMode::vr_full,false) && !q.holding());
        for(int i=0;i<3;++i) assert(!q.immersive(at(32,400+i,false),CutsceneMode::vr_full,false)); // a normal run of misses is back to 2 tolerated
        assert(q.reason()==CutsceneFallback::incomplete_pair);
        CutscenePolicy r; // pause (4) ends the immersion at once; so does a mask without the cinematic bit
        assert(r.immersive(at(32,1),CutsceneMode::vr_comfort,false));
        assert(!r.immersive(at(32|4,10),CutsceneMode::vr_comfort,false) && !r.holding());
        assert(!r.immersive(at(32,20,false),CutsceneMode::vr_comfort,false) && r.pending()); // re-armed grace, as before
        CutscenePolicy u; // never held before the pair formed, after a fallback, in theater, or with no clock
        assert(!u.immersive(at(32,1,false),CutsceneMode::vr_comfort,false) && !u.immersive(at(34,2),CutsceneMode::vr_comfort,false) && !u.holding());
        CutscenePolicy v;assert(v.immersive(at(32,1),CutsceneMode::vr_comfort,false));
        assert(!v.immersive({34,true,true,true,false},CutsceneMode::vr_comfort,false) && !v.holding()); // now_ms 0: legacy behaviour
        CutscenePolicy w;assert(w.immersive(at(32,1),CutsceneMode::vr_comfort,false));
        assert(!w.immersive(at(32,2,false),CutsceneMode::vr_comfort,false)); // a held miss...
        w.reject(CutsceneFallback::incomplete_pair);assert(!w.immersive(at(34,3),CutsceneMode::vr_comfort,false) && !w.holding());}
    {CutscenePolicy p; // Track T C1: a pre-rendered video is the theater's at once: no pending black, no 30-frame wait, sticky until end()
     p.reject(CutsceneFallback::bink_video);
     assert(!p.pending() && p.reason()==CutsceneFallback::bink_video && p.last_fallback()==CutsceneFallback::bink_video);
     assert(!p.immersive(good(),CutsceneMode::vr_comfort,false) && !p.immersive(unpaired(),CutsceneMode::vr_full,false) && !p.pending());
     assert(!std::strcmp(to_string(CutsceneFallback::bink_video),"bink_video"));
     assert(!cutscene_gates_open(CutsceneMode::vr_comfort,false,p.reason(),true));
     p.end();assert(p.immersive(good(),CutsceneMode::vr_comfort,false));}
    std::cout<<"cutscene_policy_check ok\n";
}
