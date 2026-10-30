#include "../src/throw_gesture.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
using V=std::array<double,3>;
int main() {
    using d2vr::ThrowGesture;using d2vr::gadget_squeeze;
    {   // peak over the last 100 ms x1.5 + player velocity
        ThrowGesture g;g.note_trigger(0,1);
        for(unsigned t=0;t<=400;t+=10) g.note_hand(t,{1,2,3},t==300?V{0,0,-6}:V{0,0,-1});
        g.note_trigger(350,0);   // release; the 6 m/s sample at 300 is within 100 ms
        expect(g.pending(400),"release pending");
        const auto r=g.take(650,{0,0,0});
        expect(r.ok && std::fabs(r.velocity[2]+9)<1e-9 && r.spawn[0]==1,"1.5 x peak, spawn at the hand");
        expect(!g.take(651,{}).ok,"consumed");
    }
    {   // a swing older than the window does not count; player velocity added; cap 17
        ThrowGesture g;g.note_trigger(0,1);
        for(unsigned t=0;t<=300;t+=10) g.note_hand(t,{0,0,0},t==100?V{0,0,-9}:V{.1,0,0});
        g.note_trigger(300,0);
        const auto r=g.take(500,{1,0,0});
        expect(r.ok && std::fabs(r.velocity[0]-1.15)<1e-9 && r.velocity[2]==0,"old swing ignored, player added");
        ThrowGesture h;h.note_trigger(0,1);h.note_hand(10,{0,0,0},{0,0,-12});h.note_trigger(20,0);
        const auto c=h.take(30,{0,0,0});
        expect(c.ok && std::fabs(c.velocity[2]+17)<1e-9,"capped at 17 m/s");
    }
    {   // no release / stale release / no samples / teleport / non-finite -> native fallback
        ThrowGesture g;g.note_hand(0,{0,0,0},{0,0,-3});
        expect(!g.take(10,{}).ok,"no release");
        g.note_trigger(0,1);g.note_trigger(20,0);
        expect(!g.take(1600,{}).ok,"stale release");
        ThrowGesture n;n.note_trigger(0,1);n.note_trigger(5,0);n.note_hand(6,{0,0,0},{0,0,-1});
        expect(!n.take(10,{}).ok,"no sample before the release");
        ThrowGesture t;t.note_trigger(0,1);t.note_hand(5,{0,0,0},{0,0,-20});t.note_trigger(10,0);
        expect(!t.take(20,{}).ok,"teleport speed ignored");
        ThrowGesture f;f.note_trigger(0,1);f.note_hand(5,{0,0,0},{NAN,0,0});f.note_trigger(10,0);
        expect(!f.take(20,{}).ok,"non-finite");
        ThrowGesture s;s.note_trigger(0,1);s.note_hand(5,{0,0,0},{0,0,-3});s.note_trigger(10,0);
        expect(!s.take(900,{}).ok,"hand position older than 250 ms");
    }
    {   // hysteresis: a trigger wobble between the thresholds is not a release; a new squeeze discards an unused release
        ThrowGesture g;g.note_trigger(0,1);g.note_hand(5,{0,0,0},{0,0,-3});
        g.note_trigger(10,.5f);expect(!g.pending(11),"inside hysteresis");
        g.note_trigger(20,0);g.note_trigger(30,1);expect(!g.pending(31),"re-squeeze discards");
    }
    {   // a fist holds the gadget, opening the hand releases it; the trigger alone still works
        expect(gadget_squeeze(1,1)==1 && gadget_squeeze(1,.2f)==.2f && gadget_squeeze(.2f,1)==1 && gadget_squeeze(0,0)==0,"gadget squeeze");
        ThrowGesture g;g.note_trigger(0,gadget_squeeze(1,1));g.note_hand(5,{0,0,0},{0,0,-3});
        g.note_trigger(10,gadget_squeeze(.1f,.1f));expect(g.pending(11),"an opened fist releases");
    }
    {   // v688: a fist ends when the grip opens even while the index still rests on the trigger; trigger-only holds are unchanged
        d2vr::GadgetFist fist;expect(fist(1,1)==1,"fist held");
        expect(fist(.3f,.9f)<.35f,"grip opening releases the fist at once");
        expect(fist(.3f,.9f)<.35f && fist(.1f,.9f)<.35f,"a resting index does not cook again");
        expect(fist(.1f,.1f)<.35f && fist(1,1)==1,"released index re-arms");
        d2vr::GadgetFist trig;expect(trig(.3f,1)==1 && trig(.3f,.9f)>=.55f,"trigger-only hold with a resting grip stays held");
        expect(trig(.3f,.1f)<.35f,"trigger-only release");
    }
    {   // v690: an early release on the throwing motion takes the peak of the follow-through, and holds stay off until the fist opens
        ThrowGesture g;g.note_trigger(0,1);g.note_hand(10,{0,0,0},{0,0,-2});
        expect(g.speed(20)==2 && g.speed(100)==0,"latest hand speed");
        g.release_now(20);g.note_trigger(30,1);g.note_hand(120,{0,0,-.5},{0,0,-6});g.note_hand(200,{0,0,-1},{0,0,-3});
        expect(g.pending(201),"held fist after an early release does not cancel it");
        const auto r=g.take(250,{0,0,0});expect(r.ok && std::abs(r.velocity[2]+9)<1e-9 && r.spawn[2]==-1,"follow-through peak, launch position");
        g.note_trigger(260,0);g.note_trigger(270,1);expect(!g.pending(271),"a new hold after the fist opened");
    }
    std::printf(failures?"throw_gesture_check FAILED\n":"throw_gesture_check ok\n");
    return failures?1:0;
}
