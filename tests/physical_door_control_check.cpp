#include "../src/physical_door_control.hpp"
#include <cassert>
#include <cmath>

using Controller=d2vr::PhysicalDoorController;

namespace {
float identity_mapping(const void* context,float angle) noexcept {return angle*(*static_cast<const float*>(context));}

Controller::Input make_input() {
    Controller::Input input{};
    input.owner_token=1;
    input.dt_seconds=1.f/90.f;
    input.native_state=Controller::NativeState::Closed;
    input.target_angle_radians=.2f;
    input.fresh_contact=true;
    input.contact_identity=7;
    input.pressure=Controller::Pressure::Opening;
    input.fast_open_speed=1.2f;
    input.begin_dwell_seconds=0.f;
    input.close_dwell_seconds=.05f;
    input.normal_slew_seconds_per_second=10.f;
    input.closing_slew_seconds_per_second=10.f;
    input.angle_deadband_radians=.005f;
    static constexpr float mapping_scale=1.f;
    input.mapping={identity_mapping,&mapping_scale,2.f,true};
    input.native_use_eligible=true;
    return input;
}

Controller::State begin_opening(Controller::Input& input) {
    Controller::State state{};
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::BeginNativeOpen);
    input.native_state=Controller::NativeState::Animated;
    input.animation_receipt_active=true;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::UpdateOpen);
    return state;
}

float run_slew(unsigned frames,float dt) {
    auto input=make_input();
    input.target_angle_radians=.8f;
    input.normal_slew_seconds_per_second=1.f;
    input.dt_seconds=dt;
    input.begin_dwell_seconds=0.f;
    Controller::State state{};
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::BeginNativeOpen);
    input.native_state=Controller::NativeState::Animated;
    input.animation_receipt_active=true;
    for(unsigned frame=0;frame<frames;++frame) {
        input.monotonic_tick_ms=static_cast<std::uint64_t>(std::lround((frame+1)*dt*1000.f));
        decision=Controller::step(state,input);
        assert(decision.action==Controller::Action::UpdateOpen);
    }
    return decision.seconds;
}

void test_bidirectional_and_deadband() {
    auto input=make_input();
    auto state=begin_opening(input);
    input.target_angle_radians=.1f;
    input.current_angle_radians=.2f;
    input.monotonic_tick_ms+=11;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::UpdateOpen);
    assert(decision.seconds<.2f && std::abs(decision.seconds-.1f)<.0001f);
    input.target_angle_radians=0.f;
    input.current_angle_radians=.1f;
    input.pressure=Controller::Pressure::Closing;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::UpdateOpen);
    assert(decision.seconds==0.f);
    input.pressure=Controller::Pressure::Opening;
    input.target_angle_radians=.002f;
    input.current_angle_radians=.001f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
}

void test_rate_equivalence() {
    const auto at72=run_slew(14,1.f/72.f);
    const auto at90=run_slew(18,1.f/90.f);
    const auto at120=run_slew(24,1.f/120.f);
    assert(std::abs(at72-.2f)<.01f);
    assert(std::abs(at90-.2f)<.01f);
    assert(std::abs(at120-.2f)<.01f);
}

void test_fast_open_handoff_and_fast_closing() {
    auto input=make_input();
    auto state=begin_opening(input);
    input.pressure=Controller::Pressure::Opening;
    input.inward_speed=1.3f;
    input.monotonic_tick_ms+=11;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::ReleaseOpen);
    assert(state.phase==Controller::Phase::HandedOff && state.fast_open_handoff);
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);

    input=make_input();
    state=begin_opening(input);
    input.pressure=Controller::Pressure::Closing;
    input.target_angle_radians=0.f;
    input.current_angle_radians=.2f;
    input.close_dwell_seconds=0.f;
    input.closing_slew_seconds_per_second=20.f;
    input.zero_ack=true;
    input.close_authorized=true;
    input.inward_speed=2.f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::UpdateOpen);
    assert(decision.seconds==0.f);
    assert(state.phase==Controller::Phase::WaitingForZero);
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::CloseOwned);
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
}

void test_release_and_owner_change() {
    auto input=make_input();
    auto state=begin_opening(input);
    input.fresh_contact=false;
    input.pressure=Controller::Pressure::None;
    input.monotonic_tick_ms+=11;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::ReleaseOpen);
    assert(state.phase==Controller::Phase::Idle && !state.close_emitted);

    input=make_input();
    state=begin_opening(input);
    input.owner_token=2;
    input.native_state=Controller::NativeState::Opened;
    input.animation_receipt_active=false;
    input.pressure=Controller::Pressure::Closing;
    input.close_dwell_seconds=.05f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::ReleaseOpen);
    assert(state.phase==Controller::Phase::Idle);
}

