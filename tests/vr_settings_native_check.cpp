#define NOMINMAX
#include "../src/vr_settings_native.hpp"
#include <cstdio>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    using namespace d2vr;namespace ns=native_settings;
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
    expect(!ns::apply(s,v).resolution && !ns::apply(s,v).hud_percent,"unchanged page changes nothing");
    v[ns::resolution]=150;v[ns::hud]=4;v[ns::turning]=6;v[ns::edge_aa]=1;v[ns::sharpening]=4;v[ns::mirror]=0;
    const auto c=ns::apply(s,v);
    expect(c.resolution && *c.resolution==1.5 && s.requested()==1.5,"slider sets resolution");
    expect(c.hud_percent && s.hud_percent()==100,"stepper sets HUD size");
    expect(c.turning && s.turning().mode==TurnControl::Mode::native,"stepper sets turning");
    expect(c.antialiasing && s.antialiasing() && c.sharpness && s.sharpness()==1 && c.mirror && s.mirror()==MirrorMode::right_eye,"toggles and steppers apply");
    v[ns::resolution]=999;v[ns::hud]=99;expect(!ns::apply(s,v).resolution && !ns::apply(s,v).hud_percent,"out-of-range values are ignored");
    {   // DLAA/DLSS Quality rows (with the NVIDIA attribution) appear only while DLSS is offered.
        s.dlss_available(true);const auto offered=ns::rows(s);
        expect(offered[ns::temporal].values.size()==12 && offered[ns::temporal].values[7]=="DLAA (NVIDIA DLSS)" && offered[ns::temporal].values[8]=="DLSS Quality (NVIDIA)" &&
            offered[ns::temporal].values[9]=="SMAA (edge anti-aliasing)" && offered[ns::temporal].values[10]=="SSAA 125% (MSAA-equivalent)" && offered[ns::temporal].values[11]=="SSAA 200% + SMAA (max clarity)" && offered[ns::temporal].description.find("NVIDIA DLSS")!=std::string::npos,"DLSS rows and attribution when offered");
        std::vector<int> w;for(const auto& r:offered) w.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
        w[ns::temporal]=8;expect(ns::apply(s,w).temporal_aa && s.temporal_aa()==8,"DLSS Quality applies when offered");
        s.dlss_available(false);const auto hidden=ns::rows(s);
        expect(hidden[ns::temporal].values.size()==11 && hidden[ns::temporal].value==7 && hidden[ns::temporal].values[7]=="DLSS Quality (unavailable, FSR 3)" &&
            hidden[ns::temporal].values[8]=="SMAA (edge anti-aliasing)" && hidden[ns::temporal].values[9]=="SSAA 125% (MSAA-equivalent)" && hidden[ns::temporal].values[10]=="SSAA 200% + SMAA (max clarity)" && hidden[ns::temporal].description.find("NVIDIA")==std::string::npos,"a saved DLSS mode shows as its own unavailable entry without DLSS");
        // Applying the page's own current values (the user changed another row) never rewrites the saved DLSS mode.
        std::vector<int> current;for(const auto& r:hidden) current.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
        current[ns::hud]=0;const auto other=ns::apply(s,current);
        expect(!other.temporal_aa && s.temporal_aa()==8 && other.hud_percent,"changing another row keeps a saved unoffered DLSS mode");
        s.set_temporal_aa(7);const auto dlaa=ns::rows(s);
        expect(dlaa[ns::temporal].values.size()==11 && dlaa[ns::temporal].value==7 && dlaa[ns::temporal].values[7]=="DLAA (unavailable, FSR 3)" && dlaa[ns::temporal].values[8]=="SMAA (edge anti-aliasing)" && dlaa[ns::temporal].values[9]=="SSAA 125% (MSAA-equivalent)" && dlaa[ns::temporal].values[10]=="SSAA 200% + SMAA (max clarity)","saved DLAA shows its own entry");
        current[ns::temporal]=7;expect(!ns::apply(s,current).temporal_aa && s.temporal_aa()==7,"re-applying the unavailable entry keeps DLAA");
        current[ns::temporal]=6;expect(ns::apply(s,current).temporal_aa && s.temporal_aa()==6,"stepping onto an offered row is the user's change");
        expect(ns::rows(s)[ns::temporal].values.size()==10,"no unavailable entry once an offered mode is saved (SMAA stays)");
        {   // v963: SMAA is a listed mode: temporal 0 + Edge AA on; Off turns Edge AA off.
            s.set_requested(1.0);
            const auto smaa_rows=ns::rows(s);std::vector<int> sv;for(const auto& r:smaa_rows) sv.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            sv[ns::temporal]=static_cast<int>(smaa_rows[ns::temporal].values.size())-3; // SMAA; then SSAA 125%, SSAA 150%
            const auto sc=ns::apply(s,sv);
            expect(sc.temporal_aa && *sc.temporal_aa==0 && sc.antialiasing && *sc.antialiasing && s.temporal_aa()==0 && s.antialiasing(),"SMAA applies as temporal 0 + Edge AA on");
            expect(ns::rows(s)[ns::temporal].value==static_cast<int>(ns::rows(s)[ns::temporal].values.size())-3,"SMAA is shown as the current AA mode");
            std::vector<int> ss;for(const auto& r:ns::rows(s)) ss.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            ss[ns::temporal]=static_cast<int>(ns::rows(s)[ns::temporal].values.size())-2; // SSAA 125%
            const auto ssc=ns::apply(s,ss);
            expect(ssc.temporal_aa && *ssc.temporal_aa==0 && ssc.antialiasing && !*ssc.antialiasing && !s.antialiasing() && ssc.resolution && *ssc.resolution==1.25 && s.requested()==1.25,"SSAA 125% sets temporal 0 + Edge AA off + 125% resolution");
            std::vector<int> s150;for(const auto& r:ns::rows(s)) s150.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            s150[ns::temporal]=static_cast<int>(ns::rows(s)[ns::temporal].values.size())-1; // SSAA 150%
            const auto s150c=ns::apply(s,s150);
            expect(s150c.temporal_aa && *s150c.temporal_aa==0 && s150c.antialiasing && *s150c.antialiasing && s150c.resolution && *s150c.resolution==2.0 && s.requested()==2.0,"SSAA 200% + SMAA sets temporal 0 + Edge AA on + 200% resolution");
            std::vector<int> off;for(const auto& r:ns::rows(s)) off.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
            off[ns::temporal]=0;const auto oc=ns::apply(s,off);
            expect(oc.temporal_aa && *oc.temporal_aa==0 && oc.antialiasing && !*oc.antialiasing && !s.antialiasing(),"Off from SMAA turns Edge AA off too");
        }
        s.set_temporal_aa(2);w[ns::temporal]=99;expect(!ns::apply(s,w).temporal_aa && s.temporal_aa()==2,"an out-of-range AA stepper index is ignored");
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
    if(!failures) std::printf("PASS: native VR options rows mirror the settings, apply only valid changes, actions and calibration return\n");
    return failures?1:0;
}

