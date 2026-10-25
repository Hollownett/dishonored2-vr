#include "../src/cutscene_view.hpp"
#include "../src/head_calibration.hpp"
#include "../src/scripted_recenter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
static constexpr double kPi=3.14159265358979323846;
static double deg(double d){return d*kPi/180;}
static bool near(double a,double b,double e=1e-4){return std::fabs(a-b)<e;}
static GameCameraPose scene(double yaw,double pitch=0,double roll=0,std::array<float,3> at={10,20,30}) {
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const Vec3 f{cy*cp,sy*cp,sp},l0{-sy,cy,0},u0{-cy*sp,-sy*sp,cp};
    const double cr=std::cos(roll),sr=std::sin(roll);
    const Vec3 l=l0*cr+u0*sr,u=u0*cr-l0*sr;
    GameCameraPose p{};p.position=at;p.horizontal_fov=1.4f;p.vertical_fov=.9f;
    p.basis={float(f.x),float(f.y),float(f.z),float(l.x),float(l.y),float(l.z),float(u.x),float(u.y),float(u.z)};
    return p;
}
static Quaternion about_y(double a){return {0,std::sin(a/2),0,std::cos(a/2)};}
static Vec3 row(const GameCameraPose& p,int i){return {p.basis[i*3],p.basis[i*3+1],p.basis[i*3+2]};}
static ScriptedRecenterInput in(double t,bool authored,std::optional<double> rel,std::optional<double> heading,std::uint64_t snaps,
    std::optional<double> body={},bool instant=false){return {t,authored,rel,heading,snaps,1,body,instant};}
