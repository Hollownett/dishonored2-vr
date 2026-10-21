#pragma once
#include "tracked_pose.hpp"
#include "controller_input.hpp"
#include "turn_control.hpp"
#include "vr_preferences.hpp"
#include "avatar_calibration.hpp"
#include "palm_offset.hpp"
#include <algorithm>
#include <optional>

namespace d2vr {
struct PanelPointer {double u{},v{};};
inline std::optional<PanelPointer> panel_pointer(const TrackedPose& aim,const TrackedPose& panel,double width,double height) {
    if(!finite(aim.position) || !finite(panel.position) || width<=0 || height<=0) return {};
    auto rotation=normalized(panel.orientation);rotation={-rotation.x,-rotation.y,-rotation.z,rotation.w};
    const auto origin=rotated(rotation,aim.position-panel.position);
    const auto direction=rotated(rotation,rotated(normalized(aim.orientation),{0,0,-1}));
    // A front-facing ray only; this also rejects rays parallel to the panel.
    if(direction.z>=-1e-6 || origin.z<=0) return {};
    const auto distance=-origin.z/direction.z;
    const auto hit=origin+direction*distance;
    PanelPointer p{hit.x/width+.5,.5-hit.y/height};
    return p.u>=0 && p.u<=1 && p.v>=0 && p.v<=1?std::optional<PanelPointer>(p):std::nullopt;
}
class VrSettings {
    bool previous_y_{},previous_a_{},previous_b_{},previous_x_{},previous_trigger_{},previous_left_trigger_{},stick_held_{};
    bool controls_valid_{};
    bool open_{};
    bool close_latch_{}; // gameplay panel just closed: input held through the close stays consumed until released
    bool gameplay_open_{}; // opened by holding Y during play: the panel keeps running while `gameplay` is true
    bool calibration_page_{},measurements_page_{};
    bool hand_fit_page_{};   // G5/C6: the hand-fit sub-page of the calibration page (X); both hands are fitted live, sticks move the active one
    std::array<PalmFitStep,2> fit_{};
    std::array<PalmOffset,2> palm_current_{default_palm_offset(0),default_palm_offset(1)};
    unsigned fit_side_=1;
    void leave_hand_fit() noexcept {hand_fit_page_=false;for(auto& step:fit_) step.cancel();}
    unsigned measurement_selected_{};
    AvatarPlayerMeasurements player_measurements_{};
    AvatarCalibrationStage calibration_stage_{AvatarCalibrationStage::shoulders};
    bool calibration_floor_available_{};
    std::optional<double>& measurement(unsigned index) {
        switch(index) {case 0:return player_measurements_.standing_eye_height_metres;case 1:return player_measurements_.shoulder_span_metres;
            case 2:return player_measurements_.torso_width_metres;default:return player_measurements_.arm_reach_metres[index-3];}
    }
    bool calibration_prompted_{};
    AvatarCalibrationState calibration_state_{AvatarCalibrationState::idle};
    double calibration_progress_{};
    AvatarFitRuntimeStatus calibration_runtime_{AvatarFitRuntimeStatus::unmeasured};
    unsigned selected_{};
    double requested_=1;
    bool aa_{};
    int temporal_aa_{2};
    unsigned upscaler_generation_{3}; // Actual runtime backend; preferences retain the existing quality modes.
    int hud_percent_{65};
    bool controls_hint_{true};
    TurnControl::Config turning_;
    MirrorMode mirror_{MirrorMode::both_eyes};
    double sharpness_{.5};
    bool native_calibration_{}; // calibration opened from the game's own options page: B returns there
    void cycle_turning(int direction) {
        constexpr std::array<TurnControl::Config,7> choices{{
            {TurnControl::Mode::snap,30,90},{TurnControl::Mode::snap,45,90},{TurnControl::Mode::snap,90,90},
            {TurnControl::Mode::smooth,45,45},{TurnControl::Mode::smooth,45,90},{TurnControl::Mode::smooth,45,120},
            {TurnControl::Mode::native,45,90}}};
        int current=1;
        for(int i=0;i<static_cast<int>(choices.size());++i) {
            const auto& c=choices[i];
            if(c.mode==turning_.mode && (c.mode==TurnControl::Mode::native ||
               (c.mode==TurnControl::Mode::snap?c.snap_degrees==turning_.snap_degrees:c.smooth_degrees_per_second==turning_.smooth_degrees_per_second))) current=i;
        }
        turning_=choices[(current+direction+static_cast<int>(choices.size()))%choices.size()];
    }
    void cycle_mirror(int direction) {mirror_=static_cast<MirrorMode>((static_cast<int>(mirror_)+direction+3)%3);}
    void cycle_sharpness(int direction) {
        constexpr std::array<double,5> choices{{0,.25,.5,.75,1}};
        int current=2;
        for(int i=0;i<static_cast<int>(choices.size());++i) if(choices[i]==sharpness_) current=i;
        sharpness_=choices[(current+direction+static_cast<int>(choices.size()))%choices.size()];
    }
public:
    struct PalmOffsetChange {unsigned side{};PalmOffset value{};};
    struct Changes {std::optional<double> resolution;std::optional<bool> antialiasing;std::optional<int> temporal_aa;std::optional<TurnControl::Config> turning;
                   std::optional<MirrorMode> mirror;std::optional<double> sharpness;std::optional<int> hud_percent;std::optional<bool> controls_hint;
                   bool recenter{},consume{},opened{},calibrate{},player_measurements{},test_unlock{};
                   std::optional<PalmOffsetChange> palm_offset;};
    void initialize(double scale,bool aa,TurnControl::Config turning={},MirrorMode mirror=MirrorMode::both_eyes,double sharpness=.5,int temporal_aa=2,
        int hud_percent=65,bool controls_hint=true,AvatarPlayerMeasurements player_measurements={}) {
        requested_=scale;aa_=aa;turning_=turning;mirror_=mirror;sharpness_=sharpness;temporal_aa_=temporal_aa;
        hud_percent_=hud_percent;controls_hint_=controls_hint;player_measurements_=player_measurements;
    }
    bool open() const noexcept {return open_;}
    bool calibration_page() const noexcept {return calibration_page_;}
    bool measurements_page() const noexcept {return measurements_page_;}
    bool hand_fit_page() const noexcept {return hand_fit_page_;}
    unsigned hand_fit_side() const noexcept {return fit_side_;}
    // The offsets the fit starts from (the saved or default values the game is using now); the game refreshes them while the page is closed.
    void palm_offsets(const PalmOffset& left,const PalmOffset& right) noexcept {if(!hand_fit_page_) palm_current_={left,right};}
    // The live preview while the page is open; the hand the game draws follows it.
    std::optional<PalmOffset> palm_preview(unsigned side) const {
        if(!hand_fit_page_) return std::nullopt;
        return fit_[side&1u].value();
    }
    unsigned measurement_selected() const noexcept {return measurement_selected_;}
    const AvatarPlayerMeasurements& player_measurements() const noexcept {return player_measurements_;}
    AvatarCalibrationStage calibration_stage() const noexcept {return calibration_stage_;}
    bool calibration_floor_available() const noexcept {return calibration_floor_available_;}
    void calibration_environment(AvatarCalibrationStage stage,bool floor_available) {calibration_stage_=stage;calibration_floor_available_=floor_available;}
    const wchar_t* measurement_label(unsigned row) const {
        static constexpr const wchar_t* labels[]={L"Standing eye height",L"Shoulder joint width",L"Chest width (side to side)",L"Left shoulder-to-palm reach",L"Right shoulder-to-palm reach"};
        return labels[row<5?row:0];
    }
    std::optional<double> measurement_value(unsigned row) const {
        switch(row) {case 0:return player_measurements_.standing_eye_height_metres;case 1:return player_measurements_.shoulder_span_metres;
            case 2:return player_measurements_.torso_width_metres;default:return row<5?player_measurements_.arm_reach_metres[row-3]:std::nullopt;}
    }
    AvatarCalibrationState calibration_state() const noexcept {return calibration_state_;}
    double calibration_progress() const noexcept {return calibration_progress_;}
    const wchar_t* calibration_status_text() const noexcept {
        switch(calibration_state_) {
            case AvatarCalibrationState::hold_pose:return L"Hold still - measuring your arms...";
            case AvatarCalibrationState::release_grips:return L"Shoulders captured - release grips, then extend arms";
            case AvatarCalibrationState::grips_released:return L"Hold both grips closed, arms out to the sides (T or A pose)";
            case AvatarCalibrationState::pose_moving:return L"Steady... keep arms out and still";
            case AvatarCalibrationState::tracking_lost:return L"Controllers not seen - bring them into view";
            case AvatarCalibrationState::pose_invalid:return L"Hold both arms out to your sides";
            case AvatarCalibrationState::dimensions_unsupported:return L"Arm span not supported - press A to redo";
            case AvatarCalibrationState::ready:return calibration_runtime_==AvatarFitRuntimeStatus::unmeasured?L"Measurement saved - fit not active":calibration_runtime_text();
            case AvatarCalibrationState::reference_changed:return L"View recentered - body size kept; press A to redo";
            case AvatarCalibrationState::rig_changed:return L"Avatar changed - body size kept; press A to redo";
            case AvatarCalibrationState::cancelled:return L"Capture cancelled - press A to start";
            default:return L"Press A to start";
        }
    }
    const wchar_t* calibration_height_text() const noexcept {
        if(player_measurements_.standing_eye_height_metres)return L"Standing eye height uses your entered measurement";
        return calibration_floor_available_?L"Standing eye height uses the runtime floor":L"No runtime floor: enter standing eye height with Y";
    }
    AvatarFitRuntimeStatus calibration_runtime() const noexcept {return calibration_runtime_;}
    void calibration_runtime(AvatarFitRuntimeStatus status) noexcept {
        calibration_runtime_=status;
        if(status==AvatarFitRuntimeStatus::active)calibration_prompted_=false;
    }
    const wchar_t* calibration_runtime_text() const noexcept {
        switch(calibration_runtime_) {
            case AvatarFitRuntimeStatus::verifying:return L"Checking avatar - fit suspended";
            case AvatarFitRuntimeStatus::active:return L"Avatar fit active";
            case AvatarFitRuntimeStatus::recalibration_required:return L"Recalibration required";
            default:return L"Fit not measured";
        }
    }
    bool calibration_runtime_notice() const noexcept {return calibration_runtime_!=AvatarFitRuntimeStatus::unmeasured;}
    bool calibration_required() const noexcept {
        if(calibration_runtime_==AvatarFitRuntimeStatus::active)return false;
        return calibration_runtime_==AvatarFitRuntimeStatus::recalibration_required || calibration_state_==AvatarCalibrationState::reference_changed || calibration_state_==AvatarCalibrationState::rig_changed ||
            calibration_state_==AvatarCalibrationState::cancelled;
    }
    void calibration_status(AvatarCalibrationState state,double progress=0) noexcept {
        calibration_state_=state;
        calibration_progress_=std::isfinite(progress)?std::clamp(progress,0.,1.):0.;
        if(state==AvatarCalibrationState::reference_changed || state==AvatarCalibrationState::rig_changed)calibration_runtime_=AvatarFitRuntimeStatus::recalibration_required;
    }
    unsigned selected() const noexcept {return selected_;}
    double requested() const noexcept {return requested_;}
    bool antialiasing() const noexcept {return aa_;}
    int temporal_aa() const noexcept {return temporal_aa_;}
    void upscaler_generation(unsigned value) noexcept {upscaler_generation_=value;}
    int hud_percent() const noexcept {return hud_percent_;}
    bool controls_hint() const noexcept {return controls_hint_;}
    TurnControl::Config turning() const noexcept {return turning_;}
    MirrorMode mirror() const noexcept {return mirror_;}
    double sharpness() const noexcept {return sharpness_;}
    const wchar_t* temporal_aa_text() const noexcept {
        // FSR 4 modes say when the GPU/provider fell back to FSR 3 (upscaler generation 3).
        if(temporal_aa_==5) return upscaler_generation_==3?L"Anti-aliasing: FSR 4 Native (unavailable, FSR 3)":L"Anti-aliasing: FSR 4 Native";
        if(temporal_aa_==6) return upscaler_generation_==3?L"Anti-aliasing: FSR 4 Quality (unavailable, FSR 3)":L"Anti-aliasing: FSR 4 Quality (77%)";
        switch(temporal_aa_) {
            case 1:return L"Anti-aliasing: Game TAA";
            case 2:return L"Anti-aliasing: FSR 3 Native";
            case 3:return L"Anti-aliasing: FSR 3 Quality (77%)";
            case 4:return L"Anti-aliasing: FSR 3 Balanced (67%)";
            default:return L"Anti-aliasing: Off";
        }
    }
    const wchar_t* hud_text() const noexcept {
        switch(hud_percent_) {
            case 55:return L"HUD size: 55% (centre)";
            case 75:return L"HUD size: 75%";
            case 85:return L"HUD size: 85%";
            case 100:return L"HUD size: 100% (game layout)";
            case 125:return L"HUD size: 125%";
            case 150:return L"HUD size: 150%";
            case 200:return L"HUD size: 200%";
            default:return L"HUD size: 65%";
        }
    }
    void cycle_hud(int direction) {
        constexpr std::array<int,8> choices{{55,65,75,85,100,125,150,200}}; // above 100: larger prompts and markers
        int current=1;const int n=static_cast<int>(choices.size());
        for(int i=0;i<n;++i) if(choices[i]==hud_percent_) current=i;
        hud_percent_=choices[(current+direction+n)%n];
    }
    const wchar_t* controls_hint_text() const noexcept {return controls_hint_?L"Controls hint: On":L"Controls hint: Off";}
    const wchar_t* mirror_text() const noexcept {
        switch(mirror_) {
            case MirrorMode::right_eye:return L"Monitor view: Right eye";
            case MirrorMode::left_eye:return L"Monitor view: Left eye";
            default:return L"Monitor view: Both eyes";
        }
    }
    const wchar_t* sharpness_text() const noexcept {
        switch(static_cast<int>(std::lround(sharpness_*4))) {
            case 0:return L"Sharpening: Off";
            case 1:return L"Sharpening: Low";
            case 3:return L"Sharpening: High";
            case 4:return L"Sharpening: Max";
            default:return L"Sharpening: Medium";
        }
    }
    void suspend() noexcept {open_=false;gameplay_open_=false;close_latch_=false;calibration_page_=measurements_page_=false;leave_hand_fit();controls_valid_=false;stick_held_=false;}
    // Opened from the native menu's "VR Settings" item: held buttons from that click are not replayed.
    void open_from_menu() noexcept {open_=true;calibration_page_=measurements_page_=false;leave_hand_fit();selected_=0;controls_valid_=false;stick_held_=false;native_calibration_=false;}
    // Opened in gameplay by holding Y (y_hold.hpp): the same panel on page 0. Y (a new press) or B closes it.
    void open_from_gameplay() noexcept {open_from_menu();gameplay_open_=true;}
    // The game's own options page (vr_settings_native.hpp) sets values directly; the panel shows only calibration then.
    void open_calibration_from_native() noexcept {open_=calibration_page_=true;measurements_page_=false;leave_hand_fit();selected_=8;controls_valid_=false;stick_held_=false;native_calibration_=true;}
    void set_requested(double v) noexcept {requested_=std::clamp(v,.5,2.);}
    void set_antialiasing(bool v) noexcept {aa_=v;}
    void set_temporal_aa(int v) noexcept {temporal_aa_=std::clamp(v,0,6);}
    void set_mirror(MirrorMode v) noexcept {mirror_=v;}
    void set_sharpness(double v) noexcept {sharpness_=std::clamp(v,0.,1.);}
    void set_hud_percent(int v) noexcept {hud_percent_=v;}
    void set_controls_hint(bool v) noexcept {controls_hint_=v;}
    void set_turning(TurnControl::Config v) noexcept {turning_=v;}
    // A proved owner/resource/reference change is distinct from focus loss or
    // deliberate dismissal. Only this notification rearms the required prompt.
    void calibration_identity_changed() noexcept {
        suspend();calibration_prompted_=false;
        calibration_runtime_=AvatarFitRuntimeStatus::recalibration_required;
    }
    Changes update(const ControllerInput& in,bool gameplay,bool focused,std::optional<PanelPointer> pointer={},bool settings_available=true,double dt=1./90) {
        Changes out{};
        if(!gameplay) {gameplay_open_=close_latch_=false;}
        else if(gameplay_open_ && !open_) {gameplay_open_=false;close_latch_=true;} // closed (Y, B or the close row) since the last update
        if(close_latch_ && focused && in.active) {
            // A button held through the close (B would holster the sword, Y would talk) must not reach the game as a fresh press.
            if(in.a || in.b || in.x || in.y || in.left_click || in.right_click || in.left_trigger>.3f || in.right_trigger>.3f ||
               in.left_grip>.3f || in.right_grip>.3f) {out.consume=true;return out;}
            close_latch_=false;
        }
        const bool from_gameplay=gameplay && gameplay_open_ && open_;
        if(!focused || !in.active || (gameplay && !from_gameplay) || (!settings_available && !from_gameplay)) {suspend();return out;}
        const bool trigger=in.right_trigger>.55f;
        if(!controls_valid_) {
            // Returning to focus never interprets held buttons as fresh clicks.
            previous_y_=in.y;previous_a_=in.a;previous_b_=in.b;previous_x_=in.x;previous_trigger_=trigger;previous_left_trigger_=in.left_trigger>.55f;
            stick_held_=std::hypot(in.move_x,in.move_y)>.35;controls_valid_=true;
            out.consume=open_;return out;
        }
        const bool y=in.y&&!previous_y_,a=in.a&&!previous_a_,b=in.b&&!previous_b_,click=trigger&&!previous_trigger_,x=in.x&&!previous_x_;
        const bool left_trigger=in.left_trigger>.55f,left_click=left_trigger&&!previous_left_trigger_;
        previous_y_=in.y;previous_a_=in.a;previous_b_=in.b;previous_x_=in.x;previous_trigger_=trigger;previous_left_trigger_=left_trigger;
        if(!gameplay && calibration_runtime_==AvatarFitRuntimeStatus::recalibration_required && !calibration_prompted_) {
            calibration_prompted_=true;open_=calibration_page_=true;selected_=8;
            out.opened=out.consume=true;return out;
        }
        if(open_ && calibration_page_ && hand_fit_page_ && y) {out.consume=true;return out;} // Y has no meaning on the hand-fit page
        if(open_ && calibration_page_ && y) {measurements_page_=!measurements_page_;stick_held_=false;out.consume=true;return out;}
        if(y) {open_=!open_;if(!open_) calibration_page_=false;out.opened=open_;out.consume=true;return out;}
        if(!open_) return out;
        out.consume=true;
        if(calibration_page_) {
            if(hand_fit_page_) {
                // Hold the controllers naturally; the sticks nudge the active hand (A switches it); a trigger confirms that hand.
                if(b) {leave_hand_fit();return out;}
                if(a) fit_side_^=1u;
                const auto deadzone=[](float v) {return std::abs(v)<.2f?0.f:v;};
                fit_[fit_side_].nudge(deadzone(in.move_x),deadzone(in.move_y),deadzone(in.turn_x),deadzone(in.turn_y),dt);
                for(unsigned side=0;side<2;++side) if(side?click:left_click) {
                    if(const auto value=fit_[side].confirm()) {
                        palm_current_[side]=*value;out.palm_offset=PalmOffsetChange{side,*value};
                        fit_[side].begin(side,*value); // stays open: the other hand can be fitted next, or this one nudged again
                    }
                }
                return out;
            }
            if(x && !measurements_page_) {
                hand_fit_page_=true;fit_side_=1;stick_held_=false;
                for(unsigned side=0;side<2;++side) fit_[side].begin(side,palm_current_[side]);
                return out;
            }
            if(measurements_page_) {
                if(b) {measurements_page_=false;return out;}
                if(pointer && click) {
                    const double py=pointer->v*560-110;
                    const int row=py<0?-1:static_cast<int>(py/56);
                    if(row>=0 && row<5 && py-row*56<=48)measurement_selected_=static_cast<unsigned>(row);
                }
                const auto magnitude=std::hypot(in.move_x,in.move_y);
                if(magnitude<.3)stick_held_=false;
                if(magnitude>.65 && !stick_held_) {
                    stick_held_=true;
                    if(std::abs(in.move_y)>std::abs(in.move_x))measurement_selected_=(measurement_selected_+(in.move_y>0?4:1))%5;
                    else {
                        constexpr double initial[5]={1.65,.38,.34,.65,.65},low[5]={.9,.15,.15,.25,.25},high[5]={2.2,.65,.8,1,1};
                        auto& value=measurement(measurement_selected_);
                        if(!value)value=initial[measurement_selected_];
                        else {const double next=std::round((*value+(in.move_x>0?.01:-.01))*100)/100;
                            if(next<low[measurement_selected_])value.reset();else value=std::min(next,high[measurement_selected_]);}
                        out.player_measurements=true;
                    }
                }
                if(x && measurement(measurement_selected_)) {measurement(measurement_selected_).reset();out.player_measurements=true;}
                if(a || (click && pointer && pointer->v*560>=438 && pointer->v*560<=480)) {measurements_page_=false;out.calibrate=true;}
                return out;
            }
            if(b) {calibration_page_=false;if(native_calibration_) {open_=native_calibration_=false;}}
            else if(a && calibration_state_!=AvatarCalibrationState::hold_pose) out.calibrate=true;
            return out;
        }
        if(b) {open_=false;return out;}
        int horizontal=0;
        const auto magnitude=std::hypot(in.move_x,in.move_y);
        if(magnitude<.3) stick_held_=false;
        if(magnitude>.65 && !stick_held_) {
            stick_held_=true;
            if(std::abs(in.move_y)>std::abs(in.move_x)) selected_=static_cast<unsigned>((static_cast<int>(selected_)+(in.move_y>0?9:1))%10);
            else horizontal=in.move_x>0?1:-1;
        }
        if(pointer && click) {
            const auto& p=*pointer;
            // Rows mirror settings_panel_image.hpp: 640x560 panel, row i at y=64+36i, 32 px tall.
            const double py=p.v*560-64;const int row=py<0?-1:static_cast<int>(py/36);
            if(row>=0 && row<10 && py-36*row<=32) switch(row) {
                case 0:selected_=0;horizontal=p.u<.5?-1:1;break;
                case 1:selected_=1;temporal_aa_=(temporal_aa_+1)%7;out.temporal_aa=temporal_aa_;break;
                case 2:selected_=2;aa_=!aa_;out.antialiasing=aa_;break;
                case 3:selected_=3;cycle_mirror(1);out.mirror=mirror_;break;
                case 4:selected_=4;cycle_sharpness(1);out.sharpness=sharpness_;break;
                case 5:selected_=5;cycle_hud(1);out.hud_percent=hud_percent_;break;
                case 6:selected_=6;controls_hint_=!controls_hint_;out.controls_hint=controls_hint_;break;
                case 7:selected_=7;cycle_turning(1);out.turning=turning_;break;
                case 8:selected_=8;if(p.u<.5) {calibration_page_=true;out.calibrate=true;} else out.recenter=true;break;
                default:selected_=9;if(p.u<.5) out.test_unlock=true;else open_=false;break; // test unlock (left) | close (right)
            }
        } else if(selected_==0 && horizontal) {
            // Handled below; A does not silently raise resolution.
        } else if(selected_==1 && (horizontal || a)) {temporal_aa_=(temporal_aa_+(horizontal<0?6:1))%7;out.temporal_aa=temporal_aa_;}
        else if(selected_==2 && (horizontal || a)) {aa_=!aa_;out.antialiasing=aa_;}
        else if(selected_==3 && (horizontal || a)) {cycle_mirror(horizontal?horizontal:1);out.mirror=mirror_;}
        else if(selected_==4 && (horizontal || a)) {cycle_sharpness(horizontal?horizontal:1);out.sharpness=sharpness_;}
        else if(selected_==5 && (horizontal || a)) {cycle_hud(horizontal?horizontal:1);out.hud_percent=hud_percent_;}
        else if(selected_==6 && (horizontal || a)) {controls_hint_=!controls_hint_;out.controls_hint=controls_hint_;}
        else if(selected_==7 && (horizontal || a)) {cycle_turning(horizontal?horizontal:1);out.turning=turning_;}
        else if(selected_==8 && a) {calibration_page_=true;out.calibrate=true;}
        else if(selected_==8 && horizontal) {if(horizontal<0) {calibration_page_=true;out.calibrate=true;} else out.recenter=true;}
        else if(selected_==9 && horizontal<0) out.test_unlock=true;
        else if(selected_==9 && a) open_=false;
        if(selected_==0 && horizontal) {
            const auto next=std::clamp(std::round((requested_+horizontal*.1)*10)/10,.5,2.);
            if(next!=requested_) {requested_=next;out.resolution=next;}
        }
        if(from_gameplay) {calibration_page_=false;out.calibrate=false;} // the fit needs the paused menu's still scene
        return out;
    }
};
} // namespace d2vr
