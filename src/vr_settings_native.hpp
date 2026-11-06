#pragma once
// VR settings as rows of the game's own options page (native_options_page.hpp) and back: the same values, choices and
// consequences as the drawn panel (settings_panel_image.hpp), with the game's widgets.
#include "vr_settings.hpp"
#include "native_options_page.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace d2vr {
namespace native_settings {
// At most NativeOptionsPage::max_rows rows: the game's list cannot select more (test unlock lives on the launcher Tools page).
// v1046: row 2 is ALWAYS the "Upscaler scale" slider (same label/range/interval), locked and showing 100 unless the AA mode is an
// upscaler (FSR 3/4, DLAA, DLSS Quality): no AA change alters the page's shape, so nothing re-shows. SMAA stays reachable as the
// Anti-aliasing list entries (SMAA, SSAA); the old Edge AA toggle is gone from the native page.
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
// The anti-aliasing stepper's choices, in display order. 9 is SMAA: a UI-only entry that applies as temporal 0 + Edge AA
// on, so SMAA is its own listed mode in the game menu (not just a toggle next to "Off").
inline std::vector<int> temporal_values(const VrSettings& s) {
    std::vector<int> v{0,1,2,3,4,5,6};
    if(s.dlss_available()) {v.push_back(7);v.push_back(8);}
    else if(s.temporal_aa()>=7) v.push_back(s.temporal_aa()); // an unoffered saved DLSS mode, shown truthfully
    v.push_back(9);
    v.push_back(10); // SSAA 125%: the deferred-renderer equivalent of MSAA clarity
    v.push_back(11); // SSAA 150%: more samples for thin distant geometry
    return v;
}
// The stepper index shown for the saved anti-aliasing mode: temporal 0 with Edge AA on is SMAA; without Edge AA,
// 125%/150% resolution is the SSAA preset.
inline int temporal_shown(const VrSettings& s) {
    const auto values=temporal_values(s);
    const int want=s.temporal_aa()!=0?s.temporal_aa():
        (std::abs(s.requested()-2.0)<.001?11:(s.antialiasing()?9:(std::abs(s.requested()-1.25)<.001?10:0)));
    const auto found=std::find(values.begin(),values.end(),want);
    return found==values.end()?0:static_cast<int>(found-values.begin());
}
// The AA stepper's entry for an upscaler mode at a scale the player set (instead of the preset's name).
inline std::string custom_label(const VrSettings& s) {
    const int mode=s.temporal_aa();
    std::string t=std::string(mode<=4?"FSR 3":mode<=6?"FSR 4":"DLSS")+" Custom ("+std::to_string(s.upscale_percent())+"%)";
    if(mode>=7 && !s.dlss_available()) t+=" (unavailable, FSR 3)";
    return t;
}
inline std::vector<NativeOptionsPage::Row> rows(const VrSettings& s) {
    using K=NativeOptionsPage::Kind;std::vector<NativeOptionsPage::Row> r(count);
    r[resolution]={K::slider,"Resolution","Sets how many pixels each eye renders (percent). Higher is sharper but slower; the mod caps it to what the GPU can hold.",
        50,200,5,static_cast<int>(std::lround(s.requested()*100)),100,{}};
    // DLAA/DLSS Quality are offered only with an NVIDIA GPU and the user's nvngx_dlss.dll; a saved DLSS mode shows as the
    // last offered row elsewhere (the mod renders it with FSR 3 then).
    std::vector<std::string> aa_modes;
    for(const int v:temporal_values(s)) switch(v) {
        case 0: aa_modes.push_back("Off");break;
        case 1: aa_modes.push_back("Game TAA");break;
        case 2: aa_modes.push_back("FSR 3 Native");break;
        case 3: aa_modes.push_back("FSR 3 Quality (77%)");break;
        case 4: aa_modes.push_back("FSR 3 Balanced (67%)");break;
        case 5: aa_modes.push_back("FSR 4 Native");break;
        case 6: aa_modes.push_back("FSR 4 Quality (77%)");break;
        case 7: aa_modes.push_back(s.dlss_available()?"DLAA (NVIDIA DLSS)":"DLAA (unavailable, FSR 3)");break;
        case 8: aa_modes.push_back(s.dlss_available()?"DLSS Quality (NVIDIA)":"DLSS Quality (unavailable, FSR 3)");break;
        case 10: aa_modes.push_back("SSAA 125% (MSAA-equivalent)");break;
        case 11: aa_modes.push_back("SSAA 200% + SMAA (max clarity)");break;
        default: aa_modes.push_back("SMAA (edge anti-aliasing)");break;
    }
    if(s.upscaler() && s.upscale_custom_percent()) {const auto at=static_cast<std::size_t>(temporal_shown(s));if(at<aa_modes.size()) aa_modes[at]=custom_label(s);}
    std::string aa_help="Smooths shimmering edges. Game TAA is the original; FSR Native uses full resolution; Quality and Balanced upscale. SMAA runs on its own with no temporal AA. Upscaler scale (next row) applies only to the FSR and DLSS entries and picking one resets it to that preset.";
    if(s.performance_mode_replaces_aa()) aa_help+=" The selected temporal mode is unavailable; spatial edge AA is used when enabled.";
    if(s.dlss_available()) aa_help+=" DLAA and DLSS Quality use NVIDIA DLSS.";
    r[temporal]={K::stepper,"Anti-aliasing",aa_help,0,0,1,temporal_shown(s),2,aa_modes};
    r[edge_aa]={K::slider,"Upscaler scale","What the upscaler renders before it reconstructs the full-size image, as a percent of the Resolution above: 100% is native, 77% is Quality, 67% is Balanced. Lower runs faster but softer. Applies only to the FSR and DLSS modes of Anti-aliasing (greyed out otherwise). Moving it makes the Anti-aliasing entry Custom; picking a preset entry there resets it. DLSS accepts only the range NVIDIA supports.",
        dlss_mode::min_percent,dlss_mode::max_percent,1,s.upscaler()?s.upscale_percent():100,100,{},!s.upscaler()};
    r[mirror]={K::stepper,"Monitor view","Chooses what the desktop monitor mirrors. It does not change the headset image.",
        0,0,1,static_cast<int>(s.mirror()),static_cast<int>(MirrorMode::both_eyes),{"Right eye","Left eye","Both eyes","Off"}};
    r[sharpening]={K::stepper,"Sharpening","Contrast sharpening after anti-aliasing. Raise it if the image looks soft, lower it if edges crawl.",
        0,0,1,static_cast<int>(std::lround(s.sharpness()*4)),2,{"Off","Low","Medium","High","Max"}};
    r[hud]={K::stepper,"HUD size","Scales the HUD and prompts in VR. 55% sits in the centre of your view; 100% keeps the game layout; larger sizes enlarge prompts and markers.",
        0,0,1,hud_index(s.hud_percent()),1,{"55% (centre)","65%","75%","85%","100% (game layout)","125%","150%","200%"}};
    // Keep the old row index and saved controls_hint preference compatible; opening the guide is an action.
    r[controls_hint]={K::action,"Controls & gestures","Controller pictures and motion hints. Press right to open; use the right stick to change pages. Squeeze the right grip at your right temple to close, or to open during play.",0,0,1,0,0,{"Select \xE2\x96\xBA","Shown"}};
    r[turning]={K::stepper,"Turning","Snap rotates in fixed steps and is comfortable; Smooth turns continuously; Native game keeps the original stick turn.",
        0,0,1,turning_index(s.turning()),1,{"Snap 30\xC2\xB0","Snap 45\xC2\xB0","Snap 90\xC2\xB0","Smooth 45\xC2\xB0/s","Smooth 90\xC2\xB0/s","Smooth 120\xC2\xB0/s","Native game"}};
    // Actions: press right to run ("Select" -> "Starting..."); reopening the page shows "Select" again.
    r[calibrate]={K::action,"Calibrate avatar","Measure your shoulders, then your arm reach, so the body matches yours. Press right, then follow the avatar's pose.",0,0,1,0,0,{"Select \xE2\x96\xBA","Starting\xE2\x80\xA6"}};
    r[recenter]={K::action,"Recenter view","Re-aligns the view to where you face now. Press right.",0,0,1,0,0,{"Select \xE2\x96\xBA","Recentred"}};
    return r;
}
// Values read from the page (index = row, -1 = no value): apply what changed, reported like the panel's own changes.
// v1046: row 2 is always the Upscaler scale slider (the flag stays only for callers); a value read while locked is ignored.
inline VrSettings::Changes apply(VrSettings& s,const std::vector<int>& v,bool=true) {
    VrSettings::Changes c{};
    const auto at=[&](int row) {return row<static_cast<int>(v.size())?v[static_cast<std::size_t>(row)]:-1;};
    if(const int x=at(resolution);x>=50 && x<=200 && std::lround(s.requested()*100)!=x) {s.set_requested(x/100.);c.resolution=s.requested();}
    // The anti-aliasing stepper is index-based: map the chosen index back to its mode. 9 is SMAA (temporal 0 + Edge AA on).
    bool aa_stepper_forced=false;
    {const auto values=temporal_values(s);
     if(const int x=at(temporal);x>=0 && x<static_cast<int>(values.size()) && x!=temporal_shown(s)) {
         const int mode=values[static_cast<std::size_t>(x)];
         if(mode==0) {s.set_temporal_aa(0);s.set_antialiasing(false);if(std::abs(s.requested()-1.25)<.001||std::abs(s.requested()-2.0)<.001){s.set_requested(1.0);c.resolution=1.0;}c.temporal_aa=0;c.antialiasing=false;aa_stepper_forced=true;}
         else if(mode==9) {s.set_temporal_aa(0);s.set_antialiasing(true);c.temporal_aa=0;c.antialiasing=true;aa_stepper_forced=true;}
         else if(mode==10) {s.set_temporal_aa(0);s.set_antialiasing(false);s.set_requested(1.25);c.temporal_aa=0;c.antialiasing=false;c.resolution=1.25;aa_stepper_forced=true;}
         else if(mode==11) {s.set_temporal_aa(0);s.set_antialiasing(true);s.set_requested(2.0);c.temporal_aa=0;c.antialiasing=true;c.resolution=2.0;aa_stepper_forced=true;}
         else {s.set_temporal_aa(mode);c.temporal_aa=mode;aa_stepper_forced=true;} // an upscaler entry: its preset scale, not the page's row-2 value
     }}
    // Off/SMAA in the mode list already decided Edge AA; the stale toggle value read from the same page must not undo it.
    // Picking an upscaler entry also decided the scale (its preset); a stale row-2 value read in the same pass must not undo it.
    if(!aa_stepper_forced) {
        const int x=at(edge_aa); // the slider belongs to the upscaler; a mode that is not one has it locked and ignores the value
        if(x>=dlss_mode::min_percent && x<=dlss_mode::max_percent && s.upscaler() && x!=s.upscale_percent() && s.set_upscale_percent(x)) c.upscale_percent=s.upscale_percent();
    }
    if(const int x=at(mirror);x>=0 && x<=3 && x!=static_cast<int>(s.mirror())) {s.set_mirror(static_cast<MirrorMode>(x));c.mirror=s.mirror();}
    if(const int x=at(sharpening);x>=0 && x<=4 && x!=std::lround(s.sharpness()*4)) {s.set_sharpness(x/4.);c.sharpness=s.sharpness();}
    if(const int x=at(hud);x>=0 && x<static_cast<int>(hud_choices.size()) && hud_choices[static_cast<std::size_t>(x)]!=s.hud_percent())
        {s.set_hud_percent(hud_choices[static_cast<std::size_t>(x)]);c.hud_percent=s.hud_percent();}
    if(const int x=at(turning);x>=0 && x<static_cast<int>(turning_choices.size()) && x!=turning_index(s.turning()))
        {s.set_turning(turning_choices[static_cast<std::size_t>(x)]);c.turning=s.turning();}
    return c;
}
inline VrSettings::Changes action(VrSettings& s,int row) {
    VrSettings::Changes c{};
    if(row==calibrate) {s.open_calibration_from_native();c.calibrate=c.opened=true;}
    else if(row==recenter) c.recenter=true;
    else if(row==controls_hint) c.controls_guide=true;
    return c;
}
inline void merge(VrSettings::Changes& into,const VrSettings::Changes& c) {
    if(c.resolution) into.resolution=c.resolution;
    if(c.antialiasing) into.antialiasing=c.antialiasing;
    if(c.temporal_aa) into.temporal_aa=c.temporal_aa;
    if(c.upscale_percent) into.upscale_percent=c.upscale_percent;
    if(c.turning) into.turning=c.turning;
    if(c.mirror) into.mirror=c.mirror;
    if(c.sharpness) into.sharpness=c.sharpness;
    if(c.hud_percent) into.hud_percent=c.hud_percent;
    if(c.controls_hint) into.controls_hint=c.controls_hint;
    if(c.crosshair) into.crosshair=c.crosshair;
    if(c.scripted_recenter) into.scripted_recenter=c.scripted_recenter;
    if(c.cutscene_mode) into.cutscene_mode=c.cutscene_mode;
    into.recenter|=c.recenter;into.consume|=c.consume;into.opened|=c.opened;into.calibrate|=c.calibrate;
    into.player_measurements|=c.player_measurements;into.test_unlock|=c.test_unlock;into.controls_guide|=c.controls_guide;
}
// What the open page becomes once the values read from it are applied: a private copy of the settings takes the page's values
// through apply() and rows() is read again (native_options_page.hpp refresh/restore). The copy keeps evolving across reads until the
// next publish replaces it. Drain thread only.
inline NativeOptionsPage::Resolver resolver(const VrSettings& s) {
    auto state=std::make_shared<VrSettings>(s);
    return [state](const std::vector<int>& values,const std::vector<NativeOptionsPage::Kind>& kinds) {
        (void)kinds;apply(*state,values);
        return rows(*state);
    };
}
} // namespace native_settings
} // namespace d2vr
