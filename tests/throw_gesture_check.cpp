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
    {   // v771 (user, headset v770): dual wield, the grip holds a grenade or mine like any weapon; the trigger arms it
        d2vr::GadgetGrip g;std::uint64_t t=0;const auto step=[&](bool grenade,float grip,float trig) {t+=11;return g.update(true,grenade,grip,trig,t);};
        auto o=step(true,1,0);expect(o.held && !o.native_trigger && !o.drop,"grip alone holds a grenade, unarmed");
        o=step(true,0,0);expect(o.drop && !o.native_trigger,"letting go unarmed drops it (put down or tossed)");
        o=step(true,1,0);o=step(true,1,1);expect(o.armed && o.native_trigger,"the trigger while gripping arms it: the game's trigger held (aim)");
        o=step(true,1,0);expect(o.armed && o.native_trigger,"armed stays armed when the index lets go");
        o=step(true,0,0);expect(!o.drop && !o.native_trigger && !o.armed,"the grip opening releases the game's trigger: the throw, never a drop");
        o=step(true,0,1);o=step(true,1,1);expect(!o.armed && !o.native_trigger,"a trigger already down at the grab does not arm (a resting index)");
        o=step(true,1,0);o=step(true,1,1);expect(o.armed,"... pressing it again does");
        o=step(true,0,0);
        o=step(false,1,0);o=step(false,1,1);expect(o.native_trigger && !o.drop,"a mine: the trigger taps the game's trigger (placement)");
        for(int i=0;i<20;++i) o=step(false,1,0);
        expect(!o.native_trigger,"... a short tap, not a hold");
        o=step(false,0,0);expect(o.drop,"a mine let go without the trigger drops, unarmed");
        o=g.update(false,true,1,1,t+=11);expect(!o.held && !o.native_trigger && !o.drop,"no gadget: nothing");
        // v779 (user, headset v778: a mine from the wheel was placed only after a toss; the journal had 25 trigger presses with the grip open, none placed)
        o=g.update(true,false,0,1,t+=11);expect(!o.native_trigger,"a trigger already down when a mine arrives places nothing");
        o=g.update(true,false,0,0,t+=11);o=g.update(true,false,0,1,t+=11);expect(o.native_trigger && !o.drop,"a mine from the wheel: the trigger alone places it");
        for(int i=0;i<20;++i) o=g.update(true,false,0,0,t+=11);
        o=g.update(true,false,1,1,t+=11);expect(!o.native_trigger,"the frame the grip closes (a catch with a resting index) places nothing");
        o=g.update(true,false,1,0,t+=11);o=g.update(true,false,1,1,t+=11);expect(o.native_trigger,"... a press while gripping still places");
        o=g.update(true,true,0,0,t+=11);o=g.update(true,true,0,1,t+=11);expect(!o.armed && !o.native_trigger,"a grenade still needs the grip");
    }
    {   // Task 7 (user: "mine throw should attach mine, like in the flat game"): a mine let go while the hand moves fast is placed by the engine along the
        // throw (a native trigger tap + the throw-aim override); slow it is put down as before. Speed is an explicit input (m/s).
        const auto holding=[](d2vr::GadgetGrip& g,std::uint64_t& t,bool grenade) {d2vr::GadgetGrip::Out o{};for(int i=0;i<3;++i) o=g.update(true,grenade,1,0,t+=11,0);return o;};
        {   // fast_open_places
            d2vr::GadgetGrip g;std::uint64_t t=0;holding(g,t,false);
            auto o=g.update(true,false,0,0,t+=11,2.0);
            expect(o.throw_place && !o.drop && o.native_trigger,"fast open: a throw placement and a native tap, never a drop");
            o=g.update(true,false,0,0,t+=11,2.0);expect(!o.throw_place && !o.drop && o.native_trigger,"the tap goes on, one throw_place only");
            for(int i=0;i<20;++i) o=g.update(true,false,0,0,t+=11,0);
            expect(!o.native_trigger && !o.throw_place && !o.drop,"the tap is short");
        }
        {   // slow_open_drops
            d2vr::GadgetGrip g;std::uint64_t t=0;holding(g,t,false);
            const auto o=g.update(true,false,0,0,t+=11,.5);
            expect(o.drop && !o.throw_place && !o.native_trigger,"slow open: the old drop");
            d2vr::GadgetGrip e;t=0;holding(e,t,false);
            expect(e.update(true,false,0,0,t+=11,1.5).drop,"exactly 1.5 m/s is still slow");
        }
        {   // grenade_unchanged: fast open of an unarmed grenade drops it, armed one throws; never a throw_place
            d2vr::GadgetGrip g;std::uint64_t t=0;holding(g,t,true);
            auto o=g.update(true,true,0,0,t+=11,3.0);expect(o.drop && !o.throw_place && !o.native_trigger,"grenade, fast open, unarmed: drop as today");
            o=g.update(true,true,1,0,t+=11,0);o=g.update(true,true,1,1,t+=11,0);expect(o.armed && o.native_trigger,"grenade armed as today");
            o=g.update(true,true,0,0,t+=11,3.0);expect(!o.drop && !o.throw_place && !o.native_trigger,"armed grenade opens into the native throw");
        }
        {   // armed_tap_unchanged: the trigger tapped while holding places; opening the grip in the tap neither drops nor re-places
            d2vr::GadgetGrip g;std::uint64_t t=0;holding(g,t,false);
            auto o=g.update(true,false,1,1,t+=11,0);expect(o.native_trigger && !o.throw_place,"trigger tap places as today");
            o=g.update(true,false,0,1,t+=11,3.0);expect(!o.drop && !o.throw_place && o.native_trigger,"grip opening inside the tap: no drop, no second placement");
        }
    }
    {   // Task 6 (DOTO Hook Mine): holding the trigger >= 450 ms with a mine in hand is the game's own mode switch (a native LT hold), a short press
        // still places/throws, and one press does ONE action. The trait/flag off (mode_capable=false) is today's behaviour exactly.
        const auto hold=[](d2vr::GadgetGrip& g,std::uint64_t& t,bool grenade,bool mode) {d2vr::GadgetGrip::Out o{};for(int i=0;i<3;++i) o=g.update(true,grenade,1,0,t+=11,0,mode);return o;};
        {   // tap_still_throws
            d2vr::GadgetGrip g;std::uint64_t t=0;hold(g,t,false,true);
            bool any_mode=false,any_native=false,switched=false;
            for(int i=0;i<11;++i) {const auto o=g.update(true,false,1,1,t+=11,0,true);any_mode=any_mode||o.mode_hold;any_native=any_native||o.native_trigger;}   // 121 ms down
            expect(!any_mode,"tap: no mode hold while the press is short");
            auto o=g.update(true,false,1,0,t+=11,0,true);any_native=o.native_trigger;switched=o.mode_switched;
            expect(any_native && !o.mode_hold && !switched && !o.drop,"tap: released under 450 ms -> the native tap (place/throw), no mode switch");
            for(int i=0;i<20;++i) o=g.update(true,false,1,0,t+=11,0,true);
            expect(!o.native_trigger,"tap: a short tap, not a hold");
        }
        {   // long_hold_switches
            d2vr::GadgetGrip g;std::uint64_t t=0;hold(g,t,false,true);
            d2vr::GadgetGrip::Out o{};std::uint64_t down=t+11;bool early=false,late=true,any_switched=false;
            for(int i=0;i<82;++i) {o=g.update(true,false,1,1,t+=11,0,true);   // ~900 ms
                if(t-down<440) early=early||o.mode_hold||o.native_trigger;
                if(t-down>=460) late=late&&o.mode_hold&&o.native_trigger;
                any_switched=any_switched||o.mode_switched||o.drop||o.throw_place;}
            expect(!early,"long hold: nothing is sent before 450 ms (a tap would only come at the release)");
            expect(late && !any_switched,"long hold: from 450 ms the game's LT is held (mode_hold), no tap, throw or drop");
            o=g.update(true,false,1,0,t+=11,0,true);
            expect(o.mode_switched && !o.mode_hold && !o.native_trigger && !o.drop && !o.throw_place,"long hold: the release ends the hold with the switched edge and NO tap");
            for(int i=0;i<20;++i) {o=g.update(true,false,1,0,t+=11,0,true);expect(!o.mode_switched && !o.native_trigger && !o.mode_hold,"long hold: nothing after the release");}
        }
        {   // long_hold_then_open_does_not_throw: one action only
            d2vr::GadgetGrip g;std::uint64_t t=0;hold(g,t,false,true);
            d2vr::GadgetGrip::Out o{};for(int i=0;i<50;++i) o=g.update(true,false,1,1,t+=11,0,true);
            expect(o.mode_hold && o.native_trigger,"mode hold running");
            o=g.update(true,false,0,1,t+=11,3.0,true);
            expect(!o.throw_place && !o.drop && !o.native_trigger && !o.mode_hold,"grip opens fast mid-hold: LT released, no throw, no drop");
            bool bad=false;for(int i=0;i<30;++i) {o=g.update(true,false,0,1,t+=11,3.0,true);bad=bad||o.throw_place||o.drop||o.native_trigger||o.mode_hold;}
            o=g.update(true,false,0,0,t+=11,3.0,true);bad=bad||o.throw_place||o.drop||o.native_trigger||o.mode_hold;
            expect(!bad,"... and nothing after, even with the trigger still down or released");
        }
        {   // mode_switch_off_trait_unchanged: the same inputs with mode_capable=false equal the default-argument (old) outputs
            d2vr::GadgetGrip a,b;std::uint64_t t=0;bool same=true;
            const auto eq=[](const d2vr::GadgetGrip::Out& x,const d2vr::GadgetGrip::Out& y) {return x.native_trigger==y.native_trigger && x.drop==y.drop && x.held==y.held && x.armed==y.armed && x.throw_place==y.throw_place;};
            for(int i=0;i<200;++i) {
                const float grip=(i<5||(i>120&&i<140))?1.f:(i<125?1.f:0.f);const float trig=(i>=10&&i<100)?1.f:0.f;
                t+=11;const auto x=a.update(true,false,grip,trig,t,i==141?2.0:0);const auto y=b.update(true,false,grip,trig,t,i==141?2.0:0,false);
                same=same&&eq(x,y)&&!y.mode_hold&&!y.mode_switched;}
            expect(same,"trait off: identical outputs, never a mode hold");
            d2vr::GadgetGrip g;t=0;hold(g,t,false,false);auto o=g.update(true,false,1,1,t+=11,0,false);
            expect(o.native_trigger,"trait off: the tap starts at the press, as before");
            for(int i=0;i<80;++i) {o=g.update(true,false,1,1,t+=11,0,false);expect(!o.mode_hold && !o.mode_switched,"trait off: a 900 ms hold is never a mode hold");}
        }
        {   // grenade_unchanged (mode_capable on): a grenade ignores the mode switch
            d2vr::GadgetGrip g;std::uint64_t t=0;hold(g,t,true,true);
            auto o=g.update(true,true,1,1,t+=11,0,true);expect(o.armed && o.native_trigger && !o.mode_hold,"grenade armed as today");
            bool mode=false;for(int i=0;i<80;++i) {o=g.update(true,true,1,1,t+=11,0,true);mode=mode||o.mode_hold||o.mode_switched;}
            expect(!mode && o.native_trigger,"grenade: a long trigger hold is never a mode hold");
            o=g.update(true,true,0,0,t+=11,3.0,true);expect(!o.drop && !o.throw_place && !o.native_trigger,"armed grenade still opens into the native throw");
        }
    }
    std::printf(failures?"throw_gesture_check FAILED\n":"throw_gesture_check ok\n");
    return failures?1:0;
}
