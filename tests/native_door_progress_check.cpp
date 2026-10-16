#include "../src/native_door_progress.hpp"
#include <cassert>
#include <iostream>

namespace {
using Progress=d2vr::NativeDoorProgress;
using Request=Progress::Request;
using AnimationStatus=Progress::AnimationStatus;

Request request_fixture(float seconds=0.f) {
    Request request{};
    auto& operation=request.operation;
    operation.ownership=10;
    operation.world=20;
    operation.player_generation=30;
    operation.part_generation=40;
    operation.player=0x20000;
    operation.part=0x21000;
    operation.animation=0x22000;
    operation.container=operation.animation+0x338;
    operation.binding=0x23000;
    operation.desired_seconds=seconds;
    operation.opening_clip=1;
    operation.closing_clip=2;
    request.game=0x24000;
    request.manager=0x25000;
    request.component=0x26000;
    request.door=0x27000;
    request.component_generation=50;
    request.door_spawn=0x1001;
    request.player_spawn=0x1002;
    request.part_spawn=0x1003;
    return request;
}

AnimationStatus owned(std::uint64_t input_generation,std::uint64_t revision) {
    AnimationStatus status{};
    status.input_generation=input_generation;
    status.desired_revision=revision;
    status.evaluated_revision=revision;
    status.published=true;
    status.input_owned=true;
    status.evaluated=true;
    return status;
}

Progress::EventStatus emitted(std::uint64_t epoch=70) {
    return {d2vr::NativeDoorEventAdmission::Stage::Emitted,epoch};
}

void test_initial_acknowledgement_and_timeout() {
    Progress::State state{};
    const auto initial=request_fixture();
    assert(Progress::begin(state,initial,100));
    assert(Progress::poll(state,emitted(),{}, {},101,100,50)==Progress::Result::Wait);
    assert(Progress::poll(state,emitted(),{},owned(11,1),102,100,50)==Progress::Result::Ready);
    assert(state.acknowledged_valid && !state.pending_valid);
    assert(state.acknowledged_input_generation==11 && state.acknowledged_revision==1);

    Progress::State expired{};
    assert(Progress::begin(expired,initial,200));
    assert(Progress::poll(expired,{}, {}, {},300,100,50)==Progress::Result::Expired);
    assert(!expired.begun);
}

void test_pending_waits_without_replacing_and_promotes_exact_revision() {
    Progress::State state{};
    const auto initial=request_fixture();
    assert(Progress::begin(state,initial,100));
    assert(Progress::poll(state,emitted(),{},owned(11,1),101,100,100)==Progress::Result::Ready);
    const auto changed=request_fixture(.4f);
    assert(Progress::queue(state,changed,110));
    assert(!Progress::queue(state,request_fixture(.6f),111));
    auto foreign=request_fixture(.5f);
    foreign.operation.ownership++;
    assert(!Progress::queue(state,foreign,111));
    const auto old_status=owned(11,1);
    assert(Progress::poll(state,emitted(),old_status,{},112,100,100)==Progress::Result::Wait);
    assert(state.pending_valid && state.pending.operation.desired_seconds==.4f);
    assert(Progress::poll(state,emitted(),old_status,owned(11,1),113,100,100)==Progress::Result::Wait);
    assert(Progress::poll(state,emitted(),{},owned(11,2),114,100,100)==Progress::Result::Ready);
    assert(!state.pending_valid && state.acknowledged.operation.desired_seconds==.4f);
    assert(state.acknowledged_input_generation==11 && state.acknowledged_revision==2);

    Progress::State evaluating{};
    assert(Progress::begin(evaluating,initial,200));
    assert(Progress::poll(evaluating,emitted(),{},owned(11,1),201,100,100)==Progress::Result::Ready);
    assert(Progress::queue(evaluating,changed,202));
    auto published_not_evaluated=owned(11,2);
    published_not_evaluated.evaluated=false;
    published_not_evaluated.evaluated_revision=0;
    assert(Progress::poll(evaluating,emitted(),{},published_not_evaluated,203,100,100)==Progress::Result::Wait);
    assert(evaluating.pending_valid && evaluating.acknowledged_revision==1);
    assert(Progress::poll(evaluating,emitted(),{},owned(11,2),204,100,100)==Progress::Result::Ready);

    Progress::State ownership_lost{};
    assert(Progress::begin(ownership_lost,initial,210));
    assert(Progress::poll(ownership_lost,emitted(),{},owned(11,1),211,100,100)==Progress::Result::Ready);
    assert(Progress::queue(ownership_lost,changed,212));
    auto unpublished_input=owned(11,2);
    unpublished_input.input_owned=false;
    assert(Progress::poll(ownership_lost,emitted(),{},unpublished_input,213,100,100)==Progress::Result::Expired);
}

void test_aba_loss_and_epoch_change() {
    Progress::State state{};
    const auto initial=request_fixture();
    assert(Progress::begin(state,initial,10));
    assert(Progress::poll(state,emitted(),{},owned(11,1),11,100,100)==Progress::Result::Ready);
    assert(Progress::queue(state,request_fixture(.2f),12));
    assert(Progress::poll(state,emitted(),owned(11,1),owned(11,2),13,100,100)==Progress::Result::Ready);
    assert(Progress::queue(state,request_fixture(.3f),14));
    assert(Progress::poll(state,emitted(71),owned(11,2),owned(11,3),15,100,100)==Progress::Result::Expired);

    Progress::State lost{};
    assert(Progress::begin(lost,initial,20));
    assert(Progress::poll(lost,emitted(),{},owned(11,1),21,100,100)==Progress::Result::Ready);
    assert(Progress::poll(lost,emitted(),{},owned(12,1),22,100,100)==Progress::Result::Expired);

    Progress::State stale_pending{};
    assert(Progress::begin(stale_pending,initial,30));
    assert(Progress::poll(stale_pending,emitted(),{},owned(11,1),31,100,100)==Progress::Result::Ready);
    assert(Progress::queue(stale_pending,request_fixture(.5f),32));
    assert(Progress::poll(stale_pending,emitted(),owned(11,1),{},33,100,100)==Progress::Result::Wait);
    assert(Progress::poll(stale_pending,emitted(),{}, {},34,100,100)==Progress::Result::Expired);

    Progress::State unauthorized_generation{};
    assert(Progress::begin(unauthorized_generation,initial,40));
    assert(Progress::poll(unauthorized_generation,emitted(),{},owned(11,1),41,100,100)==Progress::Result::Ready);
    assert(Progress::queue(unauthorized_generation,request_fixture(.5f),42));
    assert(Progress::poll(unauthorized_generation,emitted(),owned(11,1),owned(12,1),43,100,100)==Progress::Result::Expired);

    Progress::State handed_generation{};
    assert(Progress::begin(handed_generation,initial,50));
    assert(Progress::poll(handed_generation,emitted(),{},owned(11,1),51,100,100)==Progress::Result::Ready);
    auto closing=request_fixture(.5f);
    closing.operation.close_authorized=true;
    assert(Progress::queue(handed_generation,closing,52));
    Progress::mark_handoff(handed_generation);
    assert(Progress::poll(handed_generation,emitted(),{},owned(12,1),53,100,100)==Progress::Result::Ready);
}

void test_pending_timeout_and_close_handoff_survives_input_loss() {
    Progress::State timed{};
    const auto initial=request_fixture();
    assert(Progress::begin(timed,initial,10));
    assert(Progress::poll(timed,emitted(),{},owned(11,1),11,100,100)==Progress::Result::Ready);
    assert(Progress::queue(timed,request_fixture(.5f),12));
    assert(Progress::poll(timed,emitted(),owned(11,1),{},112,100,100)==Progress::Result::Expired);

    Progress::State handed{};
    assert(Progress::begin(handed,initial,200));
    assert(Progress::poll(handed,emitted(),{},owned(11,1),201,100,100)==Progress::Result::Ready);
    Progress::mark_handoff(handed);
    assert(Progress::poll(handed,emitted(),{}, {},202,100,100)==Progress::Result::Ready);
    assert(handed.close_handed_off && handed.acknowledged_valid);
}

void test_equal_tick_waits_and_backward_tick_expires() {
    Progress::State state{};
    const auto initial=request_fixture();
    assert(Progress::begin(state,initial,10));
    assert(Progress::poll(state,emitted(),{},owned(11,1),11,100,100)==Progress::Result::Ready);
    assert(Progress::poll(state,emitted(),owned(11,1),{},11,100,100)==Progress::Result::Wait);
    assert(state.begun && state.last_tick_ms==11);
    assert(Progress::poll(state,emitted(),owned(11,1),{},10,100,100)==Progress::Result::Expired);
    assert(!state.begun);
}
}

int main() {
    test_initial_acknowledgement_and_timeout();
    test_pending_waits_without_replacing_and_promotes_exact_revision();
    test_aba_loss_and_epoch_change();
    test_pending_timeout_and_close_handoff_survives_input_loss();
    test_equal_tick_waits_and_backward_tick_expires();
    std::cout<<"native_door_progress_check PASS\n";
}
