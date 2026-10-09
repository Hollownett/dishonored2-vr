#pragma once
#include "tracked_pose.hpp"
#include "controller_input.hpp"
#include "turn_control.hpp"
#include "vr_preferences.hpp"
#include "avatar_calibration.hpp"
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
    bool previous_y_{},previous_a_{},previous_b_{},previous_trigger_{},stick_held_{};
    bool controls_valid_{};
    bool open_{};
    bool calibration_page_{};
    bool calibration_prompted_{};
    AvatarCalibrationState calibration_state_{AvatarCalibrationState::idle};
    double calibration_progress_{};
    AvatarFitRuntimeStatus calibration_runtime_{AvatarFitRuntimeStatus::unmeasured};
    unsigned selected_{};
    double requested_=1;
    bool aa_{};
    bool temporal_aa_{false};
    bool aim_reticle_{true};
    bool controls_hint_{true};
    TurnControl::Config turning_;
    MirrorMode mirror_{MirrorMode::both_eyes};
    double sharpness_{.5};
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
    struct Changes {std::optional<double> resolution;std::optional<bool> antialiasing;std::optional<bool> temporal_aa;std::optional<TurnControl::Config> turning;
                   std::optional<MirrorMode> mirror;std::optional<double> sharpness;std::optional<bool> aim_reticle;std::optional<bool> controls_hint;
                   bool recenter{},consume{},opened{},calibrate{};};
    void initialize(double scale,bool aa,TurnControl::Config turning={},MirrorMode mirror=MirrorMode::both_eyes,double sharpness=.5,bool temporal_aa=false,
        bool aim_reticle=true,bool controls_hint=true) {
        requested_=scale;aa_=aa;turning_=turning;mirror_=mirror;sharpness_=sharpness;temporal_aa_=temporal_aa;
        aim_reticle_=aim_reticle;controls_hint_=controls_hint;
    }
    bool open() const noexcept {return open_;}
    bool calibration_page() const noexcept {return calibration_page_;}
    AvatarCalibrationState calibration_state() const noexcept {return calibration_state_;}
    double calibration_progress() const noexcept {return calibration_progress_;}
    AvatarFitRuntimeStatus calibration_runtime() const noexcept {return calibration_runtime_;}
    void calibration_runtime(AvatarFitRuntimeStatus status) noexcept {
        calibration_runtime_=status;
        if(status==AvatarFitRuntimeStatus::active)calibration_prompted_=false;
    }
    const wchar_t* calibration_runtime_text() const noexcept {
        switch(calibration_runtime_) {
            case AvatarFitRuntimeStatus::verifying:return L"Checking avatar - fit suspended";
            case AvatarFitRuntimeStatus::active:return L"Measured fit active";
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
    bool temporal_aa() const noexcept {return temporal_aa_;}
    bool aim_reticle() const noexcept {return aim_reticle_;}
    bool controls_hint() const noexcept {return controls_hint_;}
    TurnControl::Config turning() const noexcept {return turning_;}
    MirrorMode mirror() const noexcept {return mirror_;}
    double sharpness() const noexcept {return sharpness_;}
    const wchar_t* temporal_aa_text() const noexcept {return temporal_aa_?L"Temporal AA: On":L"Temporal AA: Off";}
    const wchar_t* aim_reticle_text() const noexcept {return aim_reticle_?L"Aim reticle: On":L"Aim reticle: Off";}
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
    void suspend() noexcept {open_=false;calibration_page_=false;controls_valid_=false;stick_held_=false;}
    // A proved owner/resource/reference change is distinct from focus loss or
    // deliberate dismissal. Only this notification rearms the required prompt.
    void calibration_identity_changed() noexcept {
        suspend();calibration_prompted_=false;
        calibration_runtime_=AvatarFitRuntimeStatus::recalibration_required;
    }
    Changes update(const ControllerInput& in,bool gameplay,bool focused,std::optional<PanelPointer> pointer={},bool settings_available=true) {
        Changes out{};
        if(!focused || !in.active || gameplay || !settings_available) {suspend();return out;}
        const bool trigger=in.right_trigger>.55f;
        if(!controls_valid_) {
            // Returning to focus never interprets held buttons as fresh clicks.
            previous_y_=in.y;previous_a_=in.a;previous_b_=in.b;previous_trigger_=trigger;
            stick_held_=std::hypot(in.move_x,in.move_y)>.35;controls_valid_=true;return out;
        }
        const bool y=in.y&&!previous_y_,a=in.a&&!previous_a_,b=in.b&&!previous_b_,click=trigger&&!previous_trigger_;
        previous_y_=in.y;previous_a_=in.a;previous_b_=in.b;previous_trigger_=trigger;
        if(calibration_runtime_==AvatarFitRuntimeStatus::recalibration_required && !calibration_prompted_) {
            calibration_prompted_=true;open_=calibration_page_=true;selected_=8;
            out.opened=out.consume=true;return out;
        }
        if(y) {open_=!open_;if(!open_) calibration_page_=false;out.opened=open_;out.consume=true;return out;}
        if(!open_) return out;
        out.consume=true;
        if(calibration_page_) {
            if(b) calibration_page_=false;
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
            if(p.v>=.23 && p.v<=.28) {
                selected_=0;if(p.u<.3) horizontal=-1;else if(p.u>.7) horizontal=1;
            } else if(p.v>=.30 && p.v<=.35) {selected_=1;temporal_aa_=!temporal_aa_;out.temporal_aa=temporal_aa_;}
            else if(p.v>=.36 && p.v<=.41) {selected_=2;aa_=!aa_;out.antialiasing=aa_;}
            else if(p.v>=.43 && p.v<=.47) {selected_=3;cycle_mirror(1);out.mirror=mirror_;}
            else if(p.v>=.49 && p.v<=.54) {selected_=4;cycle_sharpness(1);out.sharpness=sharpness_;}
            else if(p.v>=.56 && p.v<=.60) {selected_=5;aim_reticle_=!aim_reticle_;out.aim_reticle=aim_reticle_;}
            else if(p.v>=.62 && p.v<=.67) {selected_=6;controls_hint_=!controls_hint_;out.controls_hint=controls_hint_;}
            else if(p.v>=.69 && p.v<=.73) {selected_=7;cycle_turning(1);out.turning=turning_;}
            else if(p.v>=.75 && p.v<=.79) {selected_=8;if(p.u<.5) {calibration_page_=true;out.calibrate=true;} else out.recenter=true;}
            else if(p.v>=.81 && p.v<=.86) {open_=false;}
        } else if(selected_==0 && horizontal) {
            // Handled below; A does not silently raise resolution.
        } else if(selected_==1 && (horizontal || a)) {temporal_aa_=!temporal_aa_;out.temporal_aa=temporal_aa_;}
        else if(selected_==2 && (horizontal || a)) {aa_=!aa_;out.antialiasing=aa_;}
        else if(selected_==3 && (horizontal || a)) {cycle_mirror(horizontal?horizontal:1);out.mirror=mirror_;}
        else if(selected_==4 && (horizontal || a)) {cycle_sharpness(horizontal?horizontal:1);out.sharpness=sharpness_;}
        else if(selected_==5 && (horizontal || a)) {aim_reticle_=!aim_reticle_;out.aim_reticle=aim_reticle_;}
        else if(selected_==6 && (horizontal || a)) {controls_hint_=!controls_hint_;out.controls_hint=controls_hint_;}
        else if(selected_==7 && (horizontal || a)) {cycle_turning(horizontal?horizontal:1);out.turning=turning_;}
        else if(selected_==8 && a) {calibration_page_=true;out.calibrate=true;}
        else if(selected_==8 && horizontal) {if(horizontal<0) {calibration_page_=true;out.calibrate=true;} else out.recenter=true;}
        else if(selected_==9 && a) open_=false;
        if(selected_==0 && horizontal) {
            const auto next=std::clamp(std::round((requested_+horizontal*.1)*10)/10,.5,2.);
            if(next!=requested_) {requested_=next;out.resolution=next;}
        }
        return out;
    }
};
} // namespace d2vr
