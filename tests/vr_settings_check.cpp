#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/vr_settings.hpp"
#include "../src/settings_panel_image.hpp"
#include "../src/vr_preferences.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;
        const auto rowv=[](int row) {return (settings_row_top+settings_row_pitch*row+13)/560.;}; // middle of settings row `row`
        {
            VrSettings backend;
            backend.initialize(1,true,{},MirrorMode::both_eyes,.5,6);
            backend.upscaler_generation(4);
            require(std::wstring(backend.temporal_aa_text())==L"Anti-aliasing: FSR 4 Quality (77%)","FSR4 backend label wrong");
            require(backend.temporal_aa()==6,"Runtime backend changed saved quality mode");
            backend.upscaler_generation(3);
            require(std::wstring(backend.temporal_aa_text())==L"Anti-aliasing: FSR 4 Quality (unavailable, FSR 3)","FSR4 fallback label wrong");
            VrSettings fsr3;fsr3.initialize(1,true,{},MirrorMode::both_eyes,.5,4);fsr3.upscaler_generation(3);
            require(std::wstring(fsr3.temporal_aa_text())==L"Anti-aliasing: FSR 3 Balanced (67%)","FSR3 label wrong");
            // DLSS modes (7 DLAA, 8 DLSS Quality) report the NGX backend (generation 5) or the FSR 3 fallback.
            VrSettings dlss;dlss.initialize(1,true,{},MirrorMode::both_eyes,.5,7);
            require(std::wstring(dlss.temporal_aa_text())==L"Anti-aliasing: DLAA (unavailable, FSR 3)","DLAA fallback label wrong");
            dlss.upscaler_generation(5);require(std::wstring(dlss.temporal_aa_text())==L"Anti-aliasing: DLAA","DLAA label wrong");
            dlss.set_temporal_aa(8);require(std::wstring(dlss.temporal_aa_text())==L"Anti-aliasing: DLSS Quality","DLSS Quality label wrong");
            dlss.upscaler_generation(3);require(std::wstring(dlss.temporal_aa_text())==L"Anti-aliasing: DLSS Quality (unavailable, FSR 3)","DLSS Quality fallback label wrong");
            require(dlss.temporal_aa()==8,"Runtime backend changed the saved DLSS mode");
            // The rows cycle through 7/8 only when DLSS is offered; a saved DLSS mode steps off like the last FSR 4 row otherwise.
            require(!dlss.dlss_available() && dlss.temporal_aa_choices()==7,"DLSS offered by default");
            dlss.set_temporal_aa(6);dlss.cycle_temporal_aa(1);require(dlss.temporal_aa()==0,"Unavailable DLSS was offered after FSR 4 Quality");
            dlss.set_temporal_aa(8);dlss.cycle_temporal_aa(1);require(dlss.temporal_aa()==0,"Saved DLSS mode did not wrap to Off without DLSS");
            dlss.set_temporal_aa(7);dlss.cycle_temporal_aa(-1);require(dlss.temporal_aa()==5,"Saved DLSS mode did not step back to FSR 4 Native without DLSS");
            dlss.dlss_available(true);require(dlss.temporal_aa_choices()==9,"DLSS rows not offered when available");
            dlss.set_temporal_aa(6);dlss.cycle_temporal_aa(1);require(dlss.temporal_aa()==7,"DLAA not offered after FSR 4 Quality");
            dlss.cycle_temporal_aa(1);require(dlss.temporal_aa()==8,"DLSS Quality not offered after DLAA");
            dlss.cycle_temporal_aa(1);require(dlss.temporal_aa()==0,"DLSS Quality did not wrap to Off");
            dlss.cycle_temporal_aa(-1);require(dlss.temporal_aa()==8,"Stepping back from Off did not reach DLSS Quality");
            dlss.set_temporal_aa(9);require(dlss.temporal_aa()==8,"set_temporal_aa accepted 9");
            {   // The panel row itself: A on row 1 cycles through the offered modes.
                ControllerInput idle{};idle.active=true;dlss.open_from_menu();dlss.update(idle,false,true);
                ControllerInput down=idle;down.move_y=-1;dlss.update(down,false,true);dlss.update(idle,false,true);
                require(dlss.selected()==1,"Row 1 not selected");
                dlss.set_temporal_aa(8);ControllerInput a=idle;a.a=true;
                const auto change=dlss.update(a,false,true);
                require(change.temporal_aa && *change.temporal_aa==0 && dlss.temporal_aa()==0,"A on the row did not wrap DLSS Quality to Off");
                require(dlss.aa_chosen(),"panel_aa_change_marks_chosen: A on row 1 did not mark the AA choice");
                VrSettings fresh;require(!fresh.aa_chosen() && fresh.crosshair(),"Fresh VrSettings: aa_chosen/crosshair defaults differ");
                fresh.initialize(1,true,{},MirrorMode::right_eye,.5,2,65,true,{},true,false);require(fresh.aa_chosen() && !fresh.crosshair(),"initialize did not take aa_chosen/crosshair");
                fresh.set_crosshair(true);require(fresh.crosshair(),"set_crosshair did not apply");
            }
        }
        {   // open_from_gameplay_opens_page_zero / closes_on_y_press_after_open_from_gameplay
            VrSettings held;ControllerInput idle{};idle.active=true;
            held.update(idle,true,true);
            require(!held.open(),"Gameplay opened settings without a hold");
            held.open_from_gameplay();
            require(held.open() && !held.calibration_page(),"open_from_gameplay did not open page 0");
            require(held.update(idle,true,true,{},false).consume && held.open(),"Gameplay settings closed or leaked input on the first frame");
            require(held.update(idle,true,true,{},false).consume && held.open(),"Gameplay settings did not keep running without settings_available");
            ControllerInput down=idle;down.move_y=-1;
            held.update(down,true,true,{},false);held.update(idle,true,true,{},false);
            require(held.open(),"Gameplay settings closed on a stick move");
            ControllerInput press=idle;press.y=true;
            const auto closed=held.update(press,true,true,{},false);
            require(!held.open() && closed.consume,"Y press did not close gameplay settings");
            require(held.update(press,true,true,{},false).consume,"Y held through the close reached the game");
            require(!held.update(idle,true,true,{},false).consume,"Closed gameplay settings kept consuming input after release");
            held.open_from_gameplay();held.update(idle,true,true,{},false);
            ControllerInput back=idle;back.b=true;
            require(held.update(back,true,true,{},false).consume && !held.open(),"B did not close gameplay settings");
            held.open_from_gameplay();held.update(idle,true,true,{},false);
            require(!held.update(idle,true,false,{},false).consume && !held.open(),"Focus loss kept gameplay settings open");
            {   // the Y that opened it is still down: it must not close the panel; a later fresh press does
                VrSettings opened;ControllerInput hold=idle;hold.y=true;
                opened.open_from_gameplay();
                for(int frame=0;frame<6;++frame) require(opened.update(hold,true,true,{},false).consume && opened.open(),"Opening Y still held closed the gameplay panel");
                opened.update(idle,true,true,{},false);
                require(opened.open(),"Releasing the opening Y closed the panel");
                require(opened.update(hold,true,true,{},false).consume && !opened.open(),"A fresh Y press after release did not close the panel");
            }
            {   // close_leak: a button held through the close stays consumed until released, then input passes again
                VrSettings leak;ControllerInput shut=idle;shut.b=true;
                leak.open_from_gameplay();leak.update(idle,true,true,{},false);
                require(leak.update(shut,true,true,{},false).consume && !leak.open(),"B did not close");
                for(int frame=0;frame<4;++frame) require(leak.update(shut,true,true,{},false).consume,"B held through the close reached the game");
                require(!leak.update(idle,true,true,{},false).consume,"Input stayed consumed after B was released");
                require(!leak.update(shut,true,true,{},false).consume && !leak.open(),"A later B press was swallowed or reopened");
                VrSettings trigger;ControllerInput pull=idle;pull.right_trigger=1;ControllerInput yclose=idle;yclose.y=true;
                trigger.open_from_gameplay();trigger.update(idle,true,true,{},false);trigger.update(yclose,true,true,{},false); // Y yclose closes
                pull.y=true;
                require(trigger.update(pull,true,true,{},false).consume,"Y and trigger held through the close reached the game");
                ControllerInput only_trigger=idle;only_trigger.right_trigger=1;
                require(trigger.update(only_trigger,true,true,{},false).consume,"Trigger held through the close reached the game");
                require(!trigger.update(idle,true,true,{},false).consume,"Trigger latch never released");
            }
            {   // latch details: timeout, reopen, only what was down, sticks, menu
                const auto close_with_b=[&](VrSettings& panel,ControllerInput held) {
                    panel.open_from_gameplay();panel.update(idle,true,true,{},false);
                    ControllerInput b=held;b.b=true;require(panel.update(b,true,true,{},false).consume && !panel.open(),"B did not close");
                    return held;
                };
                VrSettings stuck;ControllerInput drift=idle;drift.right_trigger=1;
                close_with_b(stuck,drift);drift.b=true;
                int frames=0;while(stuck.update(drift,true,true,{},false,1./90).consume && frames<1000) ++frames;
                require(frames>=1.5*90-3 && frames<=1.5*90+3,"A stuck trigger did not release the latch at 1.5 s");
                VrSettings again;ControllerInput yheld=idle;yheld.y=true;
                again.open_from_gameplay();again.update(idle,true,true,{},false);again.update(yheld,true,true,{},false); // Y press closes
                require(again.update(yheld,true,true,{},false).consume,"Y latch missing");
                again.open_from_gameplay();
                require(again.update(yheld,true,true,{},false).consume && again.open(),"Reopen while latched closed at once");
                again.update(idle,true,true,{},false);
                require(again.update(yheld,true,true,{},false).consume && !again.open(),"A reopened panel could not be closed while the old latch was live");
                VrSettings grip;ControllerInput squeeze=idle;squeeze.left_grip=.9f;
                close_with_b(grip,squeeze);
                require(grip.update(squeeze,true,true,{},false).consume,"Grip held through the close reached the game");
                ControllerInput a_and_grip=squeeze;a_and_grip.a=true;
                require(grip.update(a_and_grip,true,true,{},false).consume,"Latched grip released early");
                ControllerInput a_only=idle;a_only.a=true;
                require(!grip.update(a_only,true,true,{},false).consume,"A pressed during the latch extended it");
                VrSettings stick;ControllerInput walk=idle;walk.move_x=1;
                close_with_b(stick,walk);walk.b=true;
                require(stick.update(walk,true,true,{},false).consume,"Stick held through the close reached the game");
                walk.move_x=.2f;walk.b=false;
                require(!stick.update(walk,true,true,{},false).consume,"Re-centred stick stayed latched");
                VrSettings menu;ControllerInput pause=idle;pause.menu=true;
                close_with_b(menu,pause);
                require(menu.update(pause,true,true,{},false).consume,"Menu held through the close reached the game");
                require(!menu.update(idle,true,true,{},false).consume,"Menu latch never released");
            }
            VrSettings plain;plain.open_from_menu();
            plain.update(idle,true,true);
            require(!plain.open(),"A menu-opened panel survived gameplay without the hold flag");
            VrSettings page;page.open_from_gameplay();page.update(idle,true,true,{},false);
            for(int i=0;i<2;++i) {ControllerInput step=idle;step.move_y=1;page.update(step,true,true,{},false);page.update(idle,true,true,{},false);}
            ControllerInput accept=idle;accept.a=true;
            require(!page.update(accept,true,true,{},false).calibrate && !page.calibration_page(),"Gameplay settings started a calibration");
        }
        {
            VrSettings setup;ControllerInput neutral{};neutral.active=true;
            setup.calibration_runtime(AvatarFitRuntimeStatus::recalibration_required);
            require(!setup.update(neutral,true,true).consume,"Calibration interrupted gameplay");
            require(!setup.update(neutral,false,true,{},false).consume,"Calibration interrupted a non-pause page");
            setup.update(neutral,false,true);
            require(setup.update(neutral,false,true).opened && setup.calibration_page(),"First pause did not show required calibration");
            neutral.a=true;require(setup.update(neutral,false,true).calibrate,"Setup A did not start capture");
            neutral.a=false;setup.update(neutral,false,true);
            neutral.b=true;setup.update(neutral,false,true);require(setup.open() && !setup.calibration_page(),"Setup B did not leave capture page");
            neutral.b=false;setup.update(neutral,false,true);neutral.b=true;setup.update(neutral,false,true);neutral.b=false;
            require(!setup.open() && !setup.update(neutral,false,true).opened,"Cancelled setup reopened every frame");
            setup.suspend();setup.update(neutral,false,true);
            require(!setup.update(neutral,false,true).opened,"Same required fit reopened after focus suspension");
            setup.calibration_runtime(AvatarFitRuntimeStatus::active);
            setup.calibration_runtime(AvatarFitRuntimeStatus::recalibration_required);
            require(setup.update(neutral,false,true).opened,"Invalidated accepted fit did not prompt again");
            setup.calibration_status(AvatarCalibrationState::rig_changed);
            setup.calibration_identity_changed();setup.update(neutral,false,true);
            require(setup.update(neutral,false,true).opened,"Native identity change did not rearm required prompt");
            setup.calibration_identity_changed();setup.calibration_runtime(AvatarFitRuntimeStatus::active);
            setup.update(neutral,false,true);
            require(!setup.update(neutral,false,true).opened && !setup.calibration_required(),"Accepted compatible reuse still prompted");
        }
        const TrackedPose plane{{0,0,-1.6},{0,0,0,1}},aim{{0,0,0},{0,0,0,1}};
        const auto centre=panel_pointer(aim,plane,1.28,1.12);
        require(centre && std::abs(centre->u-.5)<1e-6 && std::abs(centre->v-.5)<1e-6,"Aim ray did not hit panel centre");
        require(!panel_pointer({{0,0,-2},{0,0,0,1}},plane,1.28,1.12),"Backside ray was accepted");
        require(!panel_pointer({{3,0,0},{0,0,0,1}},plane,1.28,1.12),"Outside pointer was accepted");
        const Quaternion yaw{0,std::sin(.4),0,std::cos(.4)};
        const auto rotated_hit=panel_pointer({{1,2,3},yaw},{Vec3{1,2,3}+rotated(yaw,{0,0,-1.6}),yaw},1.28,1.12);
        require(rotated_hit && std::abs(rotated_hit->u-.5)<1e-6,"World-oriented ray/panel transform differs");
        VrSettings menu;menu.initialize(1.2,true,{TurnControl::Mode::snap,45,90});ControllerInput in{};in.active=true;
        menu.update(in,false,true);in.y=true;
        require(menu.update(in,false,true).opened && menu.open(),"Y did not open settings while paused");
        require(!menu.update(in,false,true).opened,"Held Y reopened settings");in.y=false;menu.update(in,false,true);
        in.move_x=1;auto increase=menu.update(in,false,true);
        require(increase.consume && increase.resolution && std::abs(*increase.resolution-1.3)<1e-6,"Right stick navigation did not increase scale once");
        require(!menu.update(in,false,true).resolution,"Held stick repeatedly increased scale");
        in.move_x=0;menu.update(in,false,true);in.right_trigger=1;
        const auto decrease=menu.update(in,false,true,PanelPointer{.15,rowv(0)});
        require(decrease.resolution && std::abs(*decrease.resolution-1.2)<1e-6,"Pointer minus click failed");
        require(!menu.update(in,false,true,PanelPointer{.15,rowv(0)}).resolution,"Held trigger repeated click");
        {   // Back row: left half requests the test unlock and keeps the menu open; right half closes.
            VrSettings unlock;ControllerInput c{};c.active=true;unlock.update(c,false,true);c.y=true;unlock.update(c,false,true);c.y=false;unlock.update(c,false,true);
            c.right_trigger=1;const auto request=unlock.update(c,false,true,PanelPointer{.25,rowv(12)});
            require(request.test_unlock && unlock.open(),"Test unlock click did not request the unlock");
            c.right_trigger=0;unlock.update(c,false,true);c.right_trigger=1;
            const auto close=unlock.update(c,false,true,PanelPointer{.75,rowv(12)});
            require(!close.test_unlock && !unlock.open(),"Back half did not close settings");
        }
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto taa_toggle=menu.update(in,false,true,PanelPointer{.5,rowv(1)});
        require(taa_toggle.temporal_aa && *taa_toggle.temporal_aa && menu.temporal_aa(),"Pointer did not toggle temporal AA");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(1)}).temporal_aa,"Held trigger changed temporal AA repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto toggle=menu.update(in,false,true,PanelPointer{.5,rowv(2)});
        require(toggle.antialiasing && !*toggle.antialiasing,"Pointer did not toggle AA");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto mirror_click=menu.update(in,false,true,PanelPointer{.5,rowv(3)});
        require(mirror_click.mirror && *mirror_click.mirror==MirrorMode::left_eye && menu.mirror()==MirrorMode::left_eye,"Pointer did not cycle monitor view");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(3)}).mirror,"Held trigger changed monitor view repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto sharp_click=menu.update(in,false,true,PanelPointer{.5,rowv(4)});
        require(sharp_click.sharpness && *sharp_click.sharpness==.75 && menu.sharpness()==.75,"Pointer did not cycle sharpening");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(4)}).sharpness,"Held trigger changed sharpening repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto reticle_click=menu.update(in,false,true,PanelPointer{.5,rowv(5)});
        require(reticle_click.hud_percent && *reticle_click.hud_percent==75 && menu.hud_percent()==75,"Pointer did not cycle the HUD size");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(5)}).hud_percent,"Held trigger changed the HUD size repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto hint_click=menu.update(in,false,true,PanelPointer{.5,rowv(6)});
        require(hint_click.controls_guide && hint_click.consume && !hint_click.controls_hint && menu.controls_hint(),"Pointer must consume input and open the guide without changing the saved hint preference");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(6)}).controls_guide,"Held trigger opened the guide repeatedly");
        {
            VrSettings gameplay_guide;ControllerInput c{};c.active=true;gameplay_guide.set_controls_hint(false);
            gameplay_guide.open_from_gameplay();gameplay_guide.update(c,true,true);
            c.right_trigger=1;const auto opened=gameplay_guide.update(c,true,true,PanelPointer{.5,rowv(6)});
            require(opened.controls_guide && opened.consume && !opened.controls_hint && !gameplay_guide.controls_hint(),"Gameplay panel guide action must preserve a disabled saved hint preference");
            require(!gameplay_guide.update(c,true,true,PanelPointer{.5,rowv(6)}).controls_guide,"Gameplay panel held trigger opened the guide repeatedly");
        }
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        {const bool before=menu.crosshair();
         const auto cross=menu.update(in,false,true,PanelPointer{.5,rowv(7)});
         require(cross.crosshair && *cross.crosshair==!before && menu.crosshair()==!before,"Pointer did not toggle the crosshair");
         require(!menu.update(in,false,true,PanelPointer{.5,rowv(7)}).crosshair,"Held trigger toggled the crosshair repeatedly");
         in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;}
        {   // the two rows added in v610: scripted-recenter toggle and cutscene mode cycle, each by pointer; the shifted calibrate/close rows are covered below
            const bool before=menu.scripted_recenter();
            const auto sr=menu.update(in,false,true,PanelPointer{.5,rowv(9)});
            require(sr.scripted_recenter && *sr.scripted_recenter==!before && menu.scripted_recenter()==!before,"Pointer did not toggle recenter on scripted moments");
            require(!menu.update(in,false,true,PanelPointer{.5,rowv(9)}).scripted_recenter,"Held trigger toggled recenter repeatedly");
            in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
            const auto cm=menu.update(in,false,true,PanelPointer{.5,rowv(10)});
            require(cm.cutscene_mode && *cm.cutscene_mode==CutsceneMode::vr_full && menu.cutscene_mode()==CutsceneMode::vr_full,"Pointer did not cycle the cutscene mode");
            require(!menu.update(in,false,true,PanelPointer{.5,rowv(10)}).cutscene_mode,"Held trigger cycled the cutscene mode repeatedly");
            in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
            menu.set_cutscene_mode(CutsceneMode::vr_comfort);menu.set_scripted_recenter(true);
            VrSettings d;require(d.scripted_recenter() && d.cutscene_mode()==CutsceneMode::vr_comfort,"New settings defaults differ");
            require(std::wstring(d.scripted_recenter_text())==L"Recenter on scripted moments: On" && std::wstring(d.cutscene_mode_text())==L"Cutscenes: VR comfort","Default row texts differ");
            d.set_scripted_recenter(false);d.set_cutscene_mode(CutsceneMode::vr_full);
            require(std::wstring(d.scripted_recenter_text())==L"Recenter on scripted moments: Off" && std::wstring(d.cutscene_mode_text())==L"Cutscenes: VR full","Row texts differ");
            d.set_cutscene_mode(CutsceneMode::theater);require(std::wstring(d.cutscene_mode_text())==L"Cutscenes: Theater","Theater text differs");
            VrSettings init;init.initialize(1,true,{},MirrorMode::right_eye,.5,2,65,true,{},true,true,false,CutsceneMode::theater);
            require(!init.scripted_recenter() && init.cutscene_mode()==CutsceneMode::theater,"initialize did not take the new settings");
            in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        }
        const auto turn=menu.update(in,false,true,PanelPointer{.5,rowv(8)});
        require(turn.turning && turn.turning->mode==TurnControl::Mode::snap && turn.turning->snap_degrees==90,"Turning preset did not advance");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(8)}).turning,"Held trigger changed turning repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        require(menu.update(in,false,true,PanelPointer{.5,rowv(11)}).recenter,"Recenter action did not fire");
        require(!menu.update(in,false,true,PanelPointer{.5,rowv(11)}).recenter,"Held trigger repeatedly recentered");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        require(menu.update(in,false,true,PanelPointer{.25,rowv(11)}).calibrate && menu.calibration_page(),"Avatar calibration page did not open");
        require(!menu.update(in,false,true,PanelPointer{.25,rowv(11)}).calibrate,"Held trigger restarted calibration");
        menu.calibration_status(AvatarCalibrationState::hold_pose);in.a=true;
        require(!menu.update(in,false,true).calibrate,"Stable pose capture restarted from A");in.a=false;menu.update(in,false,true);
        menu.calibration_status(AvatarCalibrationState::hold_pose,.5);
        const auto half_hold=settings_panel_image(menu,1.2,3722,3850,{});
        const auto pixel=[&](const std::vector<unsigned char>& image,unsigned x,unsigned y,unsigned channel) {return image[(y*640+x)*4+channel];};
        require(pixel(half_hold,100,350,0)==93 && pixel(half_hold,100,350,1)==211 && pixel(half_hold,400,350,0)==38,"Stable progress line did not render at measured half hold");
        std::ofstream hold_preview("artifacts/calibration-hold-progress.rgba",std::ios::binary);
        hold_preview.write(reinterpret_cast<const char*>(half_hold.data()),static_cast<std::streamsize>(half_hold.size()));
        require(static_cast<bool>(hold_preview),"Cannot save hold progress preview");
        menu.calibration_status(AvatarCalibrationState::grips_released);
        const auto released_hold=settings_panel_image(menu,1.2,3722,3850,{});
        require(menu.calibration_progress()==0 && pixel(released_hold,100,350,0)==38,"Released grip retained visible progress");
        menu.calibration_environment(AvatarCalibrationStage::t_pose,false);
        menu.calibration_status(AvatarCalibrationState::release_grips);
        require(std::wstring(menu.calibration_status_text()).find(L"release grips")!=std::wstring::npos,"Shoulder transition did not instruct release");
        const auto release_required=settings_panel_image(menu,1.2,3722,3850,{});
        menu.calibration_status(AvatarCalibrationState::grips_released);
        require(std::wstring(menu.calibration_status_text()).find(L"Hold both grips")!=std::wstring::npos,"Released transition did not instruct next hold");
        const auto next_hold=settings_panel_image(menu,1.2,3722,3850,{});
        require(release_required!=next_hold,"Rendered capture status did not distinguish release from next hold");
        AvatarPlayerMeasurements entered;entered.standing_eye_height_metres=1.61;
        VrSettings provenance;provenance.initialize(1,false,{},MirrorMode::both_eyes,.5,2,65,true,entered);
        provenance.calibration_environment(AvatarCalibrationStage::t_pose,true);
        require(std::wstring(provenance.calibration_height_text()).find(L"entered measurement")!=std::wstring::npos,"Manual height incorrectly labelled runtime floor");
        provenance.initialize(1,false);provenance.calibration_environment(AvatarCalibrationStage::t_pose,true);
        require(std::wstring(provenance.calibration_height_text()).find(L"runtime floor")!=std::wstring::npos,"Runtime floor height provenance missing");
        provenance.calibration_environment(AvatarCalibrationStage::t_pose,false);
        require(std::wstring(provenance.calibration_height_text()).find(L"No runtime floor")!=std::wstring::npos,"Unavailable floor claimed measured height");
        menu.calibration_status(AvatarCalibrationState::pose_invalid);in.a=true;
        require(menu.update(in,false,true).calibrate,"Invalid-pose A did not explicitly retry");
        in.a=false;menu.update(in,false,true);
        in.b=true;menu.update(in,false,true);require(menu.open() && !menu.calibration_page(),"Calibration B did not return to settings");
        in.b=false;menu.update(in,false,true);
        in.b=true;require(menu.update(in,false,true).consume && !menu.open(),"B did not close and consume input");
        menu.suspend();in.y=true;in.b=false;in.right_trigger=0;
        menu.update(in,false,true);require(!menu.open(),"Focus return interpreted held Y as a new press");
        in.y=false;menu.update(in,false,true);in.y=true;menu.update(in,true,true);
        require(!menu.open(),"Gameplay Y opened graphics settings");
        in.y=false;menu.update(in,false,true);in.y=true;menu.update(in,false,true);
        require(menu.open(),"Released Y did not reopen");menu.update(in,false,false);
        require(!menu.open(),"Lost focus retained settings input");
        {
            VrSettings pages;ControllerInput controls{};controls.active=true;
            pages.update(controls,false,true,{},false);controls.y=true;
            require(!pages.update(controls,false,true,{},false).consume && !pages.open(),"Non-pause Y was consumed");
            require(!pages.update(controls,false,true).opened,"Held non-pause Y opened settings on pause entry");
            controls.y=false;pages.update(controls,false,true);controls.y=true;pages.update(controls,false,true);
            require(pages.open(),"Pause fresh Y did not open settings");
            require(!pages.update(controls,false,true,{},false).consume && !pages.open(),"Non-pause entry hijacked controls when closing settings");
        }
        {
            VrSettings rows;ControllerInput stick{};stick.active=true;
            rows.initialize(1.2,false,{TurnControl::Mode::snap,45,90});
            rows.update(stick,false,true);stick.y=true;
            require(rows.update(stick,false,true).opened,"Fresh Y did not open the settings rows");stick.y=false;rows.update(stick,false,true);
            const auto press=[&](float x,float y) {
                stick.move_x=x;stick.move_y=y;const auto changed=rows.update(stick,false,true);
                stick.move_x=0;stick.move_y=0;rows.update(stick,false,true);return changed;
            };
            // Rows: 0 resolution, 1 temporal AA, 2 edge AA, 3 monitor view, 4 sharpening,
            // 5 aim reticle, 6 controls hint, 7 turning, 8 calibrate/recenter, 9 back.
            press(0,-1);
            const auto taa_stick=press(1,0);
            require(taa_stick.temporal_aa && *taa_stick.temporal_aa && rows.temporal_aa(),"Stick did not toggle temporal AA");
            press(0,-1);
            const auto edge_stick=press(1,0);
            require(edge_stick.antialiasing && *edge_stick.antialiasing && rows.antialiasing(),"Stick did not toggle edge AA");
            press(0,-1);
            const auto mirror_stick=press(1,0);
            require(mirror_stick.mirror && *mirror_stick.mirror==MirrorMode::left_eye && rows.mirror()==MirrorMode::left_eye,"Stick did not cycle monitor view");
            press(0,-1);
            const auto sharp_stick=press(1,0);
            require(sharp_stick.sharpness && *sharp_stick.sharpness==.75 && rows.sharpness()==.75,"Stick did not cycle sharpening");
            press(0,-1);
            const auto reticle_stick=press(1,0);
            require(reticle_stick.hud_percent && *reticle_stick.hud_percent==75 && rows.hud_percent()==75,"Stick did not cycle the HUD size");
            press(0,-1);
            const auto hint_stick=press(1,0);
            require(hint_stick.controls_guide && hint_stick.consume && !hint_stick.controls_hint && rows.controls_hint(),"Stick must consume input and open the guide without changing the saved hint preference");
            stick.a=true;const auto guide_a=rows.update(stick,false,true);
            require(guide_a.controls_guide && !guide_a.controls_hint && rows.controls_hint(),"A must open the guide without changing the saved hint preference");
            require(!rows.update(stick,false,true).controls_guide,"Held A opened the guide repeatedly");
            stick.a=false;rows.update(stick,false,true);
            press(0,-1);
            const bool crosshair_before=rows.crosshair();
            const auto crosshair_stick=press(1,0);
            require(crosshair_stick.crosshair && *crosshair_stick.crosshair==!crosshair_before && rows.crosshair()==!crosshair_before,"Stick did not toggle the crosshair");
            stick.a=true;const auto crosshair_a=rows.update(stick,false,true);stick.a=false;rows.update(stick,false,true);
            require(crosshair_a.crosshair && *crosshair_a.crosshair==crosshair_before && rows.crosshair()==crosshair_before,"A did not toggle the crosshair");
            require(settings_row_top+settings_row_pitch*(settings_row_count-1)+settings_row_height<settings_plate_top,"Last row overlaps the plate");
            require(settings_plate_top+settings_plate_height<=settings_footer_top && settings_footer_top+settings_footer_height<=560,"Plate/footer layout leaves the bitmap or overlaps");
            {   // v613: performance mode is honest about SMAA replacing FSR/DLSS, and the help still fits the plate
                VrSettings perf;perf.initialize(1.5,true,{TurnControl::Mode::smooth,45,120},MirrorMode::both_eyes,.5,6);perf.upscaler_generation(3);
                require(!perf.performance_mode_replaces_aa(),"Performance mode flag must default off");
                perf.performance_mode(true);perf.effective_temporal_aa(0);require(perf.performance_mode_replaces_aa(),"A spatial fallback must be reported");
                int overflow=-1;const auto on=settings_panel_image(perf,1.23,3722,3850,{},&overflow);
                require(overflow==0,"The performance-mode AA help does not fit the description plate");
                perf.performance_mode(false);const auto off=settings_panel_image(perf,1.23,3722,3850,{},&overflow);
                require(on!=off,"The settings panel must differ in performance mode");
                perf.set_temporal_aa(1);perf.performance_mode(true);perf.effective_temporal_aa(1);require(!perf.performance_mode_replaces_aa(),"Active Game TAA must not report a fallback");
                for(int mode=2;mode<=8;++mode) {perf.set_temporal_aa(mode);perf.effective_temporal_aa(mode);require(!perf.performance_mode_replaces_aa(),"Active alternate upscaler must not report a fallback");}
            }
            for(const bool dlss_offered:{false,true}) {   // every row's description fits the plate's text rect (three 13 px lines), with the longest resolution note
                VrSettings fit;fit.initialize(1.5,true,{TurnControl::Mode::smooth,45,120},MirrorMode::both_eyes,.5,dlss_offered?8:6);fit.dlss_available(dlss_offered);fit.upscaler_generation(dlss_offered?5:3);
                int overflow=-1;settings_panel_image(fit,1.23,3722,3850,{},&overflow);
                require(overflow==0,"A settings row description does not fit the description plate");
            }
            press(0,-1);press(0,-1);   // turning (8), then the scripted-recenter row (9)
            require(rows.scripted_recenter(),"scripted_recenter must default on");
            const auto recenter_stick=press(1,0);
            require(recenter_stick.scripted_recenter && !*recenter_stick.scripted_recenter && !rows.scripted_recenter(),"Stick did not toggle recenter on scripted moments");
            stick.a=true;const auto recenter_a=rows.update(stick,false,true);stick.a=false;rows.update(stick,false,true);
            require(recenter_a.scripted_recenter && *recenter_a.scripted_recenter && rows.scripted_recenter(),"A did not toggle recenter on scripted moments");
            press(0,-1);   // cutscenes (10): right steps forward, left back
            require(rows.cutscene_mode()==CutsceneMode::vr_comfort,"cutscene mode must default to vr_comfort");
            const auto full_stick=press(1,0);
            require(full_stick.cutscene_mode && *full_stick.cutscene_mode==CutsceneMode::vr_full && rows.cutscene_mode()==CutsceneMode::vr_full,"Stick right did not step to VR full");
            require(press(1,0).cutscene_mode==CutsceneMode::theater && rows.cutscene_mode()==CutsceneMode::theater,"Stick right did not step to Theater");
            require(press(1,0).cutscene_mode==CutsceneMode::vr_comfort,"Cutscene mode did not wrap forward");
            require(press(-1,0).cutscene_mode==CutsceneMode::theater,"Stick left did not wrap backward");
            stick.a=true;const auto cut_a=rows.update(stick,false,true);stick.a=false;rows.update(stick,false,true);
            require(cut_a.cutscene_mode==CutsceneMode::vr_comfort && rows.cutscene_mode()==CutsceneMode::vr_comfort,"A did not cycle the cutscene mode forward");
            char dump[512]{};
            if(GetEnvironmentVariableA("D2VR_PANEL_DUMP",dump,sizeof dump)>0) {   // optional: raw RGBA (640x560) of the recenter, cutscene and close rows for visual review
                const auto save=[&](const char* name) {std::ofstream o(std::string(dump)+"/panel-"+name+".rgba",std::ios::binary);const auto img=settings_panel_image(rows,1.2,3722,3850,{});o.write(reinterpret_cast<const char*>(img.data()),static_cast<std::streamsize>(img.size()));};
                for(const bool dlss_offered:{false,true}) for(const unsigned row:{0u,1u,6u}) {   // resolution, anti-aliasing and guide rows
                    VrSettings v;ControllerInput c{};c.active=true;v.initialize(1.5,true,{TurnControl::Mode::snap,45,90},MirrorMode::both_eyes,.5,dlss_offered?8:6);
                    v.dlss_available(dlss_offered);v.upscaler_generation(dlss_offered?5:3);
                    v.update(c,false,true);c.y=true;v.update(c,false,true);c.y=false;v.update(c,false,true);
                    for(unsigned i=0;i<row;++i) {c.move_y=-1;v.update(c,false,true);c.move_y=0;v.update(c,false,true);}
                    const auto img=settings_panel_image(v,1.23,3722,3850,{});
                    std::ofstream o(std::string(dump)+(row==6?"/panel-guide-":row?"/panel-aa-":"/panel-resolution-")+(dlss_offered?"dlss":"nodlss")+".rgba",std::ios::binary);o.write(reinterpret_cast<const char*>(img.data()),static_cast<std::streamsize>(img.size()));
                }
                save("cutscene");press(0,1);save("recenter");press(0,-1);press(0,-1);press(0,-1);save("close");press(0,1);press(0,1);
            }
            const auto rows_image=settings_panel_image(rows,1.2,3722,3850,{});
            for(unsigned row=0;row<settings_row_count;++row) {   // every row bar sits inside the bitmap, above the description plate, drawn dark or as the pale highlight
                const unsigned y=settings_row_top+settings_row_pitch*row+13;
                require(y+settings_row_height/2<settings_plate_top && settings_row_top+settings_row_pitch*(settings_row_count-1)+settings_row_height<=560,"A settings row bar reaches the description plate");
                const auto shade=pixel(rows_image,24,y,0);
                require(shade<60 || shade>150,"A settings row fell outside the panel bitmap");
            }
            // Wrapping upwards from the cutscene row (10) eleven times reaches the last row (12), whose A closes.
            for(unsigned k=0;k<11;++k) press(0,1);
            stick.a=true;const auto back=rows.update(stick,false,true);stick.a=false;
            require(back.consume && !rows.open(),"Wrapped vertical navigation did not reach the back row");
        }
        {
            VrSettings measured;ControllerInput controls{};controls.active=true;
            measured.calibration_runtime(AvatarFitRuntimeStatus::recalibration_required);
            measured.update(controls,false,true);measured.update(controls,false,true);
            controls.y=true;measured.update(controls,false,true);controls.y=false;measured.update(controls,false,true);
            require(measured.measurements_page() && measured.calibration_page(),"Y did not open measured dimensions");
            const auto move=[&](float x,float y) {
                controls.move_x=x;controls.move_y=y;const auto change=measured.update(controls,false,true);
                controls.move_x=controls.move_y=0;measured.update(controls,false,true);return change;
            };
            require(move(1,0).player_measurements && measured.measurement_value(0)==1.65,"Blank height did not become explicit measured entry");
            require(move(1,0).player_measurements && measured.measurement_value(0)==1.66,"Stick did not adjust height by one centimetre");
            controls.move_x=1;measured.update(controls,false,true);const auto held=measured.measurement_value(0);
            require(!measured.update(controls,false,true).player_measurements && measured.measurement_value(0)==held,"Held stick repeatedly edited measurement");
            controls.move_x=0;measured.update(controls,false,true);
            for(unsigned row=0;row<5;++row) {
                controls.right_trigger=1;measured.update(controls,false,true,PanelPointer{.5,(110+56*row+24)/560.});
                require(measured.measurement_selected()==row,"640x560 measurement row hitbox differs");
                controls.right_trigger=0;measured.update(controls,false,true);
                require(move(1,0).player_measurements && measured.measurement_value(row),"Measurement row did not edit its own optional value");
            }
            controls.right_trigger=1;measured.update(controls,false,true,PanelPointer{.5,160/560.});
            require(measured.measurement_selected()==4,"Measurement row gap selected a different field");
            controls.right_trigger=0;measured.update(controls,false,true);
            controls.x=true;require(measured.update(controls,false,true).player_measurements && !measured.measurement_value(4),"X did not clear the selected optional measurement");
            require(!measured.update(controls,false,true).player_measurements,"Held X repeated clear");controls.x=false;measured.update(controls,false,true);
            const auto measurements=settings_panel_image(measured,1,3200,3200,{});require(measurements.size()==640*560*4,"Measurement bitmap size differs");
            std::ofstream preview("artifacts/player-measurements-preview.rgba",std::ios::binary);
            preview.write(reinterpret_cast<const char*>(measurements.data()),static_cast<std::streamsize>(measurements.size()));require(static_cast<bool>(preview),"Measurement preview write failed");
            controls.y=true;measured.update(controls,false,true);require(!measured.measurements_page() && measured.calibration_page(),"Y did not return to capture");
            controls.y=false;measured.update(controls,false,true);controls.y=true;measured.update(controls,false,true);
            controls.y=false;measured.update(controls,false,true);controls.a=true;
            require(measured.update(controls,false,true).calibrate && !measured.measurements_page(),"A did not start capture from measured fields");
            controls.a=false;measured.update(controls,false,true);
            measured.calibration_environment(AvatarCalibrationStage::shoulders,false);measured.calibration_status(AvatarCalibrationState::hold_pose,.5);
            const auto shoulders=settings_panel_image(measured,1,3200,3200,{});
            measured.calibration_environment(AvatarCalibrationStage::t_pose,false);const auto reaches=settings_panel_image(measured,1,3200,3200,{});
            require(shoulders!=reaches,"Shoulder and reach stages render the same guidance");
            for(const auto& image:{std::pair<const char*,const std::vector<unsigned char>*>{"shoulders",&shoulders},{"reach",&reaches}}) {
                std::ofstream stage(std::string("artifacts/calibration-stage-")+image.first+".rgba",std::ios::binary);
                stage.write(reinterpret_cast<const char*>(image.second->data()),static_cast<std::streamsize>(image.second->size()));require(static_cast<bool>(stage),"Stage preview write failed");
            }
            controls.b=true;measured.update(controls,false,true);require(!measured.calibration_page(),"B failed to cancel stage UI");
        }
        for(unsigned close=0;close<4;++close) {
            VrSettings wizard;ControllerInput controls{};controls.active=true;
            AvatarCalibration capture;const TrackedPose head{{0,1.7,0},{}};
            const std::array<TrackedPose,2> hands{{{{-.8,1.48,0},{}},{{.8,1.48,0},{}}}};
            wizard.update(controls,false,true);controls.y=true;wizard.update(controls,false,true);
            controls.y=false;wizard.update(controls,false,true);controls.right_trigger=1;
            require(wizard.update(controls,false,true,PanelPointer{.25,rowv(11)}).calibrate,"Lifecycle test did not enter wizard");
            capture.begin(1,{1.5,.3});
            for(unsigned i=0;i<10;++i) capture.observe(head,hands,3,true,true,i*25,1);
            wizard.calibration_status(capture.state());controls.right_trigger=0;
            controls.b=close==0;controls.y=close==1;
            wizard.update(controls,close==2,close!=3);
            const auto status=capture.synchronize(1,wizard.open() && wizard.calibration_page() && !wizard.measurements_page());
            wizard.calibration_status(status);
            require(status==AvatarCalibrationState::cancelled && !capture.result(1),"B/Y/gameplay/focus exit retained incomplete capture");
            for(unsigned i=0;i<=40;++i) capture.observe(head,hands,3,true,true,1000+i*25,1);
            require(!capture.result(1),"Hidden cancelled wizard completed a calibration");
            require(wizard.calibration_required(),"Cancelled setup was not visibly marked for retry");
        }
        for(auto runtime:{AvatarFitRuntimeStatus::verifying,AvatarFitRuntimeStatus::active,AvatarFitRuntimeStatus::recalibration_required}) {
            menu.calibration_status(AvatarCalibrationState::ready);menu.calibration_runtime(runtime);
            require(menu.calibration_runtime_notice(),"Retained ready measurements hid runtime fit notice");
            require(menu.calibration_required()==(runtime==AvatarFitRuntimeStatus::recalibration_required),"Runtime required/active status differs");
            const auto image=settings_panel_image(menu,1.2,3722,3850,{});
            const auto name=runtime==AvatarFitRuntimeStatus::verifying?"verifying":runtime==AvatarFitRuntimeStatus::active?"active":"required";
            std::ofstream preview(std::string("artifacts/calibration-runtime-")+name+".rgba",std::ios::binary);
            preview.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));require(static_cast<bool>(preview),"Cannot save fit runtime preview");
        }
        menu.calibration_runtime(AvatarFitRuntimeStatus::unmeasured);
        for(auto state:{AvatarCalibrationState::reference_changed,AvatarCalibrationState::rig_changed,AvatarCalibrationState::cancelled}) {
            menu.calibration_status(state);require(menu.calibration_required(),"Recalibration-required reason was hidden");
            const auto preview=settings_panel_image(menu,1.2,3722,3850,{});
            const auto name=state==AvatarCalibrationState::reference_changed?"reference":state==AvatarCalibrationState::rig_changed?"rig":"cancelled";
            std::ofstream required(std::string("artifacts/calibration-required-")+name+".rgba",std::ios::binary);
            required.write(reinterpret_cast<const char*>(preview.data()),static_cast<std::streamsize>(preview.size()));
            require(static_cast<bool>(required),"Cannot save recalibration status preview");
        }
        menu.calibration_status(AvatarCalibrationState::ready);menu.calibration_runtime(AvatarFitRuntimeStatus::active);
        require(!menu.calibration_required(),"Verified active fit still requested recalibration");
        const auto pixels=settings_panel_image(menu,1.2,3722,3850,PanelPointer{.85,.26});
        require(pixels.size()==640*560*4,"Panel bitmap has wrong dimensions");
        for(std::size_t i=3;i<pixels.size();i+=4) require(pixels[i]==255,"Panel opacity differs");
        std::ofstream output("artifacts/vr-settings-preview.rgba",std::ios::binary);
        output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
        require(static_cast<bool>(output),"Cannot save settings preview");
        {VrPreferences d;require(d.antialiasing && d.temporal_aa==2 && d.hud_percent==65 && d.controls_hint && d.sharpness==.5 && d.mirror==MirrorMode::right_eye,"VrPreferences defaults differ");}
        const auto preference_file=std::filesystem::path("build/vr-settings-test.cfg");
        save_preferences(preference_file,{1.3,true,{TurnControl::Mode::smooth,45,120},MirrorMode::left_eye,.75,0,65,true});
        {std::ifstream raw(preference_file);int version{};raw>>version;require(version==11,"Saved preferences are not version 11");}
        const auto saved=load_preferences(preference_file);
        require(saved && saved->resolution==1.3 && saved->antialiasing && saved->turning.mode==TurnControl::Mode::smooth &&
            saved->turning.smooth_degrees_per_second==120 && saved->mirror==MirrorMode::left_eye && saved->sharpness==.75 && !saved->temporal_aa &&
            saved->hud_percent==65 && saved->controls_hint,
            "Version 4 preference round trip differs");
        { // mirror_off_round_trips: the fourth monitor view saves, loads, validates and never cycles into itself
            const auto off_file=std::filesystem::path("build/vr-settings-test-off.cfg");
            save_preferences(off_file,{1.0,true,{TurnControl::Mode::snap,45,90},MirrorMode::off,.5,2,65,true});
            const auto off=load_preferences(off_file);
            require(off && off->mirror==MirrorMode::off && static_cast<int>(off->mirror)==3,"Mirror off did not round trip");
            bool rejected=false;try {save_preferences(off_file,{1.0,true,{TurnControl::Mode::snap,45,90},static_cast<MirrorMode>(4),.5,2,65,true});} catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Mirror mode 4 was accepted");
            {std::ofstream raw(off_file,std::ios::trunc);raw<<"7 1 1 0 45 90 4 0.5 2 65 1 0 0 0 0 0\n";}
            bool refused=false;try {load_preferences(off_file);} catch(const std::runtime_error&) {refused=true;}
            require(refused,"A saved mirror mode 4 loaded");
            std::error_code ignored;std::filesystem::remove(off_file,ignored);
            auto off_settings=menu;off_settings.set_mirror(MirrorMode::off);
            require(std::wstring(off_settings.mirror_text())==L"Monitor view: Off","Mirror off has no label");
            off_settings.cycle_mirror(1); // the panel row steps away from off onto a real view
            require(off_settings.mirror()==MirrorMode::right_eye,"Stepping forward from off did not land on the right eye");
            off_settings.set_mirror(MirrorMode::off);off_settings.cycle_mirror(-1);require(off_settings.mirror()==MirrorMode::both_eyes,"Stepping back from off did not land on both eyes");
            off_settings.set_mirror(MirrorMode::both_eyes);off_settings.cycle_mirror(1);require(off_settings.mirror()==MirrorMode::right_eye,"The three views no longer wrap");
        }
        bool rejected=false;try {save_preferences(preference_file,{.1,false,{}});} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && load_preferences(preference_file)->resolution==1.3,"Invalid save changed working preferences");
        rejected=false;try {save_preferences(preference_file,{1.3,false,{},MirrorMode::both_eyes,1.5});} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Out-of-range sharpness was saved");
        rejected=false;try {save_preferences(preference_file,{1.3,false,{},static_cast<MirrorMode>(7),.5});} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Out-of-range monitor view was saved");
        {
            VrPreferences measured=*saved;measured.player_measurements={1.72,.43,.47,{.55,.62}};
            {auto fsr4=measured;fsr4.temporal_aa=5;save_preferences(preference_file,fsr4); // FSR 4 saved from the menu loads back
             require(load_preferences(preference_file)->temporal_aa==5,"FSR 4 preference did not round trip");}
            {   // DLSS modes: version 8 round-trips 7/8; version 7 files never carried them; 9 is out of range.
                auto dlss=measured;dlss.temporal_aa=7;save_preferences(preference_file,dlss);
                require(load_preferences(preference_file)->temporal_aa==7,"DLAA preference did not round trip");
                dlss.temporal_aa=8;save_preferences(preference_file,dlss);require(load_preferences(preference_file)->temporal_aa==8,"DLSS Quality preference did not round trip");
                {std::ifstream raw(preference_file);int version{};raw>>version;require(version>=8,"DLSS preference saved with an old version");}
                bool refused=false;dlss.temporal_aa=9;try {save_preferences(preference_file,dlss);} catch(const std::invalid_argument&) {refused=true;}
                require(refused && load_preferences(preference_file)->temporal_aa==8,"Out-of-range DLSS mode was saved");
                {std::ofstream version7(preference_file);version7<<"7 1.3 1 2 45 120 1 0.25 7 65 1 0 0 0 0 0\n";}
                refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                require(refused,"A version 7 file with a DLSS mode loaded");
                {std::ofstream version8(preference_file);version8<<"8 1.3 1 2 45 120 1 0.25 8 65 1 0 0 0 0 0\n";}
                require(load_preferences(preference_file)->temporal_aa==8,"Version 8 DLSS Quality did not load");
                {std::ofstream version8(preference_file);version8<<"8 1.3 1 2 45 120 1 0.25 9 65 1 0 0 0 0 0\n";}
                refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                require(refused,"Version 8 accepted mode 9");
                {std::ofstream version8(preference_file);version8<<"8 1.3 1 2 45 120 1 0.25 6 65 1 1.7 .4 .45 .55 .62\n";}
                const auto kept=load_preferences(preference_file);
                require(kept && kept->temporal_aa==6 && kept->player_measurements.shoulder_span_metres==.4,"Version 8 kept the FSR 4 mode and measurements");
                { // prefs_v8_defaults / prefs_v9_roundtrip / prefs_v9_fixture / prefs_v10_rejected
                    {std::ofstream version8(preference_file);version8<<"8 1.3 1 2 45 120 1 0.25 6 65 1 0 0 0 0 0\n";}
                    const auto v8=load_preferences(preference_file);require(v8 && !v8->aa_chosen && v8->crosshair,"A v8 file did not default to aa_chosen=false, crosshair=true");
                    {VrPreferences d;require(!d.aa_chosen && d.crosshair,"VrPreferences defaults for the v9 fields differ");}
                    for(const bool chosen:{false,true}) for(const bool cross:{false,true}) {
                        auto v9=dlss;v9.temporal_aa=7;v9.aa_chosen=chosen;v9.crosshair=cross;save_preferences(preference_file,v9);
                        {std::ifstream header(preference_file);int version{};header>>version;require(version==11,"save_preferences did not write version 11");}
                        const auto back=load_preferences(preference_file);require(back && back->aa_chosen==chosen && back->crosshair==cross && back->temporal_aa==7,"v9 fields did not round trip");
                    }
                    const auto fixture=load_preferences(std::filesystem::path(__FILE__).parent_path()/"fixtures"/"vr-settings-v9.cfg");
                    require(fixture && fixture->temporal_aa==7 && fixture->hud_percent==150 && fixture->aa_chosen && !fixture->crosshair,"v9 fixture did not parse");
                    {std::ofstream v9bad(preference_file);v9bad<<"9 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0 2 1\n";}
                    refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                    require(refused,"A v9 file with a marker of 2 loaded");
                    {std::ofstream v9short(preference_file);v9short<<"9 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0\n";}
                    refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                    require(refused,"A v9 file without the new tokens loaded");
                }
                { // v10: scripted_recenter + cutscene_mode; a v9 file keeps every key and takes the defaults; an unknown mode falls back to the default
                    {VrPreferences d;require(d.scripted_recenter && d.cutscene_mode==CutsceneMode::vr_comfort,"VrPreferences defaults for the v10 fields differ");}
                    {std::ofstream v9(preference_file);v9<<"9 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1 0\n";}
                    const auto old=load_preferences(preference_file);
                    require(old && old->temporal_aa==7 && old->hud_percent==150 && old->aa_chosen && !old->crosshair && old->scripted_recenter && old->cutscene_mode==CutsceneMode::vr_comfort,"A v9 file lost a key or missed the v10 defaults");
                    for(const auto mode:{CutsceneMode::vr_comfort,CutsceneMode::vr_full,CutsceneMode::theater}) for(const bool recenter:{false,true}) {
                        auto v10=dlss;v10.temporal_aa=7;v10.scripted_recenter=recenter;v10.cutscene_mode=mode;save_preferences(preference_file,v10);
                        const auto back=load_preferences(preference_file);
                        require(back && back->scripted_recenter==recenter && back->cutscene_mode==mode,"v10 fields did not round trip");
                    }
                    {std::ofstream bad(preference_file);bad<<"10 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0 1 1 1 bogus\n";}
                    const auto unknown=load_preferences(preference_file);require(unknown && unknown->cutscene_mode==CutsceneMode::vr_comfort && unknown->scripted_recenter,"An unknown cutscene mode did not fall back to the default");
                    {std::ofstream bad(preference_file);bad<<"10 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0 1 1 0 theater\n";}
                    const auto fixed=load_preferences(preference_file);require(fixed && !fixed->scripted_recenter && fixed->cutscene_mode==CutsceneMode::theater,"v10 tokens did not parse");
                    {std::ofstream bad(preference_file);bad<<"10 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0 1 1\n";}
                    refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                    require(refused,"A v10 file without the new tokens loaded");
                    const auto fixture10=load_preferences(std::filesystem::path(__FILE__).parent_path()/"fixtures"/"vr-settings-v10.cfg");
                    require(fixture10 && fixture10->temporal_aa==7 && !fixture10->crosshair && !fixture10->scripted_recenter && fixture10->cutscene_mode==CutsceneMode::theater,"v10 fixture did not parse");
                }
                { // v11: the player's Upscaler scale. A v10 file means "no custom scale"; out-of-range or misplaced values mean the preset; a short v11 file is refused
                    {VrPreferences d;require(d.upscale_percent==0,"VrPreferences default has a custom upscaler scale");}
                    {std::ofstream v10(preference_file);v10<<"10 1.3 1 2 45 120 1 0.25 6 150 1 0 0 0 0 0 1 0 0 theater\n";}
                    const auto old=load_preferences(preference_file);require(old && old->upscale_percent==0 && old->temporal_aa==6 && old->cutscene_mode==CutsceneMode::theater,"A v10 file did not load as no custom scale");
                    for(const int percent:{0,50,60,77,100}) {
                        auto v11=dlss;v11.temporal_aa=6;v11.upscale_percent=percent;save_preferences(preference_file,v11);
                        {std::ifstream header(preference_file);int version{};header>>version;require(version==11,"v11 save wrote another version");}
                        const auto back=load_preferences(preference_file);require(back && back->upscale_percent==percent && back->temporal_aa==6,"The upscaler scale did not round trip");
                        require(std::filesystem::file_size(preference_file)<=512,"A v11 file exceeds the 512 byte format limit");
                    }
                    {auto big=dlss;big.temporal_aa=6;big.upscale_percent=60;big.resolution=1.2345678901234567;big.sharpness=.1234567890123456;
                     big.player_measurements={1.7234567890123456,.4312345678901234,.4712345678901234,{.5512345678901234,.6212345678901234}};
                     save_preferences(preference_file,big);require(std::filesystem::file_size(preference_file)<=512,"The longest v11 file exceeds the 512 byte limit");}
                    const auto fixture11=load_preferences(std::filesystem::path(__FILE__).parent_path()/"fixtures"/"vr-settings-v11.cfg");
                    require(fixture11 && fixture11->temporal_aa==6 && fixture11->upscale_percent==60 && fixture11->cutscene_mode==CutsceneMode::theater,"v11 fixture did not parse");
                    {std::ofstream odd(preference_file);odd<<"11 1.3 1 2 45 120 1 0.25 6 65 1 0 0 0 0 0 1 1 1 vr_full 12\n";}
                    const auto low=load_preferences(preference_file);require(low && low->upscale_percent==0,"An out-of-range stored scale did not fall back to the preset");
                    {std::ofstream odd(preference_file);odd<<"11 1.3 1 2 45 120 1 0.25 1 65 1 0 0 0 0 0 1 1 1 vr_full 70\n";}
                    const auto wrong=load_preferences(preference_file);require(wrong && wrong->upscale_percent==0 && wrong->temporal_aa==1,"A scale stored for Game TAA did not fall back to the preset");
                    {std::ofstream bad(preference_file);bad<<"11 1.3 1 2 45 120 1 0.25 6 65 1 0 0 0 0 0 1 1 1 vr_full\n";}
                    refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                    require(refused,"A v11 file without the scale token loaded");
                    for(const int percent:{49,101,-5}) {auto badp=dlss;badp.temporal_aa=6;badp.upscale_percent=percent;refused=false;try {save_preferences(preference_file,badp);} catch(const std::invalid_argument&) {refused=true;}
                        require(refused,"An out-of-range upscaler scale was saved");}
                    {auto badp=dlss;badp.temporal_aa=1;badp.upscale_percent=70;refused=false;try {save_preferences(preference_file,badp);} catch(const std::invalid_argument&) {refused=true;}
                        require(refused,"An upscaler scale was saved for a non-upscaler mode");}
                }
                {std::ofstream version12(preference_file);version12<<"12 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0 0 1 1 vr_full 0\n";}
                refused=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {refused=true;}
                require(refused,"A future version loaded");
            }
            save_preferences(preference_file,measured);const auto roundtrip=load_preferences(preference_file);require(roundtrip.has_value(),"Measured preference round trip missing");
            const auto& m=roundtrip->player_measurements;
            require(m.standing_eye_height_metres==1.72 && m.shoulder_span_metres==.43 && m.torso_width_metres==.47 &&
                m.arm_reach_metres[0]==.55 && m.arm_reach_metres[1]==.62,"Five independent dimensions did not persist");
            const auto bytes=[&] {std::ifstream file(preference_file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});};
            const auto preserved=bytes();
            for(unsigned field=0;field<5;++field) {
                auto bad=measured;
                auto& value=field==0?bad.player_measurements.standing_eye_height_metres:field==1?bad.player_measurements.shoulder_span_metres:
                    field==2?bad.player_measurements.torso_width_metres:bad.player_measurements.arm_reach_metres[field-3];
                value=std::numeric_limits<double>::quiet_NaN();rejected=false;
                try {save_preferences(preference_file,bad);}catch(const std::invalid_argument&) {rejected=true;}
                require(rejected && bytes()==preserved,"Invalid measured save altered the working file");
            }
            {std::ofstream version6(preference_file);version6<<"6 1.3 1 2 45 120 1 0.25 2 65 1\n";}
            const auto migrated6=load_preferences(preference_file);require(migrated6.has_value(),"Version6 failed migration");
            const auto& blank=migrated6->player_measurements;
            require(!blank.standing_eye_height_metres && !blank.shoulder_span_metres && !blank.torso_width_metres &&
                !blank.arm_reach_metres[0] && !blank.arm_reach_metres[1],"Version6 invented measured dimensions");
            {std::ofstream zeros(preference_file);zeros<<"7 1.3 1 2 45 120 1 0.25 2 65 1 0 0 0 0 0\n";}
            require(!load_preferences(preference_file)->player_measurements.torso_width_metres,"Zero sentinel became a measured width");
            {std::ofstream bad(preference_file);bad<<"7 1.3 1 2 45 120 1 0.25 2 65 1 1.7 .4 4 .55 .62\n";}
            const auto invalid_bytes=bytes();rejected=false;
            try {load_preferences(preference_file);}catch(const std::exception&) {rejected=true;}
            require(rejected && bytes()==invalid_bytes,"Invalid measured load was accepted or changed saved file");
        }
        {std::ofstream legacy(preference_file);legacy<<"1 1.4 1 2 45 120\n";}
        const auto migrated=load_preferences(preference_file);
        require(migrated && migrated->resolution==1.4 && migrated->antialiasing && migrated->turning.mode==TurnControl::Mode::smooth &&
            migrated->turning.smooth_degrees_per_second==120 && migrated->mirror==MirrorMode::right_eye && migrated->sharpness==.5 && migrated->temporal_aa==2 &&
            migrated->hud_percent==65 && migrated->controls_hint,
            "Version 1 migration differs");
        {std::ofstream version2(preference_file);version2<<"2 1.3 1 2 45 120 1 0.25\n";}
        const auto older=load_preferences(preference_file);
        require(older && older->antialiasing && older->mirror==MirrorMode::left_eye && older->sharpness==.25 && older->temporal_aa==2 &&
            older->hud_percent==65 && older->controls_hint,
            "Version 2 migration differs");
        {std::ofstream version3(preference_file);version3<<"3 1.3 1 2 45 120 1 0.25 1\n";}
        const auto third=load_preferences(preference_file);
        require(third && third->antialiasing && third->temporal_aa && third->mirror==MirrorMode::left_eye && third->sharpness==.25 &&
            third->hud_percent==65 && third->controls_hint,
            "Version 3 migration differs");
        {std::ofstream invalid(preference_file);invalid<<"4 1.3 1 2 45 120 0 0.5 1 2 1";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Out-of-range aim reticle was loaded");
        {std::ofstream invalid(preference_file);invalid<<"4 1.3 1 2 45 120 0 0.5 1 1 2";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Out-of-range controls hint was loaded");
        {std::ofstream invalid(preference_file);invalid<<"4 1.3 1 2 45 120 0 0.5 1 1";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Missing controls hint field was accepted");
        {std::ofstream invalid(preference_file);invalid<<"4 1.3 1 2 45 120 0 0.5 1 1 1 extra";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Unexpected version 4 fields accepted");
        {std::ofstream invalid(preference_file);invalid<<"3 1.3 1 2 45 120 0 0.5 2";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Out-of-range temporal AA was loaded");
        {std::ofstream invalid(preference_file);invalid<<"2 1.3 1 2 45 120 4 0.5";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Out-of-range monitor view was loaded");
        {std::ofstream invalid(preference_file);invalid<<"2 1.3 1 2 45 120 0 1.5";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Out-of-range sharpness was loaded");
        {std::ofstream invalid(preference_file);invalid<<"2 1.3 1 2 45 120 0 nan";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Non-finite sharpness was loaded");
        {std::ofstream invalid(preference_file);invalid<<"3 1.3 1 2 45 120 0 0.5";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Missing temporal AA field was accepted");
        {std::ofstream invalid(preference_file);invalid<<"1 1.3 1 0 45 90 extra";}
        rejected=false;try {load_preferences(preference_file);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Unexpected preference fields accepted");std::filesystem::remove(preference_file);
        {   // G5/C6: hand fit sub-page of the calibration page
            VrSettings fit;fit.initialize(1,true);ControllerInput c{};c.active=true;
            fit.palm_offsets(PalmOffset{{.01,.02,0},12,0},PalmOffset{{-.02,-.01,.03},9,4});
            fit.open_calibration_from_native();fit.update(c,false,true);
            require(fit.calibration_page(),"hand fit test: no calibration page");
            require(!fit.hand_fit_page(),"hand fit open before X");
            c.x=true;auto e=fit.update(c,false,true);
            require(fit.hand_fit_page() && fit.calibration_page() && e.consume && !e.calibrate && !e.palm_offset,"X did not open hand fit");
            c.x=false;fit.update(c,false,true);
            require(fit.palm_preview(0) && fit.palm_preview(1),"no preview on the fit page");
            {const auto page=settings_panel_image(fit,1,1280,1120,{}),calibration=settings_panel_image(VrSettings{},1,1280,1120,{});
             require(!page.empty() && page!=calibration,"hand-fit page did not render");}
            require(std::abs(fit.palm_preview(1)->translation.x+.02)<1e-12 && std::abs(fit.palm_preview(0)->pitch_deg-12)<1e-12,"fit did not start from the current offsets");
            // Y and A have their old meanings only off the sub-page: here Y does not open measurements, A does not start capture
            c.y=true;e=fit.update(c,false,true);require(!fit.measurements_page() && fit.hand_fit_page() && e.consume,"Y acted on the hand-fit page");c.y=false;fit.update(c,false,true);
            c.a=true;e=fit.update(c,false,true);require(!e.calibrate && fit.hand_fit_side()==0,"A on hand fit: calibrated or did not switch hands");c.a=false;fit.update(c,false,true);
            c.a=true;fit.update(c,false,true);c.a=false;fit.update(c,false,true);require(fit.hand_fit_side()==1,"A did not switch back");
            // the sticks move only the active (right) hand
            c.move_y=1;for(int i=0;i<90;++i) fit.update(c,false,true,{},true,1./90);c.move_y=0;
            require(std::abs(fit.palm_preview(1)->translation.x-(-.02+.02))<1e-9,"stick did not move the active hand 2 cm/s");
            require(std::abs(fit.palm_preview(0)->translation.x-.01)<1e-12,"stick moved the inactive hand");
            // a trigger confirms that hand only
            c.right_trigger=1;e=fit.update(c,false,true);
            require(e.palm_offset && e.palm_offset->side==1 && std::abs(e.palm_offset->value.translation.x)<1e-9,"right trigger did not confirm the right hand");
            require(fit.hand_fit_page(),"confirm closed the page");
            e=fit.update(c,false,true);require(!e.palm_offset,"held trigger confirmed twice");
            c.right_trigger=0;fit.update(c,false,true);
            c.left_trigger=1;e=fit.update(c,false,true);
            require(e.palm_offset && e.palm_offset->side==0 && std::abs(e.palm_offset->value.pitch_deg-12)<1e-12,"left trigger did not confirm the left hand");
            c.left_trigger=0;fit.update(c,false,true);
            // B leaves without a change, and the preview is gone
            c.b=true;e=fit.update(c,false,true);c.b=false;
            require(!e.palm_offset && !fit.hand_fit_page() && fit.calibration_page() && !fit.palm_preview(0),"B did not return without saving");
            fit.update(c,false,true);
            // leaving mid-fit (focus loss) drops the preview
            c.x=true;fit.update(c,false,true);c.x=false;require(fit.hand_fit_page(),"re-enter failed");
            fit.suspend();require(!fit.hand_fit_page() && !fit.palm_preview(1),"suspend left the fit open");
            // a deadzone keeps a resting stick from drifting the hand
            VrSettings rest;rest.initialize(1,true);rest.open_calibration_from_native();ControllerInput r{};r.active=true;rest.update(r,false,true);
            r.x=true;rest.update(r,false,true);r.x=false;rest.update(r,false,true);r.move_x=.1f;r.turn_y=-.15f;
            for(int i=0;i<200;++i) rest.update(r,false,true,{},true,.02);
            require(std::abs(rest.palm_preview(1)->translation.y+.03)<1e-12 && std::abs(rest.palm_preview(1)->pitch_deg-15)<1e-12,"stick drift inside the deadzone moved the hand");
        }
        std::cout<<"PASS: panel ray, debounced settings and measured dimensions, staged guidance/cancellation, focus release, bitmap and atomic v7 persistence with v6/v4/v3/v2/v1 migration\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
