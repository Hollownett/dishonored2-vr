#pragma once
#include "tracked_pose.hpp"
#include <optional>
#include <cstdint>

namespace d2vr {
enum class AvatarCalibrationState {idle,hold_pose,grips_released,pose_moving,tracking_lost,pose_invalid,dimensions_unsupported,ready,reference_changed,rig_changed,cancelled};
enum class AvatarFitRuntimeStatus {unmeasured,verifying,active,recalibration_required};
struct NativeAvatarMeasurements {
    // Measure from the verified native bind palms in a straight-arm T pose,
    // converting native model units to metres before calling begin().
    double palm_span_metres{},shoulder_span_metres{};
};
struct AvatarCalibrationResult {
    std::uint64_t reference_epoch{};
    double palm_span_metres{},shoulder_span_metres{},eye_to_shoulder_metres{},arm_scale{};
    std::optional<double> standing_eye_height_metres;
};
// Explicit wizard capture, independent of locomotion/recenter. LOCAL origin
// has no implied floor: height is available only with a measured floor datum.
class AvatarCalibration {
    AvatarCalibrationState state_{AvatarCalibrationState::idle};
    NativeAvatarMeasurements native_{};
    std::optional<double> floor_y_;
    std::uint64_t epoch_{},start_tick_{},last_tick_{};
    unsigned samples_{};
    TrackedPose first_head_{};
    std::array<TrackedPose,2> first_hands_{};
    double span_sum_{},drop_sum_{},height_sum_{};
    std::optional<AvatarCalibrationResult> result_;
    bool capturing_{};
    void restart_window() noexcept {samples_=0;span_sum_=drop_sum_=height_sum_=0;start_tick_=last_tick_=0;}
public:
    AvatarCalibrationState state() const noexcept {return state_;}
    // Both the elapsed stable window and actual samples gate completion.
    double progress() const noexcept {
        if(state_==AvatarCalibrationState::ready) return 1.;
        return capturing_ && samples_?std::min(1.,std::min(static_cast<double>(last_tick_-start_tick_)/1000.,samples_/20.)):0.;
    }
    void clear() noexcept {state_=AvatarCalibrationState::idle;epoch_=0;result_.reset();capturing_=false;restart_window();}
    void invalidate_rig() noexcept {
        const bool started=state_!=AvatarCalibrationState::idle;
        clear();if(started) state_=AvatarCalibrationState::rig_changed;
    }
    AvatarCalibrationState synchronize(std::uint64_t reference_epoch,bool capture_visible) noexcept {
        // Check this every frame, including while the wizard is closed.
        if(epoch_ && (!reference_epoch || reference_epoch!=epoch_)) {
            clear();return state_=AvatarCalibrationState::reference_changed;
        }
        if(capturing_ && !capture_visible) {
            capturing_=false;restart_window();
            state_=result_?AvatarCalibrationState::ready:AvatarCalibrationState::cancelled;
        }
        return state_;
    }
    void begin(std::uint64_t reference_epoch,NativeAvatarMeasurements native,std::optional<double> measured_floor_y={}) {
        if(!reference_epoch || !std::isfinite(native.palm_span_metres) || native.palm_span_metres<.8 || native.palm_span_metres>2.5 ||
            !std::isfinite(native.shoulder_span_metres) || native.shoulder_span_metres<.15 || native.shoulder_span_metres>.65 ||
            (measured_floor_y && !std::isfinite(*measured_floor_y))) throw std::invalid_argument("Invalid avatar calibration measurements");
        // A same-space retry must not destroy the previous measured fit if
        // the player cancels or loses focus before the new capture completes.
        const auto previous=result(reference_epoch);
        clear();result_=previous;epoch_=reference_epoch;native_=native;floor_y_=measured_floor_y;
        state_=AvatarCalibrationState::grips_released;capturing_=true;
    }
    AvatarCalibrationState observe(const TrackedPose& head,const std::array<TrackedPose,2>& hands,unsigned valid_hands,
        bool head_valid,bool both_grips_held,std::uint64_t tick,std::uint64_t reference_epoch) {
        synchronize(reference_epoch,true);
        if(!capturing_) return state_;
        if(!head_valid || valid_hands!=3 || !finite(head.position) || !finite(hands[0].position) || !finite(hands[1].position)) {
            restart_window();return state_=AvatarCalibrationState::tracking_lost;
        }
        Quaternion orientation{};
        try {orientation=normalized(head.orientation);for(const auto& hand:hands) (void)normalized(hand.orientation);}
        catch(const std::invalid_argument&) {restart_window();return state_=AvatarCalibrationState::tracking_lost;}
        if(!both_grips_held) {restart_window();return state_=AvatarCalibrationState::grips_released;}
        const auto head_right=rotated(orientation,{1,0,0});
        Vec3 right{head_right.x,0,head_right.z};
        if(length(right)<.8) {restart_window();return state_=AvatarCalibrationState::pose_invalid;}
        right=unit(right);const Vec3 forward{-right.z,0,right.x};
        const auto midpoint=(hands[0].position+hands[1].position)*.5;
        const auto span=length(hands[1].position-hands[0].position),drop=head.position.y-midpoint.y;
        const auto offset=midpoint-head.position;
        const auto height=floor_y_?head.position.y-*floor_y_:0;
        if(span<.9 || span>2.4 || drop<.05 || drop>.4 || std::abs(hands[1].position.y-hands[0].position.y)>.06 ||
            dot(hands[1].position-hands[0].position,right)<span*.95 || std::abs(dot(offset,right))>.08 ||
            std::abs(dot(offset,forward))>.25 || (floor_y_ && (height<.9 || height>2.2))) {
            restart_window();return state_=AvatarCalibrationState::pose_invalid;
        }
        if(samples_ && (tick<=last_tick_ || tick-last_tick_>200 || length(head.position-first_head_.position)>.015 ||
            length(hands[0].position-first_hands_[0].position)>.025 || length(hands[1].position-first_hands_[1].position)>.025)) {
            restart_window();return state_=AvatarCalibrationState::pose_moving;
        }
        if(!samples_) {start_tick_=tick;first_head_=head;first_hands_=hands;}
        last_tick_=tick;++samples_;span_sum_+=span;drop_sum_+=drop;height_sum_+=height;state_=AvatarCalibrationState::hold_pose;
        if(samples_<20 || tick-start_tick_<1000) return state_;
        const auto measured_span=span_sum_/samples_,scale=measured_span/native_.palm_span_metres;
        if(scale<.7 || scale>1.45) {restart_window();return state_=AvatarCalibrationState::dimensions_unsupported;}
        result_=AvatarCalibrationResult{epoch_,measured_span,native_.shoulder_span_metres*scale,drop_sum_/samples_,scale,
            floor_y_?std::optional<double>(height_sum_/samples_):std::nullopt};
        capturing_=false;return state_=AvatarCalibrationState::ready;
    }
    std::optional<AvatarCalibrationResult> result(std::uint64_t reference_epoch) const {
        return reference_epoch==epoch_?result_:std::nullopt;
    }
};
} // namespace d2vr
