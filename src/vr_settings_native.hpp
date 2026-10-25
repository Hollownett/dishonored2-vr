#pragma once
// VR settings as rows of the game's own options page (native_options_page.hpp) and back: the same values, choices and
// consequences as the drawn panel (settings_panel_image.hpp), with the game's widgets.
#include "vr_settings.hpp"
#include "native_options_page.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace d2vr {
namespace native_settings {
// At most NativeOptionsPage::max_rows rows: the game's list cannot select more (test unlock lives on the launcher Tools page).
enum Row : int {resolution,temporal,edge_aa,mirror,sharpening,hud,controls_hint,turning,calibrate,recenter,count};
inline constexpr std::array<int,8> hud_choices{{55,65,75,85,100,125,150,200}};
inline constexpr std::array<TurnControl::Config,7> turning_choices{{
    {TurnControl::Mode::snap,30,90},{TurnControl::Mode::snap,45,90},{TurnControl::Mode::snap,90,90},
    {TurnControl::Mode::smooth,45,45},{TurnControl::Mode::smooth,45,90},{TurnControl::Mode::smooth,45,120},
    {TurnControl::Mode::native,45,90}}};
inline int turning_index(const TurnControl::Config& t) {
    for(int i=0;i<static_cast<int>(turning_choices.size());++i) {
        const auto& c=turning_choices[static_cast<std::size_t>(i)];
        if(c.mode==t.mode && (c.mode==TurnControl::Mode::native || (c.mode==TurnControl::Mode::snap?c.snap_degrees==t.snap_degrees:
           c.smooth_degrees_per_second==t.smooth_degrees_per_second))) return i;
    }
    return 1;
}
inline int hud_index(int percent) {
    for(int i=0;i<static_cast<int>(hud_choices.size());++i) if(hud_choices[static_cast<std::size_t>(i)]==percent) return i;
    return 1;
}
// The stepper index shown for the saved anti-aliasing mode: a DLSS mode that is not offered sits on its own
// "(unavailable)" entry at index 7 instead of being clamped onto FSR 4 Quality.
inline int temporal_shown(const VrSettings& s) {return !s.dlss_available() && s.temporal_aa()>=7?7:s.temporal_aa();}
inline std::vector<NativeOptionsPage::Row> rows(const VrSettings& s) {
    using K=NativeOptionsPage::Kind;std::vector<NativeOptionsPage::Row> r(count);
    r[resolution]={K::slider,"Resolution","Sets how many pixels each eye renders (percent). Higher is sharper but slower; the mod caps it to what the GPU can hold.",
        50,200,5,static_cast<int>(std::lround(s.requested()*100)),100,{}};
    // DLAA/DLSS Quality are offered only with an NVIDIA GPU and the user's nvngx_dlss.dll; a saved DLSS mode shows as the
    // last offered row elsewhere (the mod renders it with FSR 3 then).
    std::vector<std::string> aa_modes{"Off","Game TAA","FSR 3 Native","FSR 3 Quality (77%)","FSR 3 Balanced (67%)","FSR 4 Native","FSR 4 Quality (77%)"};
    std::string aa_help="Smooths shimmering edges over time. Game TAA is the original; FSR Native uses full resolution; Quality and Balanced render fewer pixels and upscale.";
    if(s.dlss_available()) {aa_modes.push_back("DLAA (NVIDIA DLSS)");aa_modes.push_back("DLSS Quality (NVIDIA)");aa_help+=" DLAA and DLSS Quality are powered by NVIDIA DLSS.";}
    else if(s.temporal_aa()>=7) aa_modes.push_back(s.temporal_aa()==7?"DLAA (unavailable, FSR 3)":"DLSS Quality (unavailable, FSR 3)"); // the saved choice, shown truthfully and never rewritten by the page
    r[temporal]={K::stepper,"Anti-aliasing",aa_help,0,0,1,temporal_shown(s),2,aa_modes};
    r[edge_aa]={K::toggle,"Edge anti-aliasing (SMAA)","SMAA Ultra cleans up jagged edges that the temporal pass misses.",0,0,1,s.antialiasing()?1:0,0,{}};
    r[mirror]={K::stepper,"Monitor view","Chooses what the desktop monitor mirrors. It does not change the headset image.",
        0,0,1,static_cast<int>(s.mirror()),static_cast<int>(MirrorMode::both_eyes),{"Right eye","Left eye","Both eyes","Off"}};
    r[sharpening]={K::stepper,"Sharpening","Contrast sharpening after anti-aliasing. Raise it if the image looks soft, lower it if edges crawl.",
        0,0,1,static_cast<int>(std::lround(s.sharpness()*4)),2,{"Off","Low","Medium","High","Max"}};
    r[hud]={K::stepper,"HUD size","Scales the HUD and prompts in VR. 55% sits in the centre of your view; 100% keeps the game layout; larger sizes enlarge prompts and markers.",
        0,0,1,hud_index(s.hud_percent()),1,{"55% (centre)","65%","75%","85%","100% (game layout)","125%","150%","200%"}};
    r[controls_hint]={K::toggle,"Controls hint","Shows controller button hints in the world during play.",0,0,1,s.controls_hint()?1:0,1,{}};
    r[turning]={K::stepper,"Turning","Snap rotates in fixed steps and is comfortable; Smooth turns continuously; Native game keeps the original stick turn.",
        0,0,1,turning_index(s.turning()),1,{"Snap 30\xC2\xB0","Snap 45\xC2\xB0","Snap 90\xC2\xB0","Smooth 45\xC2\xB0/s","Smooth 90\xC2\xB0/s","Smooth 120\xC2\xB0/s","Native game"}};
    // Actions: press right to run ("Select" -> "Starting..."); reopening the page shows "Select" again.
    r[calibrate]={K::action,"Calibrate avatar","Measure your shoulders, then your arm reach, so the body matches yours. Press right, then follow the avatar's pose.",0,0,1,0,0,{"Select \xE2\x96\xBA","Starting\xE2\x80\xA6"}};
    r[recenter]={K::action,"Recenter view","Re-aligns the view to where you face now. Press right.",0,0,1,0,0,{"Select \xE2\x96\xBA","Recentred"}};
    return r;
}
// Values read from the page (index = row, -1 = no value): apply what changed, reported like the panel's own changes.
inline VrSettings::Changes apply(VrSettings& s,const std::vector<int>& v) {
    VrSettings::Changes c{};
    const auto at=[&](int row) {return row<static_cast<int>(v.size())?v[static_cast<std::size_t>(row)]:-1;};
    if(const int x=at(resolution);x>=50 && x<=200 && std::lround(s.requested()*100)!=x) {s.set_requested(x/100.);c.resolution=s.requested();}
    // Compared against the displayed index: an unoffered saved DLSS mode (shown as the index-7 "(unavailable)" entry)
    // only changes when the user steps onto an offered row; the page never rewrites it on its own.
    if(const int x=at(temporal);x>=0 && x<=(s.dlss_available()?8:temporal_shown(s)==7?7:6) && x!=temporal_shown(s)) {s.set_temporal_aa(x);c.temporal_aa=x;}
    if(const int x=at(edge_aa);x>=0 && (x!=0)!=s.antialiasing()) {s.set_antialiasing(x!=0);c.antialiasing=x!=0;}
    if(const int x=at(mirror);x>=0 && x<=3 && x!=static_cast<int>(s.mirror())) {s.set_mirror(static_cast<MirrorMode>(x));c.mirror=s.mirror();}
    if(const int x=at(sharpening);x>=0 && x<=4 && x!=std::lround(s.sharpness()*4)) {s.set_sharpness(x/4.);c.sharpness=s.sharpness();}
    if(const int x=at(hud);x>=0 && x<static_cast<int>(hud_choices.size()) && hud_choices[static_cast<std::size_t>(x)]!=s.hud_percent())
        {s.set_hud_percent(hud_choices[static_cast<std::size_t>(x)]);c.hud_percent=s.hud_percent();}
    if(const int x=at(controls_hint);x>=0 && (x!=0)!=s.controls_hint()) {s.set_controls_hint(x!=0);c.controls_hint=x!=0;}
    if(const int x=at(turning);x>=0 && x<static_cast<int>(turning_choices.size()) && x!=turning_index(s.turning()))
        {s.set_turning(turning_choices[static_cast<std::size_t>(x)]);c.turning=s.turning();}
    return c;
}
inline VrSettings::Changes action(VrSettings& s,int row) {
    VrSettings::Changes c{};
    if(row==calibrate) {s.open_calibration_from_native();c.calibrate=c.opened=true;}
    else if(row==recenter) c.recenter=true;
    return c;
}
inline void merge(VrSettings::Changes& into,const VrSettings::Changes& c) {
    if(c.resolution) into.resolution=c.resolution;
    if(c.antialiasing) into.antialiasing=c.antialiasing;
    if(c.temporal_aa) into.temporal_aa=c.temporal_aa;
    if(c.turning) into.turning=c.turning;
    if(c.mirror) into.mirror=c.mirror;
    if(c.sharpness) into.sharpness=c.sharpness;
    if(c.hud_percent) into.hud_percent=c.hud_percent;
    if(c.controls_hint) into.controls_hint=c.controls_hint;
    if(c.crosshair) into.crosshair=c.crosshair;
    if(c.scripted_recenter) into.scripted_recenter=c.scripted_recenter;
    if(c.cutscene_mode) into.cutscene_mode=c.cutscene_mode;
    into.recenter|=c.recenter;into.consume|=c.consume;into.opened|=c.opened;into.calibrate|=c.calibrate;
    into.player_measurements|=c.player_measurements;into.test_unlock|=c.test_unlock;
}
} // namespace native_settings
} // namespace d2vr
