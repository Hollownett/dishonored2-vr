#pragma once
#include <cmath>
#include <cstdint>

namespace d2vr {
class PhysicalDoorController {
public:
    enum class NativeState {Other,Closed,Animated,Opened};
    enum class Pressure {None,Opening,Closing};
    enum class Phase {Idle,Opening,ClosingToZero,WaitingForZero,HandedOff};
    enum class Action {None,BeginNativeOpen,UpdateOpen,ReleaseOpen,CloseOwned,NativeUseClose};
    using AngleToSeconds=float (*)(const void*,float) noexcept;
    struct Mapping {
        AngleToSeconds angle_to_seconds{};
        const void* context{};
        float duration_seconds{};
        bool valid{};
    };
    struct Input {
        std::uint64_t owner_token{};
        std::uint64_t monotonic_tick_ms{};
        float dt_seconds{};
        NativeState native_state{NativeState::Other};
        float current_angle_radians{};
        float target_angle_radians{};
        bool fresh_contact{};
        std::uint64_t contact_identity{};
        Pressure pressure{Pressure::None};
        float inward_speed{};
        float fast_open_speed{};
        float begin_dwell_seconds{};
        float close_dwell_seconds{};
        float normal_slew_seconds_per_second{};
        float closing_slew_seconds_per_second{};
        float angle_deadband_radians{};
        Mapping mapping{};
        float evaluated_seconds{};
        bool native_use_eligible{};
        bool animation_receipt_active{};
        bool close_authorized{};
        bool zero_ack{};
    };
    struct State {
        std::uint64_t owner_token{};
        std::uint64_t contact_identity{};
        std::uint64_t last_tick_ms{};
        float commanded_seconds{};
        float opening_dwell{};
        float close_dwell{};
        Phase phase{Phase::Idle};
        bool active{},has_tick{};
        bool fast_open_handoff{},zero_command_sent{},close_emitted{};
    };
    struct Decision {
        Action action{Action::None};
        float seconds{};
    };
    static Decision step(State& state,const Input& input) noexcept {
        if(!valid_input(input)) {
            const bool was_active=state.active;
            reset(state);
            return {was_active?Action::ReleaseOpen:Action::None,0.f};
        }
        if(state.has_tick && input.monotonic_tick_ms<=state.last_tick_ms)return {};
        if((state.owner_token && state.owner_token!=input.owner_token) ||
           (state.contact_identity && state.contact_identity!=input.contact_identity)) {
            const bool was_active=state.active;
            reset(state);
            if(was_active) {
                elapsed_seconds(state,input);
                return {Action::ReleaseOpen,0.f};
            }
        }
        const auto elapsed=elapsed_seconds(state,input);
        if(!input.fresh_contact || !input.contact_identity) {
            state.opening_dwell=0.f;
            state.close_dwell=0.f;
            state.close_emitted=false;
            state.fast_open_handoff=false;
            if(state.active && state.phase!=Phase::HandedOff) {reset(state);return {Action::ReleaseOpen,0.f};}
            if(state.phase==Phase::HandedOff)reset(state);
            return {};
        }
        if(input.inward_speed<0.f || input.pressure==Pressure::None) {
            state.opening_dwell=0.f;
            state.close_dwell=0.f;
            if(state.active && state.phase!=Phase::HandedOff) {reset(state);return {Action::ReleaseOpen,0.f};}
            return {};
        }
        if(input.native_state==NativeState::Closed) {
            if(state.active && state.phase==Phase::Opening)return {};
            if(input.pressure!=Pressure::Opening || !input.native_use_eligible || state.phase==Phase::HandedOff) {
                state.opening_dwell=0.f;
                state.owner_token=input.owner_token;
                state.contact_identity=input.contact_identity;
                return {};
            }
            state.opening_dwell+=elapsed;
            state.owner_token=input.owner_token;
            state.contact_identity=input.contact_identity;
            if(state.opening_dwell<input.begin_dwell_seconds)return {};
            state.owner_token=input.owner_token;
            state.contact_identity=input.contact_identity;
            state.active=true;
            state.phase=Phase::Opening;
            state.commanded_seconds=input.evaluated_seconds;
            state.close_emitted=false;
            return {Action::BeginNativeOpen,0.f};
        }
        if(input.native_state==NativeState::Animated) {
            if(state.phase==Phase::HandedOff)return {};
            if(!state.active || !input.animation_receipt_active || state.owner_token!=input.owner_token)return {};
            if(input.pressure==Pressure::Opening && input.inward_speed>=input.fast_open_speed && input.fast_open_speed>0.f) {
                state.phase=Phase::HandedOff;
                state.active=false;
                state.fast_open_handoff=true;
                return {Action::ReleaseOpen,0.f};
            }
            if(input.pressure==Pressure::Closing) {
                state.phase=Phase::ClosingToZero;
                state.close_dwell+=elapsed;
                const auto close_target=std::abs(input.target_angle_radians)<=input.angle_deadband_radians?0.f:input.target_angle_radians;
                float desired{};
                if(!map_seconds(input.mapping,close_target,desired)) {reset(state);return {Action::ReleaseOpen,0.f};}
                state.commanded_seconds=approach(state.commanded_seconds,desired,input.closing_slew_seconds_per_second*elapsed);
                if(desired==0.f && state.commanded_seconds==0.f && state.close_dwell>=input.close_dwell_seconds) {
                    state.phase=Phase::WaitingForZero;
                    if(state.zero_command_sent && input.zero_ack && input.close_authorized && !state.close_emitted) {
                        state.phase=Phase::HandedOff;
                        state.active=false;
                        state.close_emitted=true;
                        return {Action::CloseOwned,0.f};
                    }
                    state.zero_command_sent=true;
                }
                return {Action::UpdateOpen,state.commanded_seconds};
            }
            state.close_dwell=0.f;
            state.zero_command_sent=false;
            state.phase=Phase::Opening;
            if(std::abs(input.target_angle_radians-input.current_angle_radians)<=input.angle_deadband_radians)return {};
            float desired{};
            if(!map_seconds(input.mapping,input.target_angle_radians,desired)) {reset(state);return {Action::ReleaseOpen,0.f};}
            state.commanded_seconds=approach(state.commanded_seconds,desired,input.normal_slew_seconds_per_second*elapsed);
            return {Action::UpdateOpen,state.commanded_seconds};
        }
        if(input.native_state==NativeState::Opened) {
            if(state.active)reset(state);
            state.owner_token=input.owner_token;
            state.contact_identity=input.contact_identity;
            if(input.pressure!=Pressure::Closing || !input.native_use_eligible) {state.close_dwell=0.f;return {};}
            if(state.close_emitted)return {};
            state.close_dwell+=elapsed;
            if(state.close_dwell<input.close_dwell_seconds)return {};
            state.close_emitted=true;
            state.phase=Phase::HandedOff;
            return {Action::NativeUseClose,0.f};
        }
        const bool was_active=state.active;
        reset(state);
        return {was_active?Action::ReleaseOpen:Action::None,0.f};
    }
private:
    static bool finite(float value) noexcept {return std::isfinite(value);}
    static bool valid_input(const Input& input) noexcept {
        return input.owner_token && finite(input.dt_seconds) && input.dt_seconds>0.f && input.dt_seconds<=0.1f &&
            finite(input.current_angle_radians) && finite(input.target_angle_radians) && finite(input.inward_speed) &&
            finite(input.fast_open_speed) && input.fast_open_speed>=0.f && finite(input.begin_dwell_seconds) && input.begin_dwell_seconds>=0.f &&
            finite(input.close_dwell_seconds) && input.close_dwell_seconds>=0.f && finite(input.normal_slew_seconds_per_second) && input.normal_slew_seconds_per_second>=0.f &&
            finite(input.closing_slew_seconds_per_second) && input.closing_slew_seconds_per_second>=0.f && finite(input.angle_deadband_radians) && input.angle_deadband_radians>=0.f &&
            finite(input.evaluated_seconds) && input.evaluated_seconds>=0.f;
    }
    static float elapsed_seconds(State& state,const Input& input) noexcept {
        float elapsed=input.dt_seconds;
        if(state.has_tick && input.monotonic_tick_ms>state.last_tick_ms) {
            elapsed=static_cast<float>(input.monotonic_tick_ms-state.last_tick_ms)*0.001f;
            if(elapsed>0.1f)elapsed=0.1f;
        }
        state.last_tick_ms=input.monotonic_tick_ms;
        state.has_tick=true;
        return elapsed;
    }
    static bool mapping_valid(const Mapping& mapping) noexcept {
        return mapping.valid && mapping.angle_to_seconds && mapping.context && finite(mapping.duration_seconds) && mapping.duration_seconds>0.f;
    }
    static bool map_seconds(const Mapping& mapping,float angle,float& seconds) noexcept {
        if(!mapping_valid(mapping))return false;
        seconds=mapping.angle_to_seconds(mapping.context,angle);
        return finite(seconds) && seconds>=0.f && seconds<mapping.duration_seconds;
    }
    static float approach(float current,float target,float maximum_delta) noexcept {
        if(target>current)return current+((target-current)<maximum_delta?(target-current):maximum_delta);
        if(target<current)return current-((current-target)<maximum_delta?(current-target):maximum_delta);
        return target;
    }
    static void reset(State& state) noexcept {
        const auto last_tick_ms=state.last_tick_ms;
        const bool has_tick=state.has_tick;
        state={};
        state.last_tick_ms=last_tick_ms;
        state.has_tick=has_tick;
    }
};
}
