#include "holster_rig.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr;
using P=HolsterPlace;
static bool near3(Vec3 a,Vec3 b){return length(a-b)<1e-9;}
static BodyFrames body(){
    BodyFrames f{};
    f.pelvis={1,2,1};f.pelvis_forward={1,0,0};f.pelvis_left={0,1,0};f.pelvis_valid=true;
    f.chest={1,2,1.4};f.chest_forward={1,0,0};f.chest_left={0,1,0};f.chest_valid=true;
    f.head={1,2,1.7};f.head_forward={1,0,0};f.head_valid=true;
    return f;
}
static HolsterRig::DrawIn din(float grip,Vec3 hand,double now,P place=P::pouch_left){
    HolsterRig::DrawIn d{};d.grip=grip;d.empty_hand=true;d.have_item=true;d.hand=hand;d.now=now;d.place=place;return d;
}
int main(){
    const BodyFrames f=body();
    { // pelvis_places
        HolsterRig r;
        assert(near3(*r.centre(P::hip_left,f),Vec3{1,2.21,.95}));
        assert(near3(*r.centre(P::hip_right,f),Vec3{1,1.79,.95}));
        assert(near3(*r.centre(P::pouch_left,f),Vec3{1.14,2.09,1}));
        assert(near3(*r.centre(P::pouch_right,f),Vec3{1.14,1.91,1}));
        assert(near3(*r.centre(P::shoulder_left,f),Vec3{.98,2.17,1.57}));
        assert(near3(*r.centre(P::shoulder_right,f),Vec3{.98,1.83,1.57}));
        assert(near3(*r.centre(P::heart_pocket,f),Vec3{1.12,2.07,1.38}));   // not mirrored: body's left
        assert(!r.centre(P::none,f));
    }
    { // head_fallback
        HolsterRig r;BodyFrames h{};h.head={1,2,1.7};h.head_forward={1,0,0};h.head_valid=true;
        assert(near3(*r.centre(P::hip_left,h),Vec3{1,2.20,1.08}));
        assert(near3(*r.centre(P::hip_right,h),Vec3{1,1.80,1.08}));
        assert(near3(*r.centre(P::pouch_right,h),Vec3{1.14,1.91,1.08}));
        assert(near3(*r.centre(P::heart_pocket,h),Vec3{1.12,2.07,1.38}));
        assert(!r.centre(P::shoulder_left,h) && !r.centre(P::shoulder_right,h));
        assert(!r.centre(P::hip_left,BodyFrames{}));
    }
    { // disabled_place
        HolsterConfig c;c.places[static_cast<std::size_t>(P::heart_pocket)].radius=0;
        HolsterRig r(c);
        assert(!r.centre(P::heart_pocket,f));
        assert(r.classify(0,Vec3{1.12,2.07,1.38},HeldKind::heart,f).place!=P::heart_pocket);
        assert(r.classify(0,Vec3{1.12,2.07,1.38},HeldKind::empty,f).place==P::none);
    }
    { // nearest_wins: between hip_left (1,2.21,.95) and pouch_left (1.14,2.09,1)
        HolsterRig r;
        assert(r.classify(0,Vec3{1.09,2.12,.99},HeldKind::gadget,f).place==P::pouch_left);
        assert(r.classify(0,Vec3{1.02,2.19,.96},HeldKind::gadget,f).place==P::hip_left);
    }
    { // kind_filter
        HolsterRig r;
        assert(r.classify(0,Vec3{1.14,2.09,1},HeldKind::sword,f).place==P::none);
        assert(r.classify(0,Vec3{1,2.21,.95},HeldKind::gadget,f).place==P::hip_left);
        assert(r.classify(0,Vec3{1,2.21,.95},HeldKind::gun,f).place==P::hip_left);
        assert(r.classify(0,Vec3{.98,2.17,1.57},HeldKind::sword,f).place==P::shoulder_left);
        assert(r.classify(0,Vec3{1,2.21,.95},HeldKind::empty,f).place==P::none);   // empty: draw places only
        assert(r.classify(0,Vec3{1.14,2.09,1},HeldKind::empty,f).place==P::pouch_left);
        assert(r.classify(0,Vec3{1.14,2.09,1},HeldKind::other,f).place==P::none);
    }
    { // exit_hysteresis
        HolsterRig r;const Vec3 c=*r.centre(P::pouch_left,f);
        assert(r.classify(0,c+Vec3{0,0,.12},HeldKind::gadget,f).place==P::pouch_left);
        assert(r.classify(0,c+Vec3{0,0,.15},HeldKind::gadget,f).place==P::pouch_left);
        assert(r.classify(0,c+Vec3{0,0,.17},HeldKind::gadget,f).place==P::none);
        assert(r.classify(0,c+Vec3{0,0,.15},HeldKind::gadget,f).place==P::none);   // left it: no exit extra any more
    }
    { // fast_release_never_stows + heart_any_place
        HolsterRig r;
        assert(!r.put_back(HeldKind::gadget,P::pouch_left,false,1.2));
        assert(r.put_back(HeldKind::gadget,P::pouch_left,false,.5));
        assert(!r.put_back(HeldKind::gadget,P::pouch_left,true,.5));
        assert(r.put_back(HeldKind::gadget,P::hip_right,false,.5));
        assert(!r.put_back(HeldKind::gadget,P::none,false,.1));
        for(unsigned i=1;i<static_cast<unsigned>(P::count);++i) assert(r.put_back(HeldKind::heart,static_cast<P>(i),false,5.));
        assert(!r.put_back(HeldKind::heart,P::none,false,0));
        assert(!r.put_back(HeldKind::sword,P::shoulder_left,false,0));
        assert(!r.put_back(HeldKind::gun,P::hip_left,false,0));
    }
    const Vec3 o{0,0,0};
    { // pull_commits
        HolsterRig r;
        assert(!r.draw(0,din(0,o,0)).armed);
        auto a=r.draw(0,din(1,o,.1));assert(a.armed && !a.committed && a.place==P::pouch_left);
        auto m=r.draw(0,din(1,Vec3{.04,0,0},.3));assert(!m.committed && !m.cancelled);
        auto c=r.draw(0,din(1,Vec3{.08,0,0},.5));assert(c.committed && c.place==P::pouch_left);
        auto n=r.draw(0,din(1,Vec3{.2,0,0},.6));assert(!n.committed && !n.cancelled);
        assert(r.cooling(0,.7));
    }
    { // pull_cancel_on_release
        HolsterRig r;r.draw(0,din(1,o,.1));
        auto c=r.draw(0,din(.2f,Vec3{.01,0,0},.3));assert(c.cancelled && !c.committed);
        assert(!r.draw(0,din(.2f,Vec3{.2,0,0},.4)).committed);
        r=HolsterRig();r.draw(0,din(1,o,.1));
        assert(!r.draw(0,din(.5f,Vec3{.01,0,0},.2)).cancelled);   // hysteresis: 0.5 still held
    }
    { // pull_timeout
        HolsterRig r;r.draw(0,din(1,o,0));
        assert(!r.draw(0,din(1,o,1.1)).cancelled);
        assert(r.draw(0,din(1,o,1.3)).cancelled);
        r=HolsterRig();r.draw(0,din(1,o,0));
        assert(r.draw(0,din(1,Vec3{.1,0,0},1.3)).cancelled);      // moved, but too late
    }
    { // edge_only
        HolsterRig r;
        assert(!r.draw(0,din(1,o,0,P::none)).armed);               // closed outside any place
        assert(!r.draw(0,din(1,o,.1)).armed);                      // entering with the grip already closed
        assert(!r.draw(0,din(1,o,.2)).armed);
        r.draw(0,din(0,o,.3));
        assert(r.draw(0,din(1,o,.4)).armed);
    }
    { // grab_candidate_suspends_draw
        HolsterRig r;auto d=din(1,o,0);d.suspended=true;
        assert(!r.draw(0,d).armed);
        r.draw(0,din(0,o,.1));
        assert(r.draw(0,din(1,o,.2)).armed);
        d=din(1,o,.3);d.suspended=true;
        assert(r.draw(0,d).cancelled);
        auto e=din(1,o,.4);e.empty_hand=false;r.draw(0,din(0,o,.35));
        assert(!r.draw(0,e).armed);
    }
    { // empty_pouch
        HolsterRig r;auto d=din(1,o,0);d.have_item=false;
        auto a=r.draw(0,d);assert(a.empty && !a.armed);
        d.now=.1;assert(!r.draw(0,d).empty);
        d.now=.2;d.grip=0;r.draw(0,d);
        d.now=.3;d.grip=1;assert(r.draw(0,d).empty);
    }
    { // cooldown
        HolsterRig r;r.draw(0,din(1,o,0));
        assert(r.draw(0,din(1,Vec3{.1,0,0},.1)).committed);
        r.draw(0,din(0,o,.2));
        assert(!r.draw(0,din(1,o,.3)).armed);
        r.draw(0,din(0,o,.5));
        assert(r.draw(0,din(1,o,.7)).armed);
        assert(!r.cooling(1,.1));                                  // per hand
        r.note_action(1,1.0);assert(r.cooling(1,1.3) && !r.cooling(1,1.7));
        r.reset();assert(!r.cooling(1,1.3));
    }
    { // parse_places
        HolsterConfig base;
        auto c=parse_holster_places("# comment\npouch 0.2 0.1 0.01 0.14 # tail\n\r\nheart_pocket 0.1 0.05 0 0\n",base);
        assert(c);
        HolsterRig r(*c);
        assert(near3(*r.centre(P::pouch_left,f),Vec3{1.2,2.1,1.01}));
        assert(near3(*r.centre(P::pouch_right,f),Vec3{1.2,1.9,1.01}));
        assert(!r.centre(P::heart_pocket,f));
        assert(near3(*HolsterRig(*c).centre(P::hip_left,f),Vec3{1,2.21,.95}));
        assert(!parse_holster_places("pouch 0.2 0.1 x 0.14\n",base));
        assert(!parse_holster_places("belt 0.2 0.1 0 0.14\n",base));
        assert(!parse_holster_places("pouch 0.2 0.1 0\n",base));
        assert(!parse_holster_places("pouch 0.2 0.1 nan 0.14\n",base));
        assert(!parse_holster_places("pouch 0.2 0.1 0 0.14\nhip 1\n",base));
        assert(parse_holster_places("",base));
    }
    std::printf("holster_rig_check ok\n");
    return 0;
}