void test_native_use_close_action() {
    auto input=make_input();
    Controller::State state{};
    input.native_state=Controller::NativeState::Opened;
    input.pressure=Controller::Pressure::Closing;
    input.close_dwell_seconds=0.f;
    input.monotonic_tick_ms=10;
    const auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::NativeUseClose);
}

void test_native_close_handoff_blocks_reopen_until_contact_loss() {
    auto input=make_input();
    Controller::State state{};
    input.native_state=Controller::NativeState::Opened;
    input.pressure=Controller::Pressure::Closing;
    input.close_dwell_seconds=0.f;
    input.monotonic_tick_ms=10;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::NativeUseClose);
    assert(state.phase==Controller::Phase::HandedOff && state.close_emitted);

    input.native_state=Controller::NativeState::Closed;
    input.pressure=Controller::Pressure::Opening;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);

    input.fresh_contact=false;
    input.pressure=Controller::Pressure::None;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None && state.phase==Controller::Phase::Idle);
    input.fresh_contact=true;
    input.pressure=Controller::Pressure::Opening;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::BeginNativeOpen);
}

void test_native_close_dwell() {
    auto input=make_input();
    Controller::State state{};
    input.native_state=Controller::NativeState::Opened;
    input.pressure=Controller::Pressure::Closing;
    input.close_dwell_seconds=.05f;
    input.monotonic_tick_ms=100;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    for(unsigned frame=0;frame<2;++frame) {
        input.monotonic_tick_ms+=11;
        decision=Controller::step(state,input);
        assert(decision.action==Controller::Action::None);
    }
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::NativeUseClose);
    for(unsigned frame=0;frame<90;++frame) {
        input.monotonic_tick_ms+=11;
        decision=Controller::step(state,input);
        assert(decision.action==Controller::Action::None);
    }
    input.fresh_contact=false;
    input.pressure=Controller::Pressure::None;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    input.fresh_contact=true;
    input.pressure=Controller::Pressure::Closing;
    input.close_dwell_seconds=0.f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::NativeUseClose);
}

void test_withdrawal_never_begins_or_closes() {
    auto input=make_input();
    Controller::State state{};
    input.inward_speed=-.01f;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None && state.opening_dwell==0.f);
    input.inward_speed=0.f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::BeginNativeOpen);

    input=make_input();
    state=begin_opening(input);
    input.pressure=Controller::Pressure::Closing;
    input.inward_speed=-.01f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::ReleaseOpen);
    assert(!state.active && !state.close_emitted);

    input=make_input();
    state={};
    input.native_state=Controller::NativeState::Opened;
    input.pressure=Controller::Pressure::Closing;
    input.inward_speed=-.01f;
    input.close_dwell_seconds=0.f;
    input.monotonic_tick_ms=100;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None && state.close_dwell==0.f && !state.close_emitted);
    input.inward_speed=0.f;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::NativeUseClose);
}

void test_single_begin_and_monotonic_clock() {
    auto input=make_input();
    input.begin_dwell_seconds=.03f;
    Controller::State state{};
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    input.monotonic_tick_ms=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    input.monotonic_tick_ms=22;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::BeginNativeOpen);
    input.monotonic_tick_ms=33;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    input.native_state=Controller::NativeState::Animated;
    input.animation_receipt_active=true;
    input.monotonic_tick_ms=44;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::UpdateOpen);
    const auto prior_command=state.commanded_seconds;
    input.monotonic_tick_ms=44;
    input.target_angle_radians=.4f;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    assert(state.commanded_seconds==prior_command);
    input.monotonic_tick_ms=43;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::None);
    assert(state.commanded_seconds==prior_command);
}

void test_invalid_mapping_and_identity() {
    auto input=make_input();
    auto state=begin_opening(input);
    input.mapping.valid=false;
    input.target_angle_radians=.3f;
    input.monotonic_tick_ms+=11;
    auto decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::ReleaseOpen);
    assert(state.phase==Controller::Phase::Idle);

    input=make_input();
    state=begin_opening(input);
    input.contact_identity=8;
    input.monotonic_tick_ms+=11;
    decision=Controller::step(state,input);
    assert(decision.action==Controller::Action::ReleaseOpen);
    assert(state.phase==Controller::Phase::Idle);
}
}

int main() {
    test_bidirectional_and_deadband();
    test_rate_equivalence();
    test_fast_open_handoff_and_fast_closing();
    test_release_and_owner_change();
    test_native_use_close_action();
    test_native_close_handoff_blocks_reopen_until_contact_loss();
    test_native_close_dwell();
    test_withdrawal_never_begins_or_closes();
    test_single_begin_and_monotonic_clock();
    test_invalid_mapping_and_identity();
    return 0;
}
