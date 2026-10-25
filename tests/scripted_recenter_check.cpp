#include "../src/scripted_recenter.hpp"
#include "../src/head_locomotion.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
static constexpr double kPi=3.14159265358979323846;
static double deg(double d){return d*kPi/180;}
static double wrap_test(double a){return std::remainder(a,2*kPi);}
static bool near(double a,double b,double e=1e-6){return std::fabs(a-b)<e;}
static ScriptedRecenterInput in(double t,bool authored,std::optional<double> rel=std::nullopt,std::optional<double> heading={},
    std::uint64_t snaps=0,std::uint64_t epoch=1,std::optional<double> body={},bool instant=false){
    return {t,authored,rel,heading,snaps,epoch,body,instant};
}
int main(){
    static_assert(ScriptedRecenterParams{}.dead_zone_rad==0.17453292519943295 && ScriptedRecenterParams{}.ease_s==0.4,"tuned constants");
    {const auto q=yaw_quaternion(.7); // matches HeadCalibration's yaw convention (about +Y, left turn +)
     assert(near(tracked_yaw(q,{0,0,0,1}),.7) && near(q.y,std::sin(.35)) && near(q.w,std::cos(.35)));}
    {ScriptedRecenter r; // entry_starts_continuous (sim mantle, state 8): the latched heading is already the gaze, so the start is the target
     auto o=r.update(in(0,false,{},{},0,1,deg(-130)));assert(o.offset_rad==0 && o.entries==0);
     o=r.update(in(.01,true,deg(40),deg(-90)));
     assert(near(o.offset_rad,deg(40)) && !o.easing && o.entries==1 && o.skipped_small==1);
     const double gameplay_view=deg(-130)+deg(40),authored_view=deg(-90)+(deg(40)-o.offset_rad);assert(near(gameplay_view,authored_view)); // view continuous
     o=r.update(in(.5,true,deg(40)-o.offset_rad,deg(-90)));assert(near(o.offset_rad,deg(40)) && !o.easing);}
    {ScriptedRecenter r; // entry_eases_from_start: body == heading, start 0, ease 0 -> 40 over 0.4 s
     r.update(in(0,false,{},{},0,1,0));auto o=r.update(in(1,true,deg(40),0.));assert(near(o.offset_rad,0) && o.easing);
     o=r.update(in(1.2,true,deg(40),0.));assert(near(o.offset_rad,deg(20)));o=r.update(in(1.401,true,deg(40),0.));assert(near(o.offset_rad,deg(40)) && !o.easing);}
    {ScriptedRecenter r; // entry_heading_differs_from_body_and_gaze: start 60 (continuity), ease to the gaze 20
     r.update(in(0,false,{},{},0,1,0));auto o=r.update(in(1,true,deg(20),deg(60)));
     assert(near(o.offset_rad,deg(60)) && o.easing && near(deg(60)+deg(20)-o.offset_rad,0+deg(20))); // the entry view is the gameplay view
     o=r.update(in(1.2,true,0,deg(60)));assert(near(o.offset_rad,deg(40)));
     o=r.update(in(1.401,true,0,deg(60)));assert(near(o.offset_rad,deg(20)) && !o.easing);
     ScriptedRecenter l;l.update(in(0,false,{},{},0,1,0)); // a late entry starts from the body latched at entry
     l.update(in(.1,true,std::nullopt,std::nullopt));l.update(in(.2,true,std::nullopt,std::nullopt));
     o=l.update(in(.3,true,deg(20),deg(60)));assert(near(o.offset_rad,deg(60)) && o.easing && o.entries==1);
     o=l.update(in(.701,true,0,deg(60)));assert(near(o.offset_rad,deg(20)));
     ScriptedRecenter e;e.update(in(0,false,{},{},0,1,0)); // another owner's body is no reference: start 0
     o=e.update(in(.1,true,deg(20),deg(60),0,2));assert(near(o.offset_rad,0) && o.easing);}
    {ScriptedRecenter r; // entry_eases_to_delta
     double prev=-1;
     auto o=r.update(in(10,true,deg(40),1.0));assert(near(o.offset_rad,0) && o.easing && o.entries==1);
     for(int i=1;i<=40;++i){o=r.update(in(10+.01*i,true,deg(40),1.0));assert(o.offset_rad>=prev-1e-12);prev=o.offset_rad;
        if(i==20) assert(std::fabs(o.offset_rad-deg(20))<deg(.5));}
     assert(near(o.offset_rad,deg(40)) && !o.easing);
     o=r.update(in(11,true,deg(40),1.0));assert(near(o.offset_rad,deg(40)));}
    {ScriptedRecenter r; // small_delta_skipped
     auto o=r.update(in(0,true,deg(9.9),1.0));o=r.update(in(.5,true,deg(9.9),1.0));
     assert(o.offset_rad==0 && o.skipped_small==1 && !o.easing);
     ScriptedRecenter q;q.update(in(0,true,deg(10.1),1.0));o=q.update(in(.5,true,deg(10.1),1.0));
     assert(near(o.offset_rad,deg(10.1)) && o.skipped_small==0);
     ScriptedRecenter n;n.update(in(0,true,-deg(10.1),1.0));o=n.update(in(.5,true,0,1.0));assert(near(o.offset_rad,-deg(10.1)));}
    {ScriptedRecenter r; // head_motion_during_ease_respected
     r.update(in(0,true,deg(40),1.0));
     auto o=r.update(in(.2,true,deg(-90),1.0));assert(near(o.offset_rad,deg(20)));
     o=r.update(in(.5,true,deg(5),1.0));assert(near(o.offset_rad,deg(40)));}
    {ScriptedRecenter r; // snap_reaims
     r.update(in(0,true,deg(40),1.0));auto o=r.update(in(1,true,0,1.0));assert(near(o.offset_rad,deg(40)) && o.reaims==0);
     o=r.update(in(1.1,true,deg(35),1.5,1));assert(o.reaims==1 && o.easing && near(o.offset_rad,deg(40)));
     o=r.update(in(1.5,true,0,1.5,1));assert(near(o.offset_rad,deg(75)) && o.reaims==1);
     o=r.update(in(1.6,true,0,1.5,1));assert(o.reaims==1);}
    {ScriptedRecenter r; // exit_folds_with_continuity
     r.update(in(0,true,deg(40),1.0));auto o=r.update(in(1,true,0,1.0));
     o=r.update(in(1.1,false,{},{},0,1,1.2));
     assert(o.fold_rad && near(*o.fold_rad,deg(40)+0.2) && o.offset_rad==0 && o.folds==1 && !o.easing);
     o=r.update(in(1.2,false,{},{},0,1,1.2));assert(!o.fold_rad && o.offset_rad==0 && o.folds==1);
     ScriptedRecenter w;w.update(in(0,true,deg(40),3.0));w.update(in(1,true,0,3.0));
     o=w.update(in(1.1,false,{},{},0,1,-3.0));assert(o.fold_rad && near(*o.fold_rad,deg(40)+std::remainder(-6.0,2*kPi)));
     ScriptedRecenter m;m.update(in(0,true,deg(40),1.0));m.update(in(1,true,0,1.0)); // no body: offset alone
     o=m.update(in(1.1,false));assert(o.fold_rad && near(*o.fold_rad,deg(40)));
     ScriptedRecenter z;z.update(in(0,true,deg(5),1.0)); // nothing to fold
     o=z.update(in(1,false));assert(!o.fold_rad && o.folds==0);}
    {ScriptedRecenter r; // epoch_change_drops_offset
     r.update(in(0,true,deg(40),1.0));r.update(in(.2,true,deg(40),1.0));
     auto o=r.update(in(.3,false,{},{},0,2,1.2));assert(near(o.offset_rad,0) && !o.fold_rad && o.folds==0);
     ScriptedRecenter q;q.update(in(0,true,deg(40),1.0));q.update(in(1,true,0,1.0));
     o=q.update(in(1.1,true,deg(2),1.0,0,2));assert(near(o.offset_rad,0) && !o.fold_rad);}
    {ScriptedRecenter r; // reentry_during_ease_is_continuous (body == heading, so exit adds no jump)
     r.update(in(0,true,deg(40),1.0));double fold_sum=0,prev=0,worst=0;
     for(int i=1;i<=80;++i){const double t=.005*i;const bool a=!(i>=20&&i<30);
        // hmd is measured against the composed origin; after a fold the origin turned by fold_sum, gaze stays put
        auto o=r.update(in(t,a,a?std::optional<double>(0):std::nullopt,1.0,0,1,1.0));
        if(o.fold_rad) fold_sum+=*o.fold_rad;
        const double cum=o.offset_rad+fold_sum;worst=std::max(worst,std::fabs(cum-prev));prev=cum;}
     assert(worst<deg(40)*(.005/.4)*1.5+1e-9);}
    {ScriptedRecenter r; // exit_without_body_marks_and_eases_residual
     r.update(in(0,true,deg(40),1.0));r.update(in(1,true,0,1.0));
     auto o=r.update(in(1.1,false));assert(o.fold_rad && near(*o.fold_rad,deg(40)) && o.residual_easing && o.folds==1);
     double sum=0,prev=0,mid=0;const double err=.2;
     for(int i=0;i<=41;++i){o=r.update(in(2+.01*i,false,{},{},0,1,1.0+err));
        if(o.fold_rad) sum+=*o.fold_rad;assert(sum>=prev-1e-12);prev=sum;
        if(i==20) mid=sum;
        assert(o.offset_rad==0);}
     assert(near(sum,err) && !o.residual_easing && near(mid,err*.5) && !r.update(in(3,false,{},{},0,1,1.2)).fold_rad);
     ScriptedRecenter q;q.update(in(0,true,0,1.0)); // zero offset, no body: marked but no fold on exit
     o=q.update(in(1,false));assert(!o.fold_rad && o.residual_easing);}
    {ScriptedRecenter r; // residual_dropped_on_epoch_change
     r.update(in(0,true,deg(40),1.0));r.update(in(1,true,0,1.0));r.update(in(1.1,false));
     auto o=r.update(in(1.2,false,{},{},0,2,1.5));assert(!o.fold_rad && !o.residual_easing);
     o=r.update(in(1.8,false,{},{},0,2,1.5));assert(!o.fold_rad);
     ScriptedRecenter q;q.update(in(0,true,0,1.0));q.update(in(1,false));
     q.update(in(1.1,false,{},{},0,1,1.5));o=q.update(in(1.2,false,{},{},0,3,1.5));assert(!o.fold_rad && !o.residual_easing);}
    {ScriptedRecenter r; // reentry_view_yaw_continuous: body != heading, 28 deg gaze delta after re-entry
     const double H=1.0,B=1.2,dt=.005,hmd=deg(80); // fixed head gaze in tracking space, base origin 0
     double sumfold=0,prev=NAN,worst1=0,worst2=0,rel2=0;
     for(int i=0;i<=120;++i){const double t=dt*i;const bool a=!(i>=40&&i<50);
        const double heading=i<50?H:B; // native heading on re-entry is the body's (gameplay) heading
        const double rel=hmd-sumfold; // hmd against the folded origin
        auto o=r.update(in(t,a,a?std::optional<double>(rel):std::nullopt,a?std::optional<double>(heading):std::nullopt,0,1,B));
        if(i==50) rel2=rel;
        if(o.fold_rad) sumfold+=*o.fold_rad;
        const double view=(a?heading:B)+(hmd-o.offset_rad-sumfold); // world view yaw
        if(!std::isnan(prev)) (i<50?worst1:worst2)=std::max(i<50?worst1:worst2,std::fabs(wrap_test(view-prev)));
        prev=view;}
     assert(rel2>deg(20) && r.update(in(1,true,0,B,0,1,B)).entries==2);
     assert(worst1<=1.5*hmd/.4*dt+1e-6 && worst2<=1.5*rel2/.4*dt+1e-6);}
    {ScriptedRecenter r; // non_finite_hmd_no_turn
     auto o=r.update(in(0,true,std::nullopt,1.0));o=r.update(in(1,true,deg(40),1.0)); // the first valid frame starts the pending entry turn
     assert(o.offset_rad==0 && o.easing && o.entries==1 && o.reaims==0);o=r.update(in(1.5,true,deg(40),1.0));assert(near(o.offset_rad,deg(40)) && !o.easing);
     ScriptedRecenter q;o=q.update(in(0,true,NAN,1.0));o=q.update(in(1,true,NAN,1.0));assert(o.offset_rad==0);}
    {for(double d:{179.,-179.}){ScriptedRecenter r;r.update(in(0,true,deg(d),1.0));auto o=r.update(in(1,true,deg(d),1.0));
        assert(near(o.offset_rad,deg(d)) && std::fabs(o.offset_rad)<kPi);}
     ScriptedRecenter a,b;a.update(in(0,true,kPi,1.0));b.update(in(0,true,-kPi,1.0));
     assert(near(a.update(in(1,true,0,1.0)).offset_rad,kPi) && near(b.update(in(1,true,0,1.0)).offset_rad,kPi));
     ScriptedRecenter c;c.update(in(0,true,deg(190),1.0));assert(near(c.update(in(1,true,0,1.0)).offset_rad,deg(-170)));}
    {ScriptedRecenter r; // late_heading_starts_entry_turn: 3 frames without a gaze, then 40 deg eases to 40 deg over 0.4 s from that frame
     for(int i=0;i<3;++i){auto o=r.update(in(.01*i,true,std::nullopt,std::nullopt));assert(o.offset_rad==0 && !o.easing);}
     auto o=r.update(in(.03,true,deg(40),1.0));assert(o.easing && near(o.offset_rad,0) && o.entries==1 && o.reaims==0);
     o=r.update(in(.23,true,deg(40),1.0));assert(near(o.offset_rad,deg(20)) && o.easing);
     o=r.update(in(.42,true,deg(40),1.0));assert(o.easing && o.offset_rad<deg(40));o=r.update(in(.431,true,deg(40),1.0));assert(near(o.offset_rad,deg(40)) && !o.easing && o.entries==1 && o.reaims==0 && o.skipped_small==0);
     o=r.update(in(.6,true,deg(-60),1.0));assert(near(o.offset_rad,deg(40)) && !o.easing); // one-shot: no second turn without a snap
     ScriptedRecenter s;s.update(in(0,true,std::nullopt,std::nullopt));o=s.update(in(.1,true,deg(5),1.0)); // a late small delta: the dead zone holds
     assert(o.skipped_small==1 && !o.easing && o.offset_rad==0);
     ScriptedRecenter q;q.update(in(0,true,std::nullopt,std::nullopt));o=q.update(in(.1,true,deg(35),1.5,1)); // a snap while pending is the entry aim
     assert(o.easing && o.reaims==0);o=q.update(in(.6,true,0,1.5,1));assert(near(o.offset_rad,deg(35)));
     ScriptedRecenter e;e.update(in(0,true,std::nullopt,std::nullopt));e.update(in(.1,false)); // exit clears pending
     o=e.update(in(.2,false));o=e.update(in(.3,true,std::nullopt,1.0));o=e.update(in(.4,true,deg(30),1.0));assert(o.entries==2 && o.easing);}
    {ScriptedRecenter r; // unseen_scene_folds_nothing: no heading, the caller passes no hmd (no view drawn), so the exit folds nothing
     for(int i=0;i<=60;++i){auto o=r.update(in(.01*i,true,std::nullopt,std::nullopt));assert(o.offset_rad==0 && !o.easing);}
     auto o=r.update(in(1,false,{},{},0,1,1.2));assert(!o.fold_rad && o.folds==0 && o.entries==1 && !o.residual_easing);}
    {ScriptedRecenter r; // instant_applies_full
     auto o=r.update(in(0,true,deg(50),1.0,0,1,{},true));assert(near(o.offset_rad,deg(50)) && !o.easing);}
    {ScriptedRecenter r; // reset
     r.update(in(0,true,deg(40),1.0));r.reset();assert(r.update(in(1,false)).offset_rad==0);}
    std::cout<<"PASS: scripted recenter ease, dead zone, re-aim, exit fold, epoch drop, wrap and instant\n";
}
