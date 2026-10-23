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
    expect(rows[ns::temporal].values.size()==7 && rows[ns::temporal].value==2,"anti-aliasing stepper on the current mode");
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
        expect(offered[ns::temporal].values.size()==9 && offered[ns::temporal].values[7]=="DLAA (NVIDIA DLSS)" && offered[ns::temporal].values[8]=="DLSS Quality (NVIDIA)" &&
            offered[ns::temporal].description.find("NVIDIA DLSS")!=std::string::npos,"DLSS rows and attribution when offered");
        std::vector<int> w;for(const auto& r:offered) w.push_back(r.kind==NativeOptionsPage::Kind::action?-1:r.value);
        w[ns::temporal]=8;expect(ns::apply(s,w).temporal_aa && s.temporal_aa()==8,"DLSS Quality applies when offered");
        s.dlss_available(false);const auto hidden=ns::rows(s);
        expect(hidden[ns::temporal].values.size()==7 && hidden[ns::temporal].value==6 && hidden[ns::temporal].description.find("NVIDIA")==std::string::npos,"a saved DLSS mode shows as the last offered row without DLSS");
        s.set_temporal_aa(2);w[ns::temporal]=7;expect(!ns::apply(s,w).temporal_aa && s.temporal_aa()==2,"DLSS modes are ignored when not offered");
    }
    const auto cal=ns::action(s,ns::calibrate);
    expect(cal.calibrate && s.open() && s.calibration_page(),"calibrate opens the calibration page");
    ControllerInput in{};in.active=true;s.update(in,false,true);in.b=true;s.update(in,false,true);
    expect(!s.open(),"B from a natively started calibration returns to the game's page");
    expect(ns::action(s,ns::recenter).recenter,"recenter action");
    VrSettings::Changes into{};ns::merge(into,c);expect(into.resolution && into.turning,"merge keeps reported changes");
    if(!failures) std::printf("PASS: native VR options rows mirror the settings, apply only valid changes, actions and calibration return\n");
    return failures?1:0;
}
