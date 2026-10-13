#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace d2vr {
enum class AvatarCalibrationState {idle,hold_pose,grips_released,pose_moving,tracking_lost,pose_invalid,dimensions_unsupported,ready,reference_changed,rig_changed,cancelled,release_grips};
enum class AvatarFitRuntimeStatus {unmeasured,verifying,active,recalibration_required};
// The guided capture is a single T/A pose; `shoulders` remains for callers only.
enum class AvatarCalibrationStage {shoulders,t_pose};
struct AvatarPlayerMeasurements {
    std::optional<double> standing_eye_height_metres,shoulder_span_metres,torso_width_metres;
    // Shoulder joint centre to controller palm in a straight arm pose.
    std::array<std::optional<double>,2> arm_reach_metres{};
};
struct NativeAvatarMeasurements {
    // Measure from the verified native bind palms in a straight-arm T pose,
    // converting native model units to metres before calling begin().
    double palm_span_metres{},shoulder_span_metres{};
};
struct AvatarCalibrationResult {
    std::uint64_t reference_epoch{};
    // palm_span is the equivalent level-T span: shoulder span plus both reaches.
    double palm_span_metres{},shoulder_span_metres{},eye_to_shoulder_metres{},arm_scale{};
    std::optional<double> standing_eye_height_metres;
    // Shoulder span is the native avatar's unless entered; reaches are always
    // measured from the HMD-derived shoulders; torso width only when entered.
    bool shoulder_span_measured{},arm_lengths_measured{},torso_width_measured{};
    std::optional<double> torso_width_metres;
    std::array<std::optional<double>,2> arm_reach_metres{};
};
inline bool same_avatar_calibration(const AvatarCalibrationResult& a,const AvatarCalibrationResult& b) {
    return a.reference_epoch==b.reference_epoch && a.palm_span_metres==b.palm_span_metres &&
        a.shoulder_span_metres==b.shoulder_span_metres && a.eye_to_shoulder_metres==b.eye_to_shoulder_metres &&
        a.arm_scale==b.arm_scale && a.standing_eye_height_metres==b.standing_eye_height_metres &&
        a.shoulder_span_measured==b.shoulder_span_measured && a.arm_lengths_measured==b.arm_lengths_measured &&
        a.torso_width_measured==b.torso_width_measured && a.torso_width_metres==b.torso_width_metres &&
        a.arm_reach_metres==b.arm_reach_metres;
}
// Explicit wizard capture, independent of locomotion/recenter. LOCAL origin
// has no implied floor: height is available only with a measured floor datum.
//
// One forgiving pose: arms out to the sides (level T or relaxed A pose), both
// grips squeezed, one second roughly still. Grips lie on the shoulder line only
// when the arms are level, so the hold never puts the shoulders lower than the
// adult proportion of eye height. Each arm's reach is measured from that
// HMD-derived shoulder to its grip, which does not depend on arm droop.
class AvatarCalibration {
    static constexpr std::uint64_t hold_ms=1000,grace_ms=400;
    static constexpr unsigned hold_samples=20;
    // Eye to shoulder joint is about 0.13 of standing eye height (native rig:
    // camera_jnt 1.70 m, arm_jnt 0.213 m below); 0.22 m when height is unknown.
    static constexpr double eye_to_shoulder_ratio=.13,default_eye_to_shoulder=.22,shoulder_band=.04;
    static constexpr double shoulder_behind_eye=.08;
    AvatarCalibrationState state_{AvatarCalibrationState::idle};
    NativeAvatarMeasurements native_{};
    std::optional<double> floor_y_;
    std::optional<double> measured_shoulder_span_;
    AvatarPlayerMeasurements player_{};
    std::uint64_t epoch_{},start_tick_{},last_tick_{};
    unsigned samples_{};
    Vec3 first_head_{},first_hands_[2]{},first_right_{},mean_head_{},mean_hands_[2]{};
    double first_span_{},height_sum_{};
    std::optional<AvatarCalibrationResult> result_;
    std::optional<AvatarCalibrationResult> previous_result_;
    // The player's body survives avatar and reference changes; restore() refits it.
    std::optional<AvatarCalibrationResult> retained_;
    bool capturing_{};
    void restart_window() noexcept {samples_=0;height_sum_=0;start_tick_=last_tick_=0;}
    // A brief fault (controller at the tracking edge, a wobble) pauses the
    // hold; only a fault outlasting the grace period restarts it.
    AvatarCalibrationState fault(AvatarCalibrationState state,std::uint64_t tick) noexcept {
        if(!samples_ || tick<last_tick_ || tick-last_tick_>grace_ms) restart_window();
        return state_=state;
    }
    void forget(AvatarCalibrationState state) noexcept {const auto keep=retained_;clear();retained_=keep;state_=state;}
    static bool valid_native(const NativeAvatarMeasurements& native) noexcept {
        return std::isfinite(native.palm_span_metres) && native.palm_span_metres>=.8 && native.palm_span_metres<=2.5 &&
            std::isfinite(native.shoulder_span_metres) && native.shoulder_span_metres>=.15 && native.shoulder_span_metres<=.65;
    }
    static bool fit_span(AvatarCalibrationResult& fit,const NativeAvatarMeasurements& native) noexcept {
        if(!fit.arm_reach_metres[0] || !fit.arm_reach_metres[1]) return false;
        fit.palm_span_metres=fit.shoulder_span_metres+*fit.arm_reach_metres[0]+*fit.arm_reach_metres[1];
        fit.arm_scale=fit.palm_span_metres/native.palm_span_metres;
        return fit.arm_scale>=.7 && fit.arm_scale<=1.45 && fit.palm_span_metres>=.9 && fit.palm_span_metres<=2.4;
    }
    AvatarCalibrationState finish() {
        const auto midpoint=(mean_hands_[0]+mean_hands_[1])*.5;
        Vec3 lateral=mean_hands_[1]-mean_hands_[0];lateral.y=0;
        const auto right=unit(lateral);const Vec3 forward{right.z,0,-right.x};
        AvatarCalibrationResult fit{};fit.reference_epoch=epoch_;
        fit.standing_eye_height_metres=player_.standing_eye_height_metres?player_.standing_eye_height_metres:
            floor_y_?std::optional<double>(height_sum_/samples_):std::nullopt;
        const double nominal=fit.standing_eye_height_metres?eye_to_shoulder_ratio* *fit.standing_eye_height_metres:default_eye_to_shoulder;
        fit.eye_to_shoulder_metres=std::clamp(mean_head_.y-midpoint.y,nominal-shoulder_band,nominal);
        fit.shoulder_span_metres=measured_shoulder_span_.value_or(native_.shoulder_span_metres);
        fit.shoulder_span_measured=measured_shoulder_span_.has_value();
        fit.torso_width_metres=player_.torso_width_metres;fit.torso_width_measured=player_.torso_width_metres.has_value();
        for(unsigned side=0;side<2;++side) {
            const auto shoulder=mean_head_+Vec3{0,-fit.eye_to_shoulder_metres,0}-forward*shoulder_behind_eye+
                right*((side?.5:-.5)*fit.shoulder_span_metres);
            const double reach=length(mean_hands_[side]-shoulder);
            const auto& entered=player_.arm_reach_metres[side];
            if(!std::isfinite(reach) || reach<.25 || reach>1. || (entered && std::abs(*entered-reach)>.08)) {
                restart_window();return state_=AvatarCalibrationState::dimensions_unsupported;
            }
            fit.arm_reach_metres[side]=entered?*entered:reach;
        }
        fit.arm_lengths_measured=true;
        if(!fit_span(fit,native_)) {restart_window();return state_=AvatarCalibrationState::dimensions_unsupported;}
        result_=retained_=fit;capturing_=false;return state_=AvatarCalibrationState::ready;
    }
public:
    AvatarCalibrationState state() const noexcept {return state_;}
    AvatarCalibrationStage stage() const noexcept {return AvatarCalibrationStage::t_pose;}
    AvatarPlayerMeasurements measurements() const {return player_;}
    // Both the elapsed stable window and actual samples gate completion.
    double progress() const noexcept {
        if(state_==AvatarCalibrationState::ready) return 1.;
        return capturing_ && samples_?std::min(1.,std::min(static_cast<double>(last_tick_-start_tick_)/hold_ms,
            samples_/static_cast<double>(hold_samples))):0.;
    }
    void clear() noexcept {state_=AvatarCalibrationState::idle;epoch_=0;result_.reset();previous_result_.reset();retained_.reset();
        capturing_=false;player_={};measured_shoulder_span_.reset();restart_window();}
    void invalidate_rig() noexcept {
        forget(state_!=AvatarCalibrationState::idle?AvatarCalibrationState::rig_changed:AvatarCalibrationState::idle);
    }
    AvatarCalibrationState synchronize(std::uint64_t reference_epoch,bool capture_visible) noexcept {
        // Check this every frame, including while the wizard is closed.
        if(epoch_ && (!reference_epoch || reference_epoch!=epoch_)) {
            forget(AvatarCalibrationState::reference_changed);return state_;
        }
        if(capturing_ && !capture_visible) {
            capturing_=false;restart_window();
            state_=result_?AvatarCalibrationState::ready:AvatarCalibrationState::cancelled;
        }
        return state_;
    }
    void begin(std::uint64_t reference_epoch,NativeAvatarMeasurements native,std::optional<double> measured_floor_y={},
        std::optional<double> measured_shoulder_span={}) {
        if(!reference_epoch || !valid_native(native) || (measured_floor_y && !std::isfinite(*measured_floor_y)) ||
            (measured_shoulder_span && (!std::isfinite(*measured_shoulder_span) || *measured_shoulder_span<.15 || *measured_shoulder_span>.65)))
            throw std::invalid_argument("Invalid avatar calibration measurements");
        // A same-space retry must not destroy the previous measured fit if
        // the player cancels or loses focus before the new capture completes.
        const auto previous=result(reference_epoch);const auto keep=retained_;
        clear();result_=previous;previous_result_=previous;retained_=keep;epoch_=reference_epoch;native_=native;
        floor_y_=measured_floor_y;measured_shoulder_span_=measured_shoulder_span;player_.shoulder_span_metres=measured_shoulder_span;
        state_=AvatarCalibrationState::grips_released;capturing_=true;
    }
    void begin(std::uint64_t reference_epoch,NativeAvatarMeasurements native,std::optional<double> measured_floor_y,
        const AvatarPlayerMeasurements& player) {
        const auto in_range=[](const std::optional<double>& value,double low,double high) {
            return !value || (std::isfinite(*value) && *value>=low && *value<=high);
        };
        if(!in_range(player.standing_eye_height_metres,.9,2.2) || !in_range(player.shoulder_span_metres,.15,.65) ||
            !in_range(player.torso_width_metres,.15,.8) || !in_range(player.arm_reach_metres[0],.25,1.) ||
            !in_range(player.arm_reach_metres[1],.25,1.)) throw std::invalid_argument("Invalid player body measurements");
        begin(reference_epoch,native,measured_floor_y,player.shoulder_span_metres);
        player_=player;
    }
    // Refit the retained player body to a new avatar or reference space
    // without another pose. No-op while capturing or when a fit is current.
    bool restore(std::uint64_t reference_epoch,NativeAvatarMeasurements native) {
        if(!retained_ || !reference_epoch || capturing_ || result(reference_epoch) || !valid_native(native)) return false;
        auto fit=*retained_;fit.reference_epoch=reference_epoch;
        if(!fit.shoulder_span_measured) fit.shoulder_span_metres=native.shoulder_span_metres;
        if(!fit_span(fit,native)) return false;
        forget(AvatarCalibrationState::ready);epoch_=reference_epoch;native_=native;result_=fit;return true;
    }
    AvatarCalibrationState observe(const TrackedPose& head,const std::array<TrackedPose,2>& hands,unsigned valid_hands,
        bool head_valid,bool both_grips_held,std::uint64_t tick,std::uint64_t reference_epoch) {
        synchronize(reference_epoch,true);
        if(!capturing_) return state_;
        if(!head_valid || valid_hands!=3 || !finite(head.position) || !finite(hands[0].position) || !finite(hands[1].position))
            return fault(AvatarCalibrationState::tracking_lost,tick);
        Quaternion orientation{};
        try {orientation=normalized(head.orientation);for(const auto& hand:hands) (void)normalized(hand.orientation);}
        catch(const std::invalid_argument&) {return fault(AvatarCalibrationState::tracking_lost,tick);}
        if(!both_grips_held) {restart_window();return state_=AvatarCalibrationState::grips_released;}
        // The body's side axis comes from the hands; the head need only face roughly forward.
        Vec3 lateral=hands[1].position-hands[0].position;lateral.y=0;
        const auto head_right=rotated(orientation,{1,0,0});const Vec3 facing{head_right.x,0,head_right.z};
        if(length(lateral)<.5 || length(facing)<.5) return fault(AvatarCalibrationState::pose_invalid,tick);
        const auto right=unit(lateral);const Vec3 forward{right.z,0,-right.x};
        const auto midpoint=(hands[0].position+hands[1].position)*.5;
        const auto span=length(hands[1].position-hands[0].position),drop=head.position.y-midpoint.y;
        const auto offset=midpoint-head.position;
        const auto height=floor_y_?head.position.y-*floor_y_:0;
        // Level T to relaxed A pose (about 45 degrees down). Uneven hands and a
        // slight lean are measured per arm, not rejected.
        if(span<.9 || span>2.4 || drop<.05 || drop>.8 || std::abs(hands[1].position.y-hands[0].position.y)>.15 ||
            dot(unit(facing),right)<.8 || std::abs(dot(offset,right))>.15 || std::abs(dot(offset,forward))>.35 ||
            (floor_y_ && (height<.9 || height>2.2))) return fault(AvatarCalibrationState::pose_invalid,tick);
        // Stability about the mean, and a bound on the whole hold so slow
        // deliberate movement cannot hide inside a moving average.
        if(samples_ && (tick<last_tick_ || tick-last_tick_>grace_ms || length(head.position-mean_head_)>.05 ||
            length(hands[0].position-mean_hands_[0])>.08 || length(hands[1].position-mean_hands_[1])>.08 ||
            length(head.position-first_head_)>.08 || length(hands[0].position-first_hands_[0])>.12 ||
            length(hands[1].position-first_hands_[1])>.12 || dot(right,first_right_)<.94 || std::abs(span-first_span_)>.08))
            return fault(AvatarCalibrationState::pose_moving,tick);
        // Multiple callbacks in one millisecond do not add samples or restart a hold.
        if(samples_ && tick==last_tick_) return state_=AvatarCalibrationState::hold_pose;
        if(!samples_) {
            start_tick_=tick;first_head_=head.position;first_hands_[0]=hands[0].position;first_hands_[1]=hands[1].position;
            first_right_=right;first_span_=span;mean_head_={};mean_hands_[0]=mean_hands_[1]={};
        }
        const double weight=1./(samples_+1);
        mean_head_=mean_head_+(head.position-mean_head_)*weight;
        for(unsigned side=0;side<2;++side) mean_hands_[side]=mean_hands_[side]+(hands[side].position-mean_hands_[side])*weight;
        last_tick_=tick;++samples_;height_sum_+=height;state_=AvatarCalibrationState::hold_pose;
        if(samples_<hold_samples || tick-start_tick_<hold_ms) return state_;
        return finish();
    }
    std::optional<AvatarCalibrationResult> result(std::uint64_t reference_epoch) const {
        return reference_epoch==epoch_?result_:std::nullopt;
    }
    // The native lane refused this fit: return to the last accepted one and
    // never restore the refused body automatically.
    void reject_dimensions() {result_=retained_=previous_result_;capturing_=false;restart_window();state_=AvatarCalibrationState::dimensions_unsupported;}
};
} // namespace d2vr
