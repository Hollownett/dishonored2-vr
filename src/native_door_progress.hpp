#pragma once
#include "native_door_event_admission.hpp"
#include <cmath>
#include <cstdint>

namespace d2vr {
class NativeDoorProgress {
public:
    using Request=NativeDoorEventAdmission::Request;
    using EventStatus=NativeDoorEventAdmission::Status;
    using AnimationStatus=NativeDoorAnimation::Status;
    enum class Result {Wait,Ready,Expired};
    struct State {
        Request acknowledged{};
        Request pending{};
        std::uint64_t event_epoch{};
        std::uint64_t acknowledged_input_generation{};
        std::uint64_t acknowledged_revision{};
        std::uint64_t pending_since_ms{};
        std::uint64_t last_tick_ms{};
        bool acknowledged_valid{};
        bool pending_valid{};
        bool has_tick{};
        bool begun{};
        bool close_handed_off{};
    };

    static bool begin(State& state,const Request& request,std::uint64_t now_ms) noexcept {
        if(state.begun || !valid_request(request))return false;
        state.pending=request;
        state.pending_since_ms=now_ms;
        state.pending_valid=true;
        state.begun=true;
        return true;
    }

    static bool queue(State& state,const Request& request,std::uint64_t now_ms) noexcept {
        if(!state.begun || !state.acknowledged_valid || state.pending_valid || !valid_request(request) ||
           !same_identity(state.acknowledged,request) || same(state.acknowledged,request))return false;
        state.pending=request;
        state.pending_since_ms=now_ms;
        state.pending_valid=true;
        return true;
    }

    static void mark_handoff(State& state) noexcept {state.close_handed_off=true;}

    static Result poll(State& state,const EventStatus& event_status,const AnimationStatus& acknowledged_status,
                       const AnimationStatus& pending_status,std::uint64_t now_ms,
                       std::uint64_t initial_timeout_ms,std::uint64_t pending_timeout_ms) noexcept {
        if(!state.begun)return Result::Expired;
        if(state.has_tick && now_ms==state.last_tick_ms)return Result::Wait;
        if(state.has_tick && now_ms<state.last_tick_ms)return expire(state);
        state.last_tick_ms=now_ms;
        state.has_tick=true;
        if(now_ms<state.pending_since_ms)return expire(state);
        if(!state.event_epoch) {
            if(event_status.epoch)state.event_epoch=event_status.epoch;
        } else if(event_status.epoch && event_status.epoch!=state.event_epoch) {
            return expire(state);
        } else if(state.event_epoch && !event_status.epoch) {
            return expire(state);
        }
        if(!state.event_epoch && now_ms-state.pending_since_ms>=initial_timeout_ms)return expire(state);
        if(!state.pending_valid) {
            if(!state.acknowledged_valid)return expire(state);
            if(owned_status(acknowledged_status,state.acknowledged_input_generation,state.acknowledged_revision))return Result::Ready;
            if(state.close_handed_off)return Result::Ready;
            return expire(state);
        }
        if(now_ms-state.pending_since_ms>= (state.acknowledged_valid?pending_timeout_ms:initial_timeout_ms))return expire(state);
        if(event_status.epoch && event_status.epoch!=state.event_epoch)return expire(state);
        if(!state.event_epoch || event_status.stage!=NativeDoorEventAdmission::Stage::Emitted)return Result::Wait;
        if(!owned_status(pending_status,0,0)) {
            if(state.acknowledged_valid && pending_status.published && pending_status.input_owned &&
               pending_status.input_generation==state.acknowledged_input_generation &&
               pending_status.desired_revision>state.acknowledged_revision)return Result::Wait;
            if(!state.acknowledged_valid)return Result::Wait;
            if(owned_status(acknowledged_status,state.acknowledged_input_generation,state.acknowledged_revision))return Result::Wait;
            return expire(state);
        }
        if(state.acknowledged_valid) {
            if(pending_status.input_generation!=state.acknowledged_input_generation) {
                if(!state.close_handed_off || !state.pending.operation.close_authorized)return expire(state);
            } else if(pending_status.evaluated_revision<=state.acknowledged_revision) {
                return Result::Wait;
            }
        }
        if(state.acknowledged_valid && !owned_status(acknowledged_status,state.acknowledged_input_generation,state.acknowledged_revision) &&
           pending_status.input_generation!=state.acknowledged_input_generation &&
           (!state.close_handed_off || !state.pending.operation.close_authorized))return expire(state);
        state.acknowledged=state.pending;
        state.acknowledged_valid=true;
        state.acknowledged_input_generation=pending_status.input_generation;
        state.acknowledged_revision=pending_status.evaluated_revision;
        state.pending={};
        state.pending_valid=false;
        state.pending_since_ms=now_ms;
        return Result::Ready;
    }

private:
    static bool valid_request(const Request& request) noexcept {
        const auto& operation=request.operation;
        return operation.ownership && operation.world && operation.player_generation && operation.part_generation && operation.player && operation.part &&
            operation.animation && operation.animation<=UINTPTR_MAX-0x338 && operation.container==operation.animation+0x338 && operation.binding &&
            std::isfinite(operation.desired_seconds) && operation.desired_seconds>=0.f && operation.opening_clip!=0xff && operation.closing_clip!=0xff &&
            !operation.linked && request.game && request.manager && request.component && request.door && request.component_generation &&
            request.door_spawn && request.player_spawn && request.part_spawn;
    }
    static bool same_operation(const NativeDoorAnimation::Operation& first,const NativeDoorAnimation::Operation& second) noexcept {
        return first.ownership==second.ownership && first.world==second.world && first.player_generation==second.player_generation &&
            first.part_generation==second.part_generation && first.player==second.player && first.part==second.part && first.animation==second.animation &&
            first.container==second.container && first.binding==second.binding && first.desired_seconds==second.desired_seconds &&
            first.opening_clip==second.opening_clip && first.closing_clip==second.closing_clip &&
            first.close_authorized==second.close_authorized && first.linked==second.linked;
    }
    static bool same(const Request& first,const Request& second) noexcept {
        return same_operation(first.operation,second.operation) && first.game==second.game && first.manager==second.manager &&
            first.component==second.component && first.door==second.door && first.component_generation==second.component_generation &&
            first.door_spawn==second.door_spawn && first.player_spawn==second.player_spawn && first.part_spawn==second.part_spawn;
    }
    static bool same_identity(const Request& first,const Request& second) noexcept {
        auto first_operation=first.operation;
        auto second_operation=second.operation;
        first_operation.desired_seconds=second_operation.desired_seconds;
        first_operation.close_authorized=second_operation.close_authorized;
        return same_operation(first_operation,second_operation) && first.game==second.game && first.manager==second.manager &&
            first.component==second.component && first.door==second.door && first.component_generation==second.component_generation &&
            first.door_spawn==second.door_spawn && first.player_spawn==second.player_spawn && first.part_spawn==second.part_spawn;
    }
    static bool owned_status(const AnimationStatus& status,std::uint64_t input_generation,std::uint64_t revision) noexcept {
        return status.published && status.input_owned && status.evaluated && status.input_generation && status.desired_revision &&
            status.evaluated_revision==status.desired_revision && (!input_generation || status.input_generation==input_generation) &&
            (!revision || status.evaluated_revision==revision);
    }
    static Result expire(State& state) noexcept {
        state={};
        return Result::Expired;
    }
};
}
