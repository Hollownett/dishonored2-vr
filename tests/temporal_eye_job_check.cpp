#include "temporal_eye_job.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
using namespace temporal_eye;
namespace {
struct Reference {
    int* destroyed;
    explicit Reference(int* value) : destroyed(value) {}
    ~Reference() { if (destroyed) ++*destroyed; }
};
Lease lease(int* destroyed = nullptr) { return std::make_shared<Reference>(destroyed); }
Identity id(std::uint64_t serial) { return {static_cast<std::uintptr_t>(serial), serial}; }
List list(std::uint64_t serial, int* destroyed = nullptr) { return {id(serial), lease(destroyed)}; }
RenderInputs inputs(unsigned eye = 0) {
    RenderInputs r;
    r.identity.key = {17, 9, eye};
    r.identity.generations = {1, 2, 3, 4, 5};
    r.identity.colour = {100, 80, 2, 3, 90, 70, 10};
    r.identity.depth = {100, 80, 2, 3, 90, 70, 11};
    r.identity.motion = {100, 80, 2, 3, 90, 70, 12};
    r.identity.output = {200, 160, 0, 0, 200, 160, 10};
    r.colour = {id(101), lease(), r.identity.key, 1, 10};
    r.depth = {id(102), lease(), r.identity.key, 1, 11};
    r.motion = {id(103), lease(), r.identity.key, 1, 12};
    r.output = {id(104), lease(), r.identity.key, 1, 13};
    r.feature_owner = lease();
    r.jitter_x = .25f;
    r.jitter_y = -.25f;
    r.mv_scale_x = -90;
    r.mv_scale_y = -70;
    return r;
}
ExecutionInputs current() {
    auto left = inputs(0), right = inputs(1);
    return {true, {left.identity, right.identity},
            {provenance(left.depth), provenance(right.depth)},
            {provenance(left.motion), provenance(right.motion)}};
}
struct Fake final : Executor {
    std::vector<std::string> events;
    std::vector<bool> restores;
    bool depth_ok = true, evaluate_ok = true;
    RetainedJobs* queue{};
    Identity remainder{};
    ExecutionInputs* caller_state{};
    RetainedJobs::Reservation old_reservation{};
    bool try_reentry{};
    void native(const List& l, bool restore) noexcept override {
        events.push_back("list" + std::to_string(l.id.incarnation));
        restores.push_back(restore);
        assert(l.owner);
        if (try_reentry) {
            try_reentry = false;
            assert(!queue->execute(remainder, !restore, current(), *this));
            assert(!queue->retire({old_reservation.chain}));
            assert(!queue->split(old_reservation, list(999)));
            if (caller_state) caller_state->backend_enabled = false;
        }
    }
    bool snapshot_depth(const RenderInputs& r) noexcept override {
        events.push_back("depth" + std::to_string(r.identity.key.eye));
        assert(r.depth.owner && r.feature_owner);
        return depth_ok;
    }
    bool evaluate(const RenderInputs& r) noexcept override {
        events.push_back("evaluate" + std::to_string(r.identity.key.eye));
        assert(r.jitter_x == .25f && r.mv_scale_x == -90);
        return evaluate_ok;
    }
    void fallback(const RenderInputs& r) noexcept override {
        assert(r.colour.owner && r.output.owner);
        events.push_back("fallback" + std::to_string(r.identity.key.eye));
    }
};
void expect(const Fake& f, std::initializer_list<const char*> wanted) {
    std::vector<std::string> values;
    for (const char* p : wanted) values.emplace_back(p);
    assert(f.events == values);
}
void phases_and_lifetime(bool restore) {
    RetainedJobs q;
    int destroyed = 0;
    auto r = inputs();
    r.colour.owner = lease(&destroyed);
    r.depth.owner = lease(&destroyed);
    r.motion.owner = lease(&destroyed);
    r.output.owner = lease(&destroyed);
    r.feature_owner = lease(&destroyed);
    auto context_owner = lease(&destroyed);
    assert(valid(r)); // No future command lists or execution restore required.
    auto reservation = q.reserve(id(1), context_owner, r);
    assert(reservation);
    r.identity.key.frame = 999; // Caller changes do not mutate frozen receipt.
    r.jitter_x = 123;
    assert(!q.finished(id(1), list(22)));
    auto prefix = list(21, &destroyed);
    assert(q.split(reservation, prefix)); // adapter's successful Finish(TRUE)
    assert(!q.split(reservation, list(99)));
    assert(!q.cancel(reservation));
    auto remainder = list(22, &destroyed);
    assert(q.finished(id(1), remainder));
    assert(!q.finished(id(1), list(23)));
    r = {};
    context_owner.reset();
    prefix = {};
    remainder = {};
    assert(destroyed == 0);
    auto state = current();
    Fake f;
    f.queue = &q;
    f.remainder = id(22);
    f.caller_state = &state;
    f.old_reservation = reservation;
    f.try_reentry = true;
    auto submission = q.execute(id(22), restore, state, f);
    assert(submission && !state.backend_enabled);
    expect(f, {"list21", "depth0", "evaluate0", "list22"});
    assert(f.restores == std::vector<bool>({restore, restore}));
    assert(destroyed == 0); // CPU return is explicitly NOT retirement.
    assert(!q.execute(id(22), restore, current(), f));
    assert(!q.retire({submission.serial + 1000}));
    assert(q.retire(submission));
    assert(destroyed == 8); // 4 resources, feature, context and 2 lists.
    assert(!q.retire(submission));
}
void chains_and_contexts() {
    RetainedJobs q;
    auto a = q.reserve(id(1), lease(), inputs(0));
    assert(a && !q.reserve(id(1), lease(), inputs(1))); // no second pending split
    assert(q.split(a, list(31)));
    auto b = q.reserve(id(2), lease(), inputs(1));
    assert(b && q.split(b, list(41)));
    auto c = q.reserve(id(1), lease(), inputs(1));
    assert(c && q.split(c, list(32)));
    assert(q.finished(id(2), list(42)));
    assert(q.finished(id(1), list(33)));
    Fake f;
    auto bs = q.execute(id(42), false, current(), f);
    auto as = q.execute(id(33), true, current(), f);
    expect(f, {"list41", "depth1", "evaluate1", "list42", "list31", "depth0",
               "evaluate0", "list32", "depth1", "evaluate1", "list33"});
    assert(f.restores == std::vector<bool>({false, false, true, true, true}));
    assert(q.retire(bs) && q.retire(as));
}
void rejection_and_capacity() {
    RetainedJobs q;
    auto bad = inputs();
    bad.depth.owner.reset();
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); bad.motion.producer_stamp = 0;
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); ++bad.depth.produced_for.frame;
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); ++bad.motion.produced_for.pair;
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); bad.motion.produced_for.eye = 1;
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); ++bad.depth.device_generation;
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); bad.identity.colour.x = 101;
    assert(!q.reserve(id(1), lease(), bad));
    bad = inputs(); bad.identity.key.eye = 2;
    assert(!q.reserve(id(1), lease(), bad));
    assert(!q.reserve({}, lease(), inputs()));
    assert(!q.reserve(id(1), {}, inputs()));
    // Failed native split cancels without consuming a native list, frees the slot.
    auto cancelled = q.reserve(id(1), lease(), inputs());
    assert(cancelled && q.cancel(cancelled) && !q.cancel(cancelled));
    std::array<RetainedJobs::Reservation, RetainedJobs::capacity> reservations{};
    for (std::size_t n = 0; n < reservations.size(); ++n) {
        reservations[n] = q.reserve(id(n + 1), lease(), inputs());
        assert(reservations[n]);
    }
    assert(!q.reserve(id(99), lease(), inputs()));
    for (auto r : reservations) assert(q.cancel(r));
    assert(!q.split(cancelled, list(55)));
}
void stale_fallbacks() {
    // Mutations are execution-time invalidations, so every native prefix/remainder
    // MUST still run exactly once. No rejected/disabled job may evaluate stale data.
    for (int mutation = 0; mutation < 19; ++mutation) {
        RetainedJobs q;
        auto a = q.reserve(id(1), lease(), inputs());
        assert(a && q.split(a, list(51)) && q.finished(id(1), list(52)));
        auto state = current();
        auto& e = state.expected[0];
        switch (mutation) {
        case 0: state.backend_enabled = false; break;
        case 1: ++e.key.frame; break;
        case 2: ++e.key.pair; break;
        case 3: e.key.eye = 1; break;
        case 4: ++e.generations.device; break;
        case 5: ++e.generations.backend; break;
        case 6: ++e.generations.feature; break;
        case 7: ++e.generations.reset; break;
        case 8: ++e.generations.jitter; break;
        case 9: ++e.colour.width; break;
        case 10: ++e.depth.format; break;
        case 11: ++e.motion.rect_width; break;
        case 12: ++e.output.height; break;
        case 13: state.depth[0].producer_stamp = 0; break;
        case 14: ++state.depth[0].producer_stamp; break;
        case 15: state.motion[0].producer_stamp = 0; break;
        case 16: ++state.motion[0].producer_stamp; break;
        case 17: ++state.depth[0].resource.incarnation; break;
        case 18: ++state.motion[0].resource.incarnation; break;
        default: assert(false);
        }
        Fake f;
        auto submission = q.execute(id(52), true, state, f);
        expect(f, {"list51", "fallback0", "list52"});
        assert(!q.execute(id(52), false, current(), f));
        assert(q.retire(submission));
    }
    for (bool depth_failure : {false, true}) {
        RetainedJobs q;
        auto a = q.reserve(id(1), lease(), inputs());
        assert(q.split(a, list(61)) && q.finished(id(1), list(62)));
        Fake f;
        f.depth_ok = !depth_failure;
        f.evaluate_ok = false;
        auto submission = q.execute(id(62), false, current(), f);
        if (depth_failure) expect(f, {"list61", "depth0", "fallback0", "list62"});
        else expect(f, {"list61", "depth0", "evaluate0", "fallback0", "list62"});
        assert(q.retire(submission));
    }
}
void sealed_capacity_and_cancelled_chain() {
    RetainedJobs q;
    std::array<RetainedJobs::Submission, RetainedJobs::capacity> submissions{};
    Fake f;
    for (std::size_t n = 0; n < submissions.size(); ++n) {
        const auto context = id(200 + n);
        auto first = q.reserve(context, lease(), inputs());
        assert(q.split(first, list(300 + n * 2)));
        auto refused_split = q.reserve(context, lease(), inputs(1));
        assert(refused_split && q.cancel(refused_split)); // preserves first prefix
        assert(q.finished(context, list(301 + n * 2)));
        assert(!q.finished(context, list(400 + n)));
        submissions[n] = q.execute(id(301 + n * 2), false, current(), f);
        assert(submissions[n]);
    }
    assert(!q.reserve(id(999), lease(), inputs())); // submitted slots still own leases
    assert(f.events.size() == RetainedJobs::capacity * 4);
    assert(q.retire(submissions[0]));
    auto available = q.reserve(id(999), lease(), inputs());
    assert(available && q.cancel(available));
    for (std::size_t n = 1; n < submissions.size(); ++n) assert(q.retire(submissions[n]));
}
void reused_addresses() {
    RetainedJobs q;
    auto a = q.reserve(id(1), lease(), inputs());
    assert(q.split(a, list(71)) && q.finished(id(1), list(72)));
    Fake f;
    auto old = q.execute(id(72), false, current(), f);
    assert(q.retire(old));
    auto b = q.reserve(id(1), lease(), inputs());
    List new_prefix{{71, 171}, lease()}, new_remainder{{72, 172}, lease()};
    assert(q.split(b, new_prefix) && q.finished(id(1), new_remainder));
    assert(!q.execute(id(72), false, current(), f)); // old raw address cannot match
    auto fresh = q.execute(new_remainder.id, true, current(), f);
    assert(fresh && !q.retire(old) && q.retire(fresh));
    expect(f, {"list71", "depth0", "evaluate0", "list72", "list171", "depth0", "evaluate0", "list172"});
}
}
int main() {
    static_assert(!std::is_copy_constructible<RetainedJobs>::value, "queue ownership is unique");
    phases_and_lifetime(false);
    phases_and_lifetime(true);
    chains_and_contexts();
    rejection_and_capacity();
    stale_fallbacks();
    sealed_capacity_and_cancelled_chain();
    reused_addresses();
    std::cout << "temporal_eye_job_check: passed\n";
}
