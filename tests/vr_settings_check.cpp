#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/vr_settings.hpp"
#include "../src/settings_panel_image.hpp"
#include "../src/vr_preferences.hpp"
#include <fstream>
#include <iostream>
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;
        {
            VrSettings setup;ControllerInput neutral{};neutral.active=true;
            setup.calibration_runtime(AvatarFitRuntimeStatus::recalibration_required);
            require(!setup.update(neutral,true,true).consume,"Calibration interrupted gameplay");
            require(!setup.update(neutral,false,true,{},false).consume,"Calibration interrupted a non-pause page");
            setup.update(neutral,false,true);
            require(setup.update(neutral,false,true).opened && setup.calibration_page(),"First pause did not show required calibration");
            neutral.a=true;require(setup.update(neutral,false,true).calibrate,"Setup A did not start capture");
            neutral.a=false;setup.update(neutral,false,true);
            neutral.y=true;setup.update(neutral,false,true);require(!setup.open(),"Setup Y did not close page");
            neutral.y=false;require(!setup.update(neutral,false,true).opened,"Cancelled setup reopened every frame");
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
        const auto decrease=menu.update(in,false,true,PanelPointer{.15,.25});
        require(decrease.resolution && std::abs(*decrease.resolution-1.2)<1e-6,"Pointer minus click failed");
        require(!menu.update(in,false,true,PanelPointer{.15,.25}).resolution,"Held trigger repeated click");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto taa_toggle=menu.update(in,false,true,PanelPointer{.5,.32});
        require(taa_toggle.temporal_aa && *taa_toggle.temporal_aa && menu.temporal_aa(),"Pointer did not toggle temporal AA");
        require(!menu.update(in,false,true,PanelPointer{.5,.32}).temporal_aa,"Held trigger changed temporal AA repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto toggle=menu.update(in,false,true,PanelPointer{.5,.38});
        require(toggle.antialiasing && !*toggle.antialiasing,"Pointer did not toggle AA");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto mirror_click=menu.update(in,false,true,PanelPointer{.5,.45});
        require(mirror_click.mirror && *mirror_click.mirror==MirrorMode::right_eye && menu.mirror()==MirrorMode::right_eye,"Pointer did not cycle monitor view");
        require(!menu.update(in,false,true,PanelPointer{.5,.45}).mirror,"Held trigger changed monitor view repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto sharp_click=menu.update(in,false,true,PanelPointer{.5,.51});
        require(sharp_click.sharpness && *sharp_click.sharpness==.75 && menu.sharpness()==.75,"Pointer did not cycle sharpening");
        require(!menu.update(in,false,true,PanelPointer{.5,.51}).sharpness,"Held trigger changed sharpening repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto reticle_click=menu.update(in,false,true,PanelPointer{.5,.58});
        require(reticle_click.aim_reticle && !*reticle_click.aim_reticle && !menu.aim_reticle(),"Pointer did not toggle the aim reticle");
        require(!menu.update(in,false,true,PanelPointer{.5,.58}).aim_reticle,"Held trigger changed the aim reticle repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto hint_click=menu.update(in,false,true,PanelPointer{.5,.64});
        require(hint_click.controls_hint && !*hint_click.controls_hint && !menu.controls_hint(),"Pointer did not toggle the controls hint");
        require(!menu.update(in,false,true,PanelPointer{.5,.64}).controls_hint,"Held trigger changed the controls hint repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        const auto turn=menu.update(in,false,true,PanelPointer{.5,.71});
        require(turn.turning && turn.turning->mode==TurnControl::Mode::snap && turn.turning->snap_degrees==90,"Turning preset did not advance");
        require(!menu.update(in,false,true,PanelPointer{.5,.71}).turning,"Held trigger changed turning repeatedly");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        require(menu.update(in,false,true,PanelPointer{.5,.76}).recenter,"Recenter action did not fire");
        require(!menu.update(in,false,true,PanelPointer{.5,.76}).recenter,"Held trigger repeatedly recentered");
        in.right_trigger=0;menu.update(in,false,true);in.right_trigger=1;
        require(menu.update(in,false,true,PanelPointer{.25,.76}).calibrate && menu.calibration_page(),"Avatar calibration page did not open");
        require(!menu.update(in,false,true,PanelPointer{.25,.76}).calibrate,"Held trigger restarted calibration");
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
            require(mirror_stick.mirror && *mirror_stick.mirror==MirrorMode::right_eye && rows.mirror()==MirrorMode::right_eye,"Stick did not cycle monitor view");
            press(0,-1);
            const auto sharp_stick=press(1,0);
            require(sharp_stick.sharpness && *sharp_stick.sharpness==.75 && rows.sharpness()==.75,"Stick did not cycle sharpening");
            press(0,-1);
            const auto reticle_stick=press(1,0);
            require(reticle_stick.aim_reticle && !*reticle_stick.aim_reticle && !rows.aim_reticle(),"Stick did not toggle the aim reticle");
            press(0,-1);
            const auto hint_stick=press(1,0);
            require(hint_stick.controls_hint && !*hint_stick.controls_hint && !rows.controls_hint(),"Stick did not toggle the controls hint");
            const auto rows_image=settings_panel_image(rows,1.2,3722,3850,{});
            require(pixel(rows_image,30,76,0)==38,"The resolution row fell outside the panel bitmap");
            for(unsigned row=1;row<10;++row) {
                const auto shade=pixel(rows_image,30,128+36*row+4,0);
                require(shade==36 || shade==38,"A settings row fell outside the panel bitmap");
            }
            // Wrapping upwards from the controls hint row reaches the last, whose A closes.
            press(0,1);press(0,1);press(0,1);press(0,1);press(0,1);press(0,1);press(0,1);
            stick.a=true;const auto back=rows.update(stick,false,true);stick.a=false;
            require(back.consume && !rows.open(),"Wrapped vertical navigation did not reach the back row");
        }
        for(unsigned close=0;close<4;++close) {
            VrSettings wizard;ControllerInput controls{};controls.active=true;
            AvatarCalibration capture;const TrackedPose head{{0,1.7,0},{}};
            const std::array<TrackedPose,2> hands{{{{-.8,1.48,0},{}},{{.8,1.48,0},{}}}};
            wizard.update(controls,false,true);controls.y=true;wizard.update(controls,false,true);
            controls.y=false;wizard.update(controls,false,true);controls.right_trigger=1;
            require(wizard.update(controls,false,true,PanelPointer{.25,.75}).calibrate,"Lifecycle test did not enter wizard");
            capture.begin(1,{1.5,.3});
            for(unsigned i=0;i<10;++i) capture.observe(head,hands,3,true,true,i*25,1);
            wizard.calibration_status(capture.state());controls.right_trigger=0;
            controls.b=close==0;controls.y=close==1;
            wizard.update(controls,close==2,close!=3);
            const auto status=capture.synchronize(1,wizard.open() && wizard.calibration_page());
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
        {VrPreferences d;require(d.antialiasing && !d.temporal_aa && d.aim_reticle && d.controls_hint && d.sharpness==.5 && d.mirror==MirrorMode::both_eyes,"VrPreferences defaults differ");}
        const auto preference_file=std::filesystem::path("build/vr-settings-test.cfg");
        save_preferences(preference_file,{1.3,true,{TurnControl::Mode::smooth,45,120},MirrorMode::left_eye,.75,false,false,true});
        {std::ifstream raw(preference_file);int version{};raw>>version;require(version==4,"Saved preferences are not version 4");}
        const auto saved=load_preferences(preference_file);
        require(saved && saved->resolution==1.3 && saved->antialiasing && saved->turning.mode==TurnControl::Mode::smooth &&
            saved->turning.smooth_degrees_per_second==120 && saved->mirror==MirrorMode::left_eye && saved->sharpness==.75 && !saved->temporal_aa &&
            !saved->aim_reticle && saved->controls_hint,
            "Version 4 preference round trip differs");
        bool rejected=false;try {save_preferences(preference_file,{.1,false,{}});} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && load_preferences(preference_file)->resolution==1.3,"Invalid save changed working preferences");
        rejected=false;try {save_preferences(preference_file,{1.3,false,{},MirrorMode::both_eyes,1.5});} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Out-of-range sharpness was saved");
        rejected=false;try {save_preferences(preference_file,{1.3,false,{},static_cast<MirrorMode>(7),.5});} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Out-of-range monitor view was saved");
        {std::ofstream legacy(preference_file);legacy<<"1 1.4 1 2 45 120\n";}
        const auto migrated=load_preferences(preference_file);
        require(migrated && migrated->resolution==1.4 && migrated->antialiasing && migrated->turning.mode==TurnControl::Mode::smooth &&
            migrated->turning.smooth_degrees_per_second==120 && migrated->mirror==MirrorMode::both_eyes && migrated->sharpness==.5 && !migrated->temporal_aa &&
            migrated->aim_reticle && migrated->controls_hint,
            "Version 1 migration differs");
        {std::ofstream version2(preference_file);version2<<"2 1.3 1 2 45 120 1 0.25\n";}
        const auto older=load_preferences(preference_file);
        require(older && older->antialiasing && older->mirror==MirrorMode::left_eye && older->sharpness==.25 && !older->temporal_aa &&
            older->aim_reticle && older->controls_hint,
            "Version 2 migration differs");
        {std::ofstream version3(preference_file);version3<<"3 1.3 1 2 45 120 1 0.25 1\n";}
        const auto third=load_preferences(preference_file);
        require(third && third->antialiasing && third->temporal_aa && third->mirror==MirrorMode::left_eye && third->sharpness==.25 &&
            third->aim_reticle && third->controls_hint,
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
        {std::ofstream invalid(preference_file);invalid<<"2 1.3 1 2 45 120 3 0.5";}
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
        std::cout<<"PASS: panel ray, debounced settings, temporal AA/SMAA/monitor view/sharpening/aim reticle/controls hint rows, calibration exit cancellation/retry reasons, focus release, bitmap and atomic v4/v3/v2/v1 preference persistence\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