int main(){
    static_assert(cut_jump_m==.5 && cut_yaw_rad==kPi/6,"cut thresholds: > 0.5 m or >= 30 degrees in one frame");
    static_assert(cutscene_vignette_onset_mps==.45 && ComfortVignette::full_move_mps==3.,"the locomotion vignette's onset (0.15 x 3 m/s)");
    {   // cut_thresholds
        const auto a=scene(.3);auto b=a;
        b.position[0]+=.49f;assert(!detect_cut(a,b,1).cut && near(detect_cut(a,b,1).jump_m,.49,1e-5));
        b=a;b.position[1]+=.51f;assert(detect_cut(a,b,1).cut && near(detect_cut(a,b,1).jump_m,.51,1e-5));
        assert(!detect_cut(a,scene(.3+deg(29.9)),1).cut);
        assert(detect_cut(a,scene(.3+deg(30.1)),1).cut && near(detect_cut(a,scene(.3+deg(30.1)),1).jump_yaw_rad,deg(30.1)));
        assert(detect_cut(a,scene(.3-deg(30.1)),1).cut); // either way
        assert(!detect_cut(scene(kPi-deg(10)),scene(-kPi+deg(10)),1).cut); // 20 degrees across the wrap, not 340
        assert(!detect_cut(a,scene(.3,deg(60)),1).cut);                   // a pitch-only move is not a yaw cut
        b=a;b.position[0]+=40;assert(!detect_cut(a,b,100).cut && detect_cut(a,b,50).cut); // units per metre
        b=a;b.position[2]=std::nanf("");assert(!detect_cut(a,b,1).cut);
    }
    const Quaternion origin=about_y(.4); // the room forward, yaw only
    const TrackedPose head{{.1,1.6,-.2},origin};
    {   // full_view_identity_head_equals_scene (pitch and roll kept)
        const auto s=scene(.7,deg(-20),deg(8));
        const auto v=cutscene_full_view(s,head,head,origin,1);
        for(int i=0;i<3;++i) assert(near(v.position[i],s.position[i]));
        for(int i=0;i<9;++i) assert(near(v.basis[i],s.basis[i]));
        assert(v.horizontal_fov==s.horizontal_fov && v.vertical_fov==s.vertical_fov);
    }
    {   // full_view_adds_head_yaw (about the scene's own up axis)
        const auto s=scene(.3);
        const TrackedPose turned{head.position,multiplied(about_y(.25),origin)};
        const auto v=cutscene_full_view(s,turned,turned,origin,1);
        assert(near(std::atan2(v.basis[1],v.basis[0]),.55) && near(v.basis[2],0));
        const auto p=scene(.3,deg(-30)); // pitched scene: forward turns toward the scene's left by the head yaw
        const auto w=cutscene_full_view(p,turned,turned,origin,1);
        const Vec3 want=row(p,0)*std::cos(.25)+row(p,1)*std::sin(.25);
        for(int i=0;i<3;++i) assert(near(w.basis[i],i==0?want.x:i==1?want.y:want.z));
        const TrackedPose nod{head.position,multiplied(origin,{std::sin(-.1),0,0,std::cos(-.1)})}; // head pitch down 0.2 adds to the scene's
        const auto n=cutscene_full_view(p,nod,nod,origin,1);
        assert(near(std::asin(n.basis[2]),deg(-30)-.2));
    }
    {   // full_view_eye_offset: the right eye sits to the scene's right, the physical offset is turned into the scene frame
        const auto s=scene(.3);
        const TrackedPose centre{{0,1.6,0},origin};
        const Vec3 right_xr=rotated(origin,{.032,0,0});
        const TrackedPose eye{centre.position+right_xr,origin};
        const auto v=cutscene_full_view(s,centre,eye,origin,1);
        const Vec3 d{v.position[0]-s.position[0],v.position[1]-s.position[1],v.position[2]-s.position[2]};
        assert(near(dot(d,row(s,1)),-.032) && near(length(d),.032));
        const auto lean=cutscene_full_view(s,centre,centre,origin,1,rotated(origin,{0,0,-.1})); // 10 cm forward
        assert(near(lean.position[0]-s.position[0],.1*row(s,0).x) && near(lean.position[1]-s.position[1],.1*row(s,0).y));
        bool threw=false;try{cutscene_full_view(s,centre,{centre.position+Vec3{.2,0,0},origin},origin,1);}catch(const std::invalid_argument&){threw=true;}
        assert(threw);threw=false;
        try{cutscene_full_view(s,centre,centre,{.1,0,0,1},1);}catch(const std::invalid_argument&){threw=true;}assert(threw);threw=false;
        auto bad=s;bad.basis[0]=2;try{cutscene_full_view(bad,centre,centre,origin,1);}catch(const std::invalid_argument&){threw=true;}assert(threw);
    }
    {   // comfort_cut_reaims_instantly: a cut re-latches the heading and the re-aim lands in full on that frame
        AuthoredHeading h;ScriptedRecenter r;
        auto y=h.update(scene(0),true,1);auto o=r.update(in(0,true,0.,y,h.snaps()));assert(o.entries==1 && near(o.offset_rad,0));
        o=r.update(in(.5,true,deg(50),y,h.snaps())); // the player looked 50 degrees away during the shot: no re-aim without a cut
        assert(near(o.offset_rad,0) && o.reaims==0);
        const auto cut_to=scene(deg(10),0,0,{12,20,30}); // a 2 m cut whose yaw moved only 10 degrees: AuthoredHeading alone would not snap
        y=h.update(cut_to,true,1);assert(h.snaps()==0 && near(*y,0));
        y=h.cut_to(cut_to);assert(h.snaps()==1 && near(*y,deg(10)));
        o=r.update(in(.51,true,deg(50),y,h.snaps(),{},true),{.17453292519943295,.4,true});
        assert(o.reaims==1 && !o.easing && near(o.offset_rad,deg(50))); // the full delta on the cut frame
        o=r.update(in(.6,true,0.,y,h.snaps()),{.17453292519943295,.4,true});assert(near(o.offset_rad,deg(50)) && !o.easing);
        AuthoredHeading idle;assert(!idle.cut_to(cut_to) && idle.snaps()==0); // outside a scene: nothing to re-latch
    }
    {   // pan_without_cut_no_reaim: a smooth dolly and pan is no cut; below 30 degrees of drift the heading holds and nothing re-aims
        CutsceneViewTracker t;AuthoredHeading h;ScriptedRecenter r;
        std::uint64_t cuts=0;std::optional<double> y;ScriptedRecenterOutput o{};
        for(int i=0;i<=56;++i) { // 0.5 degrees and 2 cm per frame: 28 degrees
            const auto s=scene(deg(.5*i),0,0,{10+.02f*i,20,30});
            const auto step=t.step(true,CutsceneMode::vr_comfort,100+i,7,s);
            cuts+=step.cut.cut;
            y=h.update(s,true,1);o=r.update(in(i*.01,true,deg(20),y,h.snaps(),{},step.instant),{.17453292519943295,.4,true});
        }
        assert(cuts==0 && t.cuts()==0 && h.snaps()==0 && o.reaims==0 && near(*y,0));
        // past 30 degrees of drift: a re-aim inside a continuous shot, eased from the last view (continuous_reaim), never a step
        double last_view=*y+(deg(20)-o.offset_rad);bool eased=false;
        for(int i=57;i<=110;++i) { // up to 55 degrees: one snap (near 30), the ease (0.4 s) done
            const auto s=scene(deg(.5*i),0,0,{10+.02f*i,20,30});
            const auto step=t.step(true,CutsceneMode::vr_comfort,100+i,7,s);assert(!step.cut.cut && !step.instant);
            y=h.update(s,true,1);
            const double rel=deg(20)-o.offset_rad; // the gaze stays 20 degrees off the room forward
            o=r.update(in(i*.01,true,rel,y,h.snaps(),{},false),{.17453292519943295,.4,true});
            const double view=*y+(deg(20)-o.offset_rad);
            assert(std::fabs(view-last_view)<deg(2)); // no 30 degree step on the snap frame
            eased|=o.easing;last_view=view;
        }
        assert(h.snaps()==1 && o.reaims==1 && eased);
        ScriptedRecenter g; // G1 (continuous_reaim off) keeps its reviewed behaviour: the snap shows at once
        g.update(in(0,true,0.,0.,0));const auto s1=g.update(in(.1,true,deg(20),deg(31),1));assert(s1.reaims==1 && near(s1.offset_rad,0));
    }
    {   // tracker: entry, mode latch, instant entry after a gap, cut only between consecutive frames of one owner
        CutsceneViewTracker t;
        auto s=t.step(false,CutsceneMode::vr_full,10,1,scene(0));assert(s.kind==CutsceneViewKind::none && !s.entry);
        s=t.step(true,CutsceneMode::vr_full,11,1,scene(0));assert(s.kind==CutsceneViewKind::full && s.entry && !s.instant); // follows gameplay
        s=t.step(true,CutsceneMode::vr_comfort,12,1,scene(0));assert(s.kind==CutsceneViewKind::full && !s.entry); // latched for the cutscene
        s=t.step(true,CutsceneMode::vr_full,13,1,scene(0,0,0,{11,20,30}));assert(s.cut.cut && s.instant && t.cuts()==1);
        s=t.step(false,CutsceneMode::vr_comfort,14,1,scene(0));assert(s.kind==CutsceneViewKind::none && t.kind()==CutsceneViewKind::none);
        s=t.step(false,CutsceneMode::vr_comfort,15,1,scene(deg(90),0,0,{30,20,30}));assert(!s.cut.cut && t.cuts()==1); // gameplay (a snap turn, a teleport): no cut
        s=t.step(true,CutsceneMode::vr_comfort,40,1,scene(0));assert(s.kind==CutsceneViewKind::comfort && s.entry && s.instant); // after a load
        s=t.step(true,CutsceneMode::vr_comfort,41,2,scene(0,0,0,{50,20,30}));assert(!s.cut.cut && !s.instant && t.cuts()==1); // another owner
        s=t.step(true,CutsceneMode::vr_comfort,42,2,scene(0,0,0,{50,20,30}));assert(!s.cut.cut);
        s=t.step(true,CutsceneMode::vr_comfort,45,2,scene(deg(90),0,0,{50,20,30}));assert(!s.cut.cut); // 3 Presents apart: not consecutive
        s=t.step(true,CutsceneMode::vr_comfort,46,2,scene(deg(90),0,0,{50,20,30}));
        assert(!s.cut.cut && near(t.last_cut().jump_m,1,1e-5) && t.entries()==2);
    }
    {   // scene motion: steady speed, no spike from a cut, a gap starts afresh; the vignette's onset
        SceneMotion m;double v=0;
        for(int i=0;i<=100;++i) v=m.update({float(.01*i),0,0},i*.01,false); // 1 m/s for 1 s
        assert(near(v,1,1e-3));
        v=m.update({50,0,0},1.01,true);assert(near(v,1,1e-3)); // a 49 m cut is not motion
        v=m.update({50.01f,0,0},1.02,false);assert(near(v,1,2e-2));
        v=m.update({50.01f,0,0},2,false);assert(v==0); // a 1 s gap
        assert(cutscene_vignette_speed(.45)==0 && near(cutscene_vignette_speed(.46),.46) && cutscene_vignette_speed(std::nan(""))==0);
    }
    {   // provisional_origin_rebase_keeps_the_view: the composed origin yaw (offset + origin) is the same after the room origin replaces the
        // provisional one, mid-ease too, and the ease still ends on the same composed target
        ScriptedRecenter r;r.update(in(0,false,{},{},0,0.));
        auto o=r.update(in(1,true,deg(40),0.,0));assert(o.easing);
        o=r.update(in(1.2,true,deg(40),0.,0));const double provisional=deg(25),room=deg(-15);
        const double before=o.offset_rad+provisional;
        r.rebase(provisional-room); // tracked_yaw(provisional, room)
        o=r.update(in(1.2,true,0.,0.,0));assert(near(o.offset_rad+room,before) && o.easing);
        o=r.update(in(1.5,true,0.,0.,0));assert(near(o.offset_rad+room,deg(40)+provisional) && !o.easing);
        r.rebase(std::nan(""));o=r.update(in(1.6,true,0.,0.,0));assert(near(o.offset_rad+room,deg(40)+provisional));
        // the exit fold after the rebase keeps the cutscene's last view: gameplay = body + hmd - (room + fold) = heading + hmd - composed
        o=r.update(in(1.7,false,{},{},0,deg(70)));assert(o.fold_rad && near(room+*o.fold_rad,deg(40)+provisional+deg(70)));
    }
    {   // VR full follows the scene's yaw every frame without counting snaps; a re-latch outside a scene does nothing
        AuthoredHeading h;h.update(scene(0),true,1);
        for(int i=1;i<=90;++i) {h.update(scene(deg(i)),true,1);const auto y=h.cut_to(scene(deg(i)),false);assert(y && near(*y,deg(i)));}
        assert(h.snaps()==0);
    }
    {   // the room origin is never first-calibrated by a cutscene: holds() answers without capturing
        HeadCalibration c;const TrackedPose pose{{0,1.6,0},about_y(.3)};
        assert(!c.holds(1,2,3,4) && c.captures()==0);
        c.get(pose,1,2,3,4,true);assert(c.holds(1,2,3,4) && !c.holds(1,2,3,5) && !c.holds(2,2,3,4) && c.captures()==1);
    }
    std::cout<<"cutscene_view_check passed\n";
}
