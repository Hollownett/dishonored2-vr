#define NOMINMAX
#include "../src/vr_settings_native.hpp"
#include <cstdio>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
using d2vr::NativeOptionsPage;
namespace ns=d2vr::native_settings;
// Applies a page read the way the runtime does: row 2 means what the page showing it was (the rows of the state read from).
static d2vr::VrSettings::Changes ap(d2vr::VrSettings& s,const std::vector<int>& v) {return ns::apply(s,v);}
static std::vector<int> page_values(const std::vector<NativeOptionsPage::Row>& rows) {
    std::vector<int> v;for(const auto& r:rows) v.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);return v;
}

int main() {
    using namespace d2vr;
    VrSettings s;s.initialize(1.2,false,{TurnControl::Mode::snap,45,90},MirrorMode::both_eyes,.5,2,65,true);
    const auto rows=ns::rows(s);
    expect(rows.size()==ns::count,"one row per setting and action");
    expect(rows[ns::resolution].kind==NativeOptionsPage::Kind::slider && rows[ns::resolution].value==120,"resolution slider in percent");
    expect(rows[ns::temporal].values.size()==10 && rows[ns::temporal].value==2,"anti-aliasing stepper on the current mode (Off..FSR 4 + SMAA + SSAA)");
    expect(rows[ns::hud].value==1 && rows[ns::turning].value==1 && rows[ns::mirror].value==2,"choices select the current values");
    expect(rows[ns::calibrate].kind==NativeOptionsPage::Kind::action,"calibration is an action row");
    for(const auto& r:rows) expect(!r.label.empty() && !r.description.empty(),"every row is labelled and described");
    expect(rows.size()<=NativeOptionsPage::max_rows,"every row is selectable in the game's 10-row list");
    std::vector<int> v;for(const auto& r:rows) v.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
    expect(!ap(s,v).resolution && !ap(s,v).hud_percent,"unchanged page changes nothing");
    v[ns::resolution]=150;v[ns::hud]=4;v[ns::turning]=6;v[ns::sharpening]=4;v[ns::mirror]=0;
    const auto c=ap(s,v);
    expect(c.resolution && *c.resolution==1.5 && s.requested()==1.5,"slider sets resolution");
    expect(c.hud_percent && s.hud_percent()==100,"stepper sets HUD size");
    expect(c.turning && s.turning().mode==TurnControl::Mode::native,"stepper sets turning");
    expect(c.sharpness && s.sharpness()==1 && c.mirror && s.mirror()==MirrorMode::right_eye,"steppers apply");
    {   VrSettings t;t.initialize(1,false,{},MirrorMode::right_eye,.5,1,65,true); // v1046: Game TAA: row 2 is the locked Upscaler scale slider
        auto tv=page_values(ns::rows(t));tv[ns::edge_aa]=70;const auto tc=ap(t,tv);
        expect(ns::rows(t)[ns::edge_aa].kind==NativeOptionsPage::Kind::slider && !tc.upscale_percent && !tc.antialiasing && !t.antialiasing(),"a locked slider value is ignored outside the upscaler modes");}
    v[ns::resolution]=999;v[ns::hud]=99;expect(!ap(s,v).resolution && !ap(s,v).hud_percent,"out-of-range values are ignored");
    {   // DLAA/DLSS Quality rows (with the NVIDIA attribution) appear only while DLSS is offered.
        s.dlss_available(true);const auto offered=ns::rows(s);
        expect(offered[ns::temporal].values.size()==12 && offered[ns::temporal].values[7]=="DLAA (NVIDIA DLSS)" && offered[ns::temporal].values[8]=="DLSS Quality (NVIDIA)" &&
            offered[ns::temporal].values[9]=="SMAA (edge anti-aliasing)" && offered[ns::temporal].values[10]=="SSAA 125% (MSAA-equivalent)" && offered[ns::temporal].values[11]=="SSAA 200% + SMAA (max clarity)" && offered[ns::temporal].description.find("NVIDIA DLSS")!=std::string::npos,"DLSS rows and attribution when offered");
        std::vector<int> w;for(const auto& r:offered) w.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
        w[ns::temporal]=8;expect(ap(s,w).temporal_aa && s.temporal_aa()==8,"DLSS Quality applies when offered");
        s.dlss_available(false);const auto hidden=ns::rows(s);
        expect(hidden[ns::temporal].values.size()==11 && hidden[ns::temporal].value==7 && hidden[ns::temporal].values[7]=="DLSS Quality (unavailable, FSR 3)" &&
            hidden[ns::temporal].values[8]=="SMAA (edge anti-aliasing)" && hidden[ns::temporal].values[9]=="SSAA 125% (MSAA-equivalent)" && hidden[ns::temporal].values[10]=="SSAA 200% + SMAA (max clarity)" && hidden[ns::temporal].description.find("NVIDIA")==std::string::npos,"a saved DLSS mode shows as its own unavailable entry without DLSS");
        // Applying the page's own current values (the user changed another row) never rewrites the saved DLSS mode.
        std::vector<int> current;for(const auto& r:hidden) current.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
        current[ns::hud]=0;const auto other=ap(s,current);
        expect(!other.temporal_aa && s.temporal_aa()==8 && other.hud_percent,"changing another row keeps a saved unoffered DLSS mode");
        s.set_temporal_aa(7);const auto dlaa=ns::rows(s);
        expect(dlaa[ns::temporal].values.size()==11 && dlaa[ns::temporal].value==7 && dlaa[ns::temporal].values[7]=="DLAA (unavailable, FSR 3)" && dlaa[ns::temporal].values[8]=="SMAA (edge anti-aliasing)" && dlaa[ns::temporal].values[9]=="SSAA 125% (MSAA-equivalent)" && dlaa[ns::temporal].values[10]=="SSAA 200% + SMAA (max clarity)","saved DLAA shows its own entry");
        current[ns::temporal]=7;expect(!ap(s,current).temporal_aa && s.temporal_aa()==7,"re-applying the unavailable entry keeps DLAA");
        current[ns::temporal]=6;expect(ap(s,current).temporal_aa && s.temporal_aa()==6,"stepping onto an offered row is the user's change");
        expect(ns::rows(s)[ns::temporal].values.size()==10,"no unavailable entry once an offered mode is saved (SMAA stays)");
        {   // v963: SMAA is a listed mode: temporal 0 + Edge AA on; Off turns Edge AA off.
            s.set_requested(1.0);
            const auto smaa_rows=ns::rows(s);std::vector<int> sv;for(const auto& r:smaa_rows) sv.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            sv[ns::temporal]=static_cast<int>(smaa_rows[ns::temporal].values.size())-3; // SMAA; then SSAA 125%, SSAA 150%
            const auto sc=ap(s,sv);
            expect(sc.temporal_aa && *sc.temporal_aa==0 && sc.antialiasing && *sc.antialiasing && s.temporal_aa()==0 && s.antialiasing(),"SMAA applies as temporal 0 + Edge AA on");
            expect(ns::rows(s)[ns::temporal].value==static_cast<int>(ns::rows(s)[ns::temporal].values.size())-3,"SMAA is shown as the current AA mode");
            std::vector<int> ss;for(const auto& r:ns::rows(s)) ss.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            ss[ns::temporal]=static_cast<int>(ns::rows(s)[ns::temporal].values.size())-2; // SSAA 125%
            const auto ssc=ap(s,ss);
            expect(ssc.temporal_aa && *ssc.temporal_aa==0 && ssc.antialiasing && !*ssc.antialiasing && !s.antialiasing() && ssc.resolution && *ssc.resolution==1.25 && s.requested()==1.25,"SSAA 125% sets temporal 0 + Edge AA off + 125% resolution");
            std::vector<int> s150;for(const auto& r:ns::rows(s)) s150.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            s150[ns::temporal]=static_cast<int>(ns::rows(s)[ns::temporal].values.size())-1; // SSAA 150%
            const auto s150c=ap(s,s150);
            expect(s150c.temporal_aa && *s150c.temporal_aa==0 && s150c.antialiasing && *s150c.antialiasing && s150c.resolution && *s150c.resolution==2.0 && s.requested()==2.0,"SSAA 200% + SMAA sets temporal 0 + Edge AA on + 200% resolution");
            std::vector<int> off;for(const auto& r:ns::rows(s)) off.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            off[ns::temporal]=0;const auto oc=ap(s,off);
            expect(oc.temporal_aa && *oc.temporal_aa==0 && oc.antialiasing && !*oc.antialiasing && !s.antialiasing(),"Off from SMAA turns Edge AA off too");
        }
        s.set_temporal_aa(2);w[ns::temporal]=99;expect(!ap(s,w).temporal_aa && s.temporal_aa()==2,"an out-of-range AA stepper index is ignored");
    }
    {   // v1041: row 2 is the Upscaler scale slider while the AA mode is an upscaler, the Edge AA toggle otherwise.
        using K=NativeOptionsPage::Kind;
        VrSettings u;u.initialize(1,false,{},MirrorMode::right_eye,.5,6,65,true);
        auto r=ns::rows(u);
        expect(r[ns::edge_aa].kind==K::slider && r[ns::edge_aa].label=="Upscaler scale" && r[ns::edge_aa].minimum==50 && r[ns::edge_aa].maximum==100 && r[ns::edge_aa].interval==1 && r[ns::edge_aa].value==77 && r.size()<=NativeOptionsPage::max_rows,"FSR 4 Quality shows the slider at its 77% preset");
        const std::pair<int,int> presets[]={{2,100},{3,77},{4,67},{5,100},{6,77},{7,100}};
        for(const auto& x:presets) {u.set_temporal_aa(x.first);expect(ns::rows(u)[ns::edge_aa].kind==K::slider && ns::rows(u)[ns::edge_aa].value==x.second,"every upscaler preset shows its percent");}
        u.dlss_available(true);u.set_temporal_aa(8);u.dlss_quality_percent(72);expect(ns::rows(u)[ns::edge_aa].value==72,"DLSS Quality's preset is NGX's optimal percent");
        for(int m=0;m<=8;++m) {u.set_temporal_aa(m);const auto e=ns::rows(u)[ns::edge_aa];
            expect(e.kind==K::slider && e.label=="Upscaler scale" && e.minimum==50 && e.maximum==100 && e.interval==1 && e.locked==!u.upscaler() && (!e.locked || e.value==100),"row 2 is the Upscaler scale slider in every mode, locked (100) iff not an upscaler");}
        u.set_temporal_aa(0);u.set_antialiasing(true);expect(ns::rows(u)[ns::edge_aa].locked && ns::rows(u)[ns::edge_aa].value==100,"SMAA locks the slider");
        u.dlss_available(false);u.set_temporal_aa(6);
        // Moving the slider: a custom scale for this upscaler, the stepper's current entry is relabelled, the render request is the scale's.
        auto pv=page_values(ns::rows(u));pv[ns::edge_aa]=60;
        auto c60=ap(u,pv);
        expect(c60.upscale_percent && *c60.upscale_percent==60 && !c60.temporal_aa && u.temporal_aa()==6 && u.upscale_custom_percent()==60 && u.upscale_percent()==60,"slider sets a custom scale without changing the mode");
        r=ns::rows(u);
        expect(r[ns::temporal].values[static_cast<std::size_t>(r[ns::temporal].value)]=="FSR 4 Custom (60%)" && r[ns::edge_aa].value==60,"the current entry reads FSR 4 Custom (60%)");
        expect(u.preferences().upscale_percent==60,"the custom scale is saved");
        pv=page_values(r);expect(!ap(u,pv).upscale_percent,"re-reading the same page changes nothing");
        pv[ns::edge_aa]=77;auto c77=ap(u,pv);
        expect(c77.upscale_percent && u.upscale_custom_percent()==0 && ns::rows(u)[ns::temporal].values[static_cast<std::size_t>(ns::rows(u)[ns::temporal].value)]=="FSR 4 Quality (77%)","back on the preset the entry is the preset again");
        pv[ns::edge_aa]=49;expect(!ap(u,pv).upscale_percent && u.upscale_percent()==77,"below 50 is ignored");pv[ns::edge_aa]=101;expect(!ap(u,pv).upscale_percent,"above 100 is ignored");
        pv[ns::edge_aa]=100;ap(u,pv);expect(u.upscale_percent()==100 && u.upscale_custom_percent()==100,"100 on a Quality entry is a custom native scale");
        // Picking another entry resets the scale to that entry's preset, even when the page still carries the old slider value.
        pv=page_values(ns::rows(u));pv[ns::temporal]=3;pv[ns::edge_aa]=100;
        auto pick=ap(u,pv);
        expect(pick.temporal_aa && *pick.temporal_aa==3 && !pick.upscale_percent && u.upscale_custom_percent()==0 && ns::rows(u)[ns::edge_aa].value==77,"picking FSR 3 Quality resets the scale to 77%");
        VrSettings off;off.initialize(1,true,{},MirrorMode::right_eye,.5,0,65,true);
        auto ov=page_values(ns::rows(off));ov[ns::edge_aa]=70;
        const auto stale=ns::apply(off,ov);
        expect(!stale.upscale_percent && !stale.antialiasing && off.antialiasing(),"a locked slider value on a non-upscaler mode changes nothing");
        // DLSS labels.
        VrSettings d;d.initialize(1,false,{},MirrorMode::right_eye,.5,8,65,true,{},false,true,true,CutsceneMode::vr_comfort,55);
        expect(d.upscale_custom_percent()==55 && ns::rows(d)[ns::temporal].values[static_cast<std::size_t>(ns::rows(d)[ns::temporal].value)]=="DLSS Custom (55%) (unavailable, FSR 3)","a saved DLSS scale reads truthfully when DLSS is unavailable");
        d.dlss_available(true);expect(ns::rows(d)[ns::temporal].values[static_cast<std::size_t>(ns::rows(d)[ns::temporal].value)]=="DLSS Custom (55%)","DLSS Custom label");
        VrSettings bad;bad.initialize(1,false,{},MirrorMode::right_eye,.5,1,65,true,{},false,true,true,CutsceneMode::vr_comfort,70);
        expect(bad.upscale_custom_percent()==0,"a stored scale on a non-upscaler mode is dropped");
        // The row switch while the page is open: the resolver turns the page's values into the rows they lead to (a copy of the settings).
        VrSettings g;g.initialize(1,false,{},MirrorMode::right_eye,.5,1,65,true);
        g.dlss_available(true);const auto resolve=ns::resolver(g);
        const auto kinds_of=[](const std::vector<NativeOptionsPage::Row>& rr) {std::vector<NativeOptionsPage::Kind> k;for(const auto& x:rr) k.push_back(x.kind);return k;};
        auto page=ns::rows(g);
        // v1046: no Anti-aliasing change reshows: Game TAA -> FSR 4 Quality -> Off -> SMAA -> DLSS Quality -> SSAA.
        const auto idx=[](const std::vector<NativeOptionsPage::Row>& rr,const char* n) {for(std::size_t i=0;i<rr[ns::temporal].values.size();++i) if(rr[ns::temporal].values[i].rfind(n,0)==0) return static_cast<int>(i);return -1;};
        for(const char* name:{"FSR 4 Quality","Off","SMAA","DLSS Quality","SSAA 200%","Game TAA"}) {
            auto qv=page_values(page);qv[ns::temporal]=idx(page,name);if(qv[ns::temporal]<0) std::printf("missing %s\n",name);
            const auto next=resolve(qv,kinds_of(page));const auto pl=NativeOptionsPage::plan(page,next,qv);
            const bool up=std::string(name).find("FSR")==0 || std::string(name).find("DLSS")==0;
            expect(!pl.reshow && next[ns::edge_aa].kind==K::slider && next[ns::edge_aa].locked==!up && (up || next[ns::edge_aa].value==100),"AA switch is an in-place touch with the slider locked iff upscaler");
            if(page[ns::edge_aa].locked!=next[ns::edge_aa].locked) expect(pl.touch[ns::edge_aa]==1,"a locked change alone touches row 2");
            page=next;}
        {   VrSettings t;t.initialize(1,false,{},MirrorMode::right_eye,.5,1,65,true);const auto rt=ns::resolver(t);auto p0=ns::rows(t);auto tv6=page_values(p0);tv6[ns::temporal]=6;
            const auto n=rt(tv6,kinds_of(p0));const auto pl=NativeOptionsPage::plan(p0,n,tv6);
            expect(!pl.reshow && pl.touch[ns::edge_aa]==1 && !n[ns::edge_aa].locked && n[ns::edge_aa].value==77,"Game TAA -> FSR 4 Quality unlocks row 2 at 77 without a reshow");}
        // Dragging the unlocked slider relabels the stepper entry in place; the slider itself is not rewritten.
        {   VrSettings f;f.initialize(1,false,{},MirrorMode::right_eye,.5,6,65,true);const auto rf=ns::resolver(f);auto p0=ns::rows(f);auto sv=page_values(p0);sv[ns::edge_aa]=64;
            const auto dr=rf(sv,kinds_of(p0));const auto pl=NativeOptionsPage::plan(p0,dr,sv);
            expect(!pl.reshow && pl.touched && pl.touch[ns::temporal]==1 && pl.touch[ns::edge_aa]==0 && dr[ns::temporal].values[6]=="FSR 4 Custom (64%)","dragging relabels the entry in place");
            auto pv2=page_values(dr);pv2[ns::temporal]=0;const auto offr=rf(pv2,kinds_of(dr));
            expect(!NativeOptionsPage::plan(dr,offr,pv2).reshow && offr[ns::edge_aa].locked && offr[ns::edge_aa].value==100,"upscaler -> Off locks the slider in place");
            auto steady=page_values(offr);expect(!NativeOptionsPage::plan(offr,rf(steady,kinds_of(offr)),steady).touched,"a settled page is left alone");}
        {   // Restore: defaults (upscaler FSR 3 Native, 100) from a locked page need no reshow.
            VrSettings o;o.initialize(1,false,{},MirrorMode::right_eye,.5,0,65,true);const auto ro=ns::resolver(o);auto locked=ns::rows(o);
            auto dv=page_values(locked);for(std::size_t i=0;i<locked.size();++i) if(locked[i].kind!=K::action) dv[i]=locked[i].fallback;
            const auto def=ro(dv,kinds_of(locked));
            expect(!NativeOptionsPage::plan(locked,def,dv).reshow && def.size()<=NativeOptionsPage::max_rows,"restore defaults needs no reshow");}
    }
    const auto cal=ns::action(s,ns::calibrate);
    expect(cal.calibrate && s.open() && s.calibration_page(),"calibrate opens the calibration page");
    ControllerInput in{};in.active=true;s.update(in,false,true);in.b=true;s.update(in,false,true);
    expect(!s.open(),"B from a natively started calibration returns to the game's page");
    expect(ns::action(s,ns::recenter).recenter,"recenter action");
    expect(ns::action(s,ns::controls_hint).controls_guide && ns::rows(s)[ns::controls_hint].kind==NativeOptionsPage::Kind::action,"the Controls & gestures row opens the controls page");
    s.set_controls_hint(false);const auto guide=ns::action(s,ns::controls_hint);
    expect(guide.controls_guide && !guide.controls_hint && !s.controls_hint(),"opening the guide preserves a disabled saved hint preference");
    VrSettings::Changes into{};ns::merge(into,c);expect(into.resolution && into.turning,"merge keeps reported changes");
    ns::merge(into,guide);expect(into.controls_guide,"merge carries the guide action to the runtime");
    // v1073: the game shows the drawn panel only for a calibration started from the game's own page (GameXr closes anything else).
    {
        VrSettings p;ControllerInput c2{};c2.active=true;
        p.calibration_runtime(AvatarFitRuntimeStatus::recalibration_required);
        p.update(c2,false,true);p.update(c2,false,true);   // a menu frame: the panel's own recalibration prompt
        expect(!p.open() || !p.native_calibration(),"the automatic recalibration prompt is never the game's Calibrate page (GameXr closes it)");
        p.suspend();ns::action(p,ns::calibrate);
        expect(p.open() && p.calibration_page() && p.native_calibration(),"the Calibrate row opens the one page the game may show");
        p.suspend();
        expect(!p.native_calibration(),"a suspended calibration forgets it was native (a later prompt cannot pass as one)");
    }
    if(!failures) std::printf("PASS: native VR options rows mirror the settings, apply only valid changes, actions and calibration return\n");
    return failures?1:0;
}

