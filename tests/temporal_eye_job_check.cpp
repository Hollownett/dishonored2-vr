#include "temporal_eye_job.hpp"
#include <cassert>
#include <cstdlib>
#include <new>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
// CPU allocation-failure injection for sole-owner custom-deleter reentry.
// The counter resets before throwing so cleanup callbacks can allocate normally.
namespace allocation_failure { int remaining = -1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t size) {
    if (allocation_failure::remaining == 0) {
        allocation_failure::remaining = -1;
        throw std::bad_alloc();
    }
    if (allocation_failure::remaining > 0) --allocation_failure::remaining;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#undef TEST_NOINLINE
using namespace temporal_eye;
namespace {
bool submitted(const RetainedJobs::ExecuteResult& r) {
    return r.status == RetainedJobs::ExecuteStatus::submitted && bool(r.submission);
}
bool missed(const RetainedJobs::ExecuteResult& r) {
    return r.status == RetainedJobs::ExecuteStatus::miss && !r.submission && r.deferred.owner && r.deferred.id.valid();
}
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
    r.identity.generations = {1, 2, 3, 4, 5, 6};
    r.identity.device = id(90);
    r.identity.requested_mode = RequestedMode::dlaa;
    r.identity.selected_backend = Backend::dlss;
    r.identity.colour = {100, 80, 2, 3, 90, 70, 10};
    r.identity.depth = {100, 80, 2, 3, 90, 70, 11};
    r.identity.motion = {100, 80, 2, 3, 90, 70, 12};
    r.identity.output = {200, 160, 0, 0, 200, 160, 10};
    r.colour = {id(101), lease(), r.identity.key, 1, 10};
    r.depth = {id(102), lease(), r.identity.key, 1, 11};
    r.motion = {id(103), lease(), r.identity.key, 1, 12};
    r.output = {id(104), lease(), r.identity.key, 1, 13};
    r.feature_owner = lease();
    r.device_owner = lease();
    r.depth_view_owner = lease();
    r.identity.colour_selection = {r.colour.id, 1};
    r.identity.depth_selection = {r.depth.id, 2};
    r.identity.motion_selection = {r.motion.id, 3};
    r.identity.output_selection = {r.output.id, 4};
    r.identity.depth_view = {id(110), r.identity.depth_selection, 5,
                             DepthViewDimension::texture2d, 11, 0, 1, 0, 1};
    r.identity.jitter = {.25f, -.25f, -.125f, .125f, 3, 32, false};
    r.identity.motion_contract = {-90, -70, false, MotionDirection::current_to_previous,
                                 MotionUnits::uv, MotionResolution::active_render};
    r.identity.colour_contract = {ColourDomain::native_scene, ColourEncoding::linear,
                                  ExposureSource::automatic, true, true, 1, 1, {}, {}};
    r.identity.inverted_depth = true;
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
    bool depth_ok = true, evaluate_ok = true, fallback_ok = true;
    RetainedJobs* queue{};
    List remainder{};
    ExecutionInputs* caller_state{};
    RetainedJobs::Reservation old_reservation{};
    bool try_reentry{};
    void native(const List& l, bool restore) noexcept override {
        events.push_back("list" + std::to_string(l.id.incarnation));
        restores.push_back(restore);
        assert(l.owner);
        if (try_reentry) {
            try_reentry = false;
            auto blocked = queue->execute(remainder, !restore, current(), *this);
            assert(blocked.status == RetainedJobs::ExecuteStatus::busy && !blocked.submission);
            assert(blocked.deferred.id == remainder.id && blocked.deferred.owner && blocked.restore == !restore);
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
        assert(r.identity.jitter.current_x == .25f && r.identity.motion_contract.scale_x == -90);
        return evaluate_ok;
    }
    bool fallback_only(const Lease& owner) noexcept override {
        assert(owner); events.push_back("legacy_fallback");return fallback_ok;
    }
    bool fallback(const RenderInputs& r) noexcept override {
        assert(r.colour.owner && r.output.owner);
        events.push_back("fallback" + std::to_string(r.identity.key.eye));return fallback_ok;
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
    r.device_owner = lease(&destroyed);
    r.depth_view_owner = lease(&destroyed);
    auto context_owner = lease(&destroyed);
    assert(valid(r)); // No future command lists or execution restore required.
    auto reservation = q.reserve(id(1), context_owner, r);
    assert(reservation);
    r.identity.key.frame = 999; // Caller changes do not mutate frozen receipt.
    r.identity.jitter.current_x = 123;
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
    f.remainder = list(22);
    f.caller_state = &state;
    f.old_reservation = reservation;
    f.try_reentry = true;
    auto submission = q.execute(list(22), restore, state, f);
    assert(submitted(submission) && !state.backend_enabled);
    expect(f, {"list21", "depth0", "evaluate0", "list22"});
    assert(f.restores == std::vector<bool>({restore, restore}));
    assert(destroyed == 0); // CPU return is explicitly NOT retirement.
    assert(missed(q.execute(list(22), restore, current(), f)));
    assert(!q.retire({submission.submission.serial + 1000}));
    assert(q.retire(submission.submission));
    assert(destroyed == 10); // 4 resources, feature/device/view, context and 2 lists.
    assert(!q.retire(submission.submission));
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
    auto bs = q.execute(list(42), false, current(), f);
    auto as = q.execute(list(33), true, current(), f);
    expect(f, {"list41", "depth1", "evaluate1", "list42", "list31", "depth0",
               "evaluate0", "list32", "depth1", "evaluate1", "list33"});
    assert(f.restores == std::vector<bool>({false, false, true, true, true}));
    assert(q.retire(bs.submission) && q.retire(as.submission));
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
        auto submission = q.execute(list(52), true, state, f);
        expect(f, {"list51", "fallback0", "list52"});
        assert(missed(q.execute(list(52), false, current(), f)));
        assert(q.retire(submission.submission));
    }
    for (bool depth_failure : {false, true}) {
        RetainedJobs q;
        auto a = q.reserve(id(1), lease(), inputs());
        assert(q.split(a, list(61)) && q.finished(id(1), list(62)));
        Fake f;
        f.depth_ok = !depth_failure;
        f.evaluate_ok = false;
        auto submission = q.execute(list(62), false, current(), f);
        if (depth_failure) expect(f, {"list61", "depth0", "fallback0", "list62"});
        else expect(f, {"list61", "depth0", "evaluate0", "fallback0", "list62"});
        assert(q.retire(submission.submission));
    }
}
void sealed_capacity_and_cancelled_chain() {
    RetainedJobs q;
    std::array<RetainedJobs::ExecuteResult, RetainedJobs::capacity> submissions{};
    Fake f;
    for (std::size_t n = 0; n < submissions.size(); ++n) {
        const auto context = id(200 + n);
        auto first = q.reserve(context, lease(), inputs());
        assert(q.split(first, list(300 + n * 2)));
        auto refused_split = q.reserve(context, lease(), inputs(1));
        assert(refused_split && q.cancel(refused_split)); // preserves first prefix
        assert(q.finished(context, list(301 + n * 2)));
        assert(!q.finished(context, list(400 + n)));
        submissions[n] = q.execute(list(301 + n * 2), false, current(), f);
        assert(submitted(submissions[n]));
    }
    assert(!q.reserve(id(999), lease(), inputs())); // submitted slots still own leases
    assert(f.events.size() == RetainedJobs::capacity * 4);
    assert(q.retire(submissions[0].submission));
    auto available = q.reserve(id(999), lease(), inputs());
    assert(available && q.cancel(available));
    for (std::size_t n = 1; n < submissions.size(); ++n) assert(q.retire(submissions[n].submission));
}
void complete_receipt_freshness() {
    using Change = void (*)(InputIdentity&);
    const Change changes[] = {
        [](InputIdentity& e) { ++e.generations.dimensions; },
        [](InputIdentity& e) { ++e.device.incarnation; },
        [](InputIdentity& e) { e.requested_mode = RequestedMode::dlss_quality; },
        [](InputIdentity& e) { e.selected_backend = Backend::fsr4; },
        [](InputIdentity& e) { ++e.colour.mip_levels; },
        [](InputIdentity& e) { ++e.colour.array_size; },
        [](InputIdentity& e) { ++e.colour.sample_count; },
        [](InputIdentity& e) { ++e.colour.sample_quality; },
        [](InputIdentity& e) { ++e.colour.usage; },
        [](InputIdentity& e) { ++e.colour.bind_flags; },
        [](InputIdentity& e) { ++e.colour.cpu_access_flags; },
        [](InputIdentity& e) { ++e.colour.misc_flags; },
        [](InputIdentity& e) { ++e.colour_selection.resource.incarnation; },
        [](InputIdentity& e) { ++e.colour_selection.generation; },
        [](InputIdentity& e) { ++e.depth_selection.resource.incarnation; },
        [](InputIdentity& e) { ++e.depth_selection.generation; },
        [](InputIdentity& e) { ++e.motion_selection.resource.incarnation; },
        [](InputIdentity& e) { ++e.motion_selection.generation; },
        [](InputIdentity& e) { ++e.output_selection.resource.incarnation; },
        [](InputIdentity& e) { ++e.output_selection.generation; },
        [](InputIdentity& e) { ++e.depth_view.view.incarnation; },
        [](InputIdentity& e) { ++e.depth_view.resource.resource.incarnation; },
        [](InputIdentity& e) { ++e.depth_view.resource.generation; },
        [](InputIdentity& e) { ++e.depth_view.generation; },
        [](InputIdentity& e) { ++e.depth_view.format; },
        [](InputIdentity& e) { ++e.depth_view.most_detailed_mip; },
        [](InputIdentity& e) { ++e.depth_view.mip_levels; },
        [](InputIdentity& e) { ++e.depth_view.first_array_slice; },
        [](InputIdentity& e) { ++e.depth_view.array_size; },
        [](InputIdentity& e) { e.depth_view.dimension = DepthViewDimension::texture2d_ms; },
        [](InputIdentity& e) { ++e.jitter.current_x; },
        [](InputIdentity& e) { ++e.jitter.current_y; },
        [](InputIdentity& e) { ++e.jitter.previous_x; },
        [](InputIdentity& e) { ++e.jitter.previous_y; },
        [](InputIdentity& e) { ++e.jitter.phase; },
        [](InputIdentity& e) { ++e.jitter.phase_count; },
        [](InputIdentity& e) { e.jitter.reset_history = true; },
        [](InputIdentity& e) { ++e.motion_contract.scale_x; },
        [](InputIdentity& e) { ++e.motion_contract.scale_y; },
        [](InputIdentity& e) { e.motion_contract.jittered = true; },
        [](InputIdentity& e) { e.motion_contract.direction = MotionDirection::previous_to_current; },
        [](InputIdentity& e) { e.motion_contract.units = MotionUnits::pixels; },
        [](InputIdentity& e) { e.motion_contract.resolution = MotionResolution::full_render; },
        [](InputIdentity& e) { e.colour_contract.domain = ColourDomain::post_tonemap; },
        [](InputIdentity& e) { e.colour_contract.encoding = ColourEncoding::perceptual_gamma; },
        [](InputIdentity& e) { e.colour_contract.exposure = ExposureSource::fixed; },
        [](InputIdentity& e) { e.colour_contract.hdr = false; },
        [](InputIdentity& e) { e.colour_contract.auto_exposure = false; },
        [](InputIdentity& e) { ++e.colour_contract.pre_exposure; },
        [](InputIdentity& e) { ++e.colour_contract.exposure_scale; },
        [](InputIdentity& e) { ++e.colour_contract.exposure_resource.resource.incarnation; },
        [](InputIdentity& e) { ++e.colour_contract.exposure_resource.generation; },
        [](InputIdentity& e) { ++e.colour_contract.exposure_shape.sample_count; },
        [](InputIdentity& e) { e.inverted_depth = false; },
    };
    for (auto change : changes) {
        RetainedJobs q;
        auto r = q.reserve(id(1), lease(), inputs());
        assert(q.split(r, list(81)) && q.finished(id(1), list(82)));
        auto state = current();
        change(state.expected[0]);
        Fake f;
        auto result = q.execute(list(82), false, state, f);
        assert(submitted(result));
        expect(f, {"list81", "fallback0", "list82"});
        assert(q.retire(result.submission));
    }
    using Invalid = void (*)(RenderInputs&);
    const Invalid invalidations[] = {
        [](RenderInputs& r) { r.identity.colour.sample_count = 0; },
        [](RenderInputs& r) { r.identity.depth.sample_count = 2; },
        [](RenderInputs& r) { r.identity.depth_view.dimension = DepthViewDimension::texture2d_ms; },
        [](RenderInputs& r) { r.identity.motion.array_size = 0; },
        [](RenderInputs& r) { r.identity.output.mip_levels = 0; },
        [](RenderInputs& r) { r.identity.depth_view.most_detailed_mip = 1; },
        [](RenderInputs& r) { r.identity.depth_view.first_array_slice = 1; },
        [](RenderInputs& r) { ++r.identity.depth_selection.generation; },
        [](RenderInputs& r) { ++r.identity.colour_selection.resource.incarnation; },
        [](RenderInputs& r) { r.identity.device = {}; },
        [](RenderInputs& r) { r.device_owner.reset(); },
        [](RenderInputs& r) { r.depth_view_owner.reset(); },
        [](RenderInputs& r) { r.identity.jitter.phase = 32; },
        [](RenderInputs& r) { r.identity.motion_contract.scale_x = 0; },
        [](RenderInputs& r) { r.identity.colour_contract.pre_exposure = 0; },
        [](RenderInputs& r) { r.identity.colour_contract.exposure_scale = 0; },
        [](RenderInputs& r) { r.identity.colour_contract.auto_exposure = false; },
        [](RenderInputs& r) { r.identity.colour_contract.exposure = ExposureSource::texture; },
    };
    for (auto invalidate : invalidations) {
        auto r = inputs();
        invalidate(r);
        RetainedJobs q;
        assert(!valid(r) && !q.reserve(id(1), lease(), r));
    }
}
void texture_exposure_lease() {
    int destroyed = 0;
    auto r = inputs();
    r.identity.colour_contract.exposure = ExposureSource::texture;
    r.identity.colour_contract.auto_exposure = false;
    r.identity.colour_contract.exposure_resource = {id(115), 8};
    r.identity.colour_contract.exposure_shape = {1, 1, 0, 0, 1, 1, 21};
    r.exposure_owner = lease(&destroyed);
    auto state = current();
    state.expected[0] = r.identity;
    RetainedJobs q;
    auto reservation = q.reserve(id(1), lease(), r);
    assert(reservation && q.split(reservation, list(83)) && q.finished(id(1), list(84)));
    r = {};
    Fake f;
    auto result = q.execute(list(84), false, state, f);
    assert(submitted(result) && destroyed == 0);
    expect(f, {"list83", "depth0", "evaluate0", "list84"});
    assert(q.retire(result.submission) && destroyed == 1);
}
void allocation_failure_reentry() {
    // Sweep reserve's allocation points, then reach success. Failures must leave
    // no occupied slot, and destroy the sole moved context owner outside the lock.
    bool reached_success = false;
    unsigned failures = 0;
    for (int fail_after = 0; fail_after < 8 && !reached_success; ++fail_after) {
        RetainedJobs q;
        auto r = inputs();
        unsigned releases = 0;
        Lease sole_context(new int(1), [&](const void* p) {
            ++releases;
            auto nested = q.reserve(id(998), lease(), r);
            assert(nested && q.cancel(nested)); // deadlocks if deleter runs under queue mutex
            delete static_cast<const int*>(p);
        });
        allocation_failure::remaining = fail_after;
        try {
            auto reserved = q.reserve(id(1), std::move(sole_context), r);
            allocation_failure::remaining = -1;
            assert(reserved && !sole_context);
            assert(q.cancel(reserved));
            reached_success = true;
        } catch (const std::bad_alloc&) {
            allocation_failure::remaining = -1;
            ++failures;
            assert(!sole_context);
            auto retried = q.reserve(id(1), lease(), r);
            assert(retried && q.cancel(retried));
        }
        assert(releases == 1);
    }
    assert(reached_success && failures >= 2);
}
void cross_chain_reentry() {
    RetainedJobs q;
    auto a = q.reserve(id(1), lease(), inputs(0));
    auto b = q.reserve(id(2), lease(), inputs(1));
    assert(q.split(a, list(81)) && q.finished(id(1), list(82)));
    assert(q.split(b, list(91)) && q.finished(id(2), list(92)));
    struct SharedDepth final : Executor {
        RetainedJobs& queue;
        int depth = -1, busy_lease_destroyed = 0;
        std::vector<std::string> events;
        std::vector<bool> restores;
        RetainedJobs::ExecuteResult deferred, unknown;
        explicit SharedDepth(RetainedJobs& value) : queue(value) {}
        void native(const List& l, bool restore) noexcept override {
            events.push_back("list" + std::to_string(l.id.incarnation));
            restores.push_back(restore);
            if (l.id == id(81)) {
                depth = 0; // This prefix produced left depth in the SHARED native texture.
                auto incoming = list(92, &busy_lease_destroyed);
                deferred = queue.execute(incoming, true, current(), *this);
                incoming = {};
                assert(deferred.status == RetainedJobs::ExecuteStatus::busy);
                assert(!deferred.submission && deferred.deferred.id == id(92));
                assert(deferred.deferred.owner && deferred.restore && busy_lease_destroyed == 0);
                // Unknown lists can overwrite depth too: busy MUST NOT mean native miss.
                unknown = queue.execute(list(99), false, current(), *this);
                assert(unknown.status == RetainedJobs::ExecuteStatus::busy && unknown.deferred.owner);
                assert(depth == 0); // B's native prefix has not interleaved.
            } else if (l.id == id(91)) depth = 1;
        }
        bool snapshot_depth(const RenderInputs& r) noexcept override {
            assert(depth == static_cast<int>(r.identity.key.eye));
            events.push_back("depth" + std::to_string(depth));
            return true;
        }
        bool evaluate(const RenderInputs& r) noexcept override {
            assert(depth == static_cast<int>(r.identity.key.eye));
            events.push_back("evaluate" + std::to_string(depth));
            return true;
        }
        bool fallback_only(const Lease&) noexcept override { assert(false);return false; }
        bool fallback(const RenderInputs&) noexcept override { assert(false);return false; }
    } f(q);
    auto first = q.execute(list(82), false, current(), f);
    assert(submitted(first) && f.depth == 0 && f.busy_lease_destroyed == 0);
    // The adapter stores busy requests in arrival order and retries after unwind.
    auto second = q.execute(std::move(f.deferred.deferred), f.deferred.restore, current(), f);
    assert(submitted(second) && f.depth == 1 && f.busy_lease_destroyed == 1);
    auto unknown = q.execute(std::move(f.unknown.deferred), f.unknown.restore, current(), f);
    assert(missed(unknown)); // Only now may adapter forward the unknown native list.
    f.native(unknown.deferred, unknown.restore); // Miss result still owns a moved retry.
    assert(f.events == std::vector<std::string>({"list81", "depth0", "evaluate0", "list82",
                                               "list91", "depth1", "evaluate1", "list92", "list99"}));
    assert(f.restores == std::vector<bool>({false, false, true, true, false}));
    assert(missed(q.execute(list(82), false, current(), f)));
    assert(missed(q.execute(list(92), false, current(), f)));
    assert(q.retire(first.submission) && q.retire(second.submission));
}
SetupObservation setup(unsigned eye,std::uint32_t counter) {
    SetupObservation s;
    s.pass.view=100+eye;s.pass.counter=counter;s.pass.jitter_uv={.1f,.2f,-.1f,-.2f};
    s.pass.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    s.allocation=50;s.camera_owner={60,61,0};s.eye=eye;s.setup_completed=true;
    s.submitted_projection_present=true;s.submitted_projection=s.pass.projection;
    return s;
}
void native_pair_receipts() {
    NativePairBook book;
    auto left=setup(0,0),right=setup(1,0);
    assert(book.publish(left) && !book.resolve(0,left.pass));
    assert(book.publish(right));
    const auto l=book.resolve(0,left.pass),r=book.resolve(1,right.pass);
    assert(l && r && l->pair==r->pair && l->frame==r->frame && l->serial!=r->serial);
    assert(l->setup.pass.counter==0); // Raw zero is not the missing-receipt sentinel.
    auto torn=left.pass;++torn.counter;assert(!book.resolve(0,torn));
    torn=left.pass;torn.projection[2]=1;assert(!book.resolve(0,torn));
    torn=left.pass;torn.jitter_uv[2]=1;assert(!book.resolve(0,torn));
    assert(!book.publish(left) && !book.resolve(1,right.pass)); // repeated Setup is ambiguous
    left=setup(0,1);right=setup(1,1);assert(book.publish(left) && book.publish(right));
    const auto next=book.resolve(0,left.pass);assert(next && next->pair>l->pair && next->owner_generation>l->owner_generation);
    auto changed=setup(0,2);changed.camera_owner[2]=1;
    assert(book.publish(changed) && !book.resolve(1,right.pass));
    auto changed_right=setup(1,2);changed_right.camera_owner=changed.camera_owner;
    assert(book.publish(changed_right) && book.resolve(0,changed.pass));
    book.invalidate();left=setup(0,0xffffffffu);right=setup(1,0xffffffffu);
    assert(book.publish(left) && book.publish(right));const auto generation=book.generation();
    left=setup(0,0);right=setup(1,0);assert(book.publish(left) && book.publish(right));
    assert(book.generation()==generation && book.resolve(0,left.pass)); // natural 32-bit wrap
    auto incomplete=setup(0,1);incomplete.setup_completed=false;assert(!book.publish(incomplete));
    book.invalidate();left=setup(0,9);right=setup(1,9);right.pass.view=left.pass.view;
    assert(book.publish(left) && !book.publish(right));
}
void mixed_temporal_and_fallback_nodes() {
    RetainedJobs q;
    int destroyed=0;
    auto first=q.reserve(id(1),lease(),inputs());
    assert(first && q.split(first,list(201)));
    // No invented eye/frame/device/resource receipt is required for legacy work.
    auto second=q.reserve_fallback(id(1),lease(),lease(&destroyed));
    assert(second && q.split(second,list(202)) && q.finished(id(1),list(203)));
    assert(!q.reserve_fallback(id(2),lease(),{}));
    Fake f;auto result=q.execute(list(203),true,current(),f);
    assert(submitted(result) && result.pair_state==PairState::fallback);
    expect(f,{"list201","depth0","evaluate0","list202","legacy_fallback","list203"});
    assert(f.restores==std::vector<bool>({true,true,true}) && destroyed==0);
    assert(q.retire(result.submission) && destroyed==1);
    // Stale temporal input cannot discard the fallback-only/native part of a chain.
    auto third=q.reserve_fallback(id(1),lease(),lease());assert(q.split(third,list(204)));
    auto fourth=q.reserve(id(1),lease(),inputs(1));assert(q.split(fourth,list(205)) && q.finished(id(1),list(206)));
    auto disabled=current();disabled.backend_enabled=false;
    Fake stale;auto completed=q.execute(list(206),false,disabled,stale);
    expect(stale,{"list204","legacy_fallback","list205","fallback1","list206"});
    assert(completed.pair_state==PairState::fallback);
    assert(q.retire(completed.submission));
}
void whole_resource_copy_contract() {
    auto a=inputs().identity.colour;a.x=a.y=0;a.rect_width=a.width;a.rect_height=a.height;
    auto b=a;assert(copy_compatible(a,b));
    b.format=99;assert(!copy_compatible(a,b)); // Conversion must be explicit.
    b=a;b.rect_width-=1;assert(!copy_compatible(a,b)); // CopyResource cannot crop.
    b=a;b.sample_count=2;assert(!copy_compatible(a,b));
    b=a;b.mip_levels=2;assert(!copy_compatible(a,b));
    b=a;b.array_size=2;assert(!copy_compatible(a,b));
}
void completion_quarantine_and_pair_results() {
    {
        RetainedJobs q;int destroyed=0;
        auto receipt=inputs();receipt.feature_owner=lease(&destroyed);
        auto reservation=q.reserve(id(1),lease(),receipt);receipt={};
        assert(q.split(reservation,list(301)) && q.finished(id(1),list(302)));
        Fake f;auto submitted_work=q.execute(list(302),false,current(),f);
        assert(submitted(submitted_work) && destroyed==0);
        assert(!q.retire(submitted_work.submission,RetainedJobs::Completion::pending) && destroyed==0);
        assert(!q.retire(submitted_work.submission,RetainedJobs::Completion::failed) && destroyed==0);
        assert(!q.retire(submitted_work.submission,RetainedJobs::Completion::completed) && destroyed==0);
        assert(q.retire(submitted_work.submission,RetainedJobs::Completion::device_abandoned) && destroyed==1);
    }
    for(int fail_eye=-1;fail_eye<2;++fail_eye) {
        RetainedJobs q;
        auto left=q.reserve(id(1),lease(),inputs(0)),right=q.reserve(id(2),lease(),inputs(1));
        assert(q.split(left,list(311)) && q.finished(id(1),list(312)));
        assert(q.split(right,list(321)) && q.finished(id(2),list(322)));
        Fake f;f.evaluate_ok=fail_eye!=0;
        auto a=q.execute(list(312),true,current(),f);
        assert(a.pair_state==(fail_eye==0?PairState::fallback:PairState::incomplete));
        f.evaluate_ok=fail_eye!=1;
        auto b=q.execute(list(322),false,current(),f);
        assert(b.pair_state==(fail_eye<0?PairState::coherent:PairState::fallback));
        assert(q.retire(a.submission) && q.retire(b.submission));
    }
    {
        RetainedJobs q;auto first=q.reserve(id(1),lease(),inputs(0));
        assert(q.split(first,list(331)) && q.finished(id(1),list(332)));
        Fake f;f.evaluate_ok=false;f.fallback_ok=false;
        auto failed=q.execute(list(332),false,current(),f);
        expect(f,{"list331","depth0","evaluate0","fallback0","list332"});
        assert(failed.pair_state==PairState::failed && q.retire(failed.submission));
    }
    PairPublication ledger;auto left=inputs(0).identity,right=inputs(1).identity;
    assert(ledger.observe(left,EyeOutcome::temporal)==PairState::incomplete);
    ++right.generations.feature;
    assert(ledger.observe(right,EyeOutcome::temporal)==PairState::fallback); // mixed feature generations
}
void reused_addresses() {
    RetainedJobs q;
    auto a = q.reserve(id(1), lease(), inputs());
    assert(q.split(a, list(71)) && q.finished(id(1), list(72)));
    Fake f;
    auto old = q.execute(list(72), false, current(), f);
    assert(q.retire(old.submission));
    auto b = q.reserve(id(1), lease(), inputs());
    List new_prefix{{71, 171}, lease()}, new_remainder{{72, 172}, lease()};
    assert(q.split(b, new_prefix) && q.finished(id(1), new_remainder));
    assert(missed(q.execute(list(72), false, current(), f))); // old raw address cannot match
    auto fresh = q.execute(new_remainder, true, current(), f);
    assert(submitted(fresh) && !q.retire(old.submission) && q.retire(fresh.submission));
    expect(f, {"list71", "depth0", "evaluate0", "list72", "list171", "depth0", "evaluate0", "list172"});
}
}
int main() {
    static_assert(!std::is_constructible<bool, RetainedJobs::ExecuteResult>::value,
                  "busy cannot silently take native-on-miss boolean paths");
    static_assert(!std::is_copy_constructible<RetainedJobs>::value, "queue ownership is unique");
    phases_and_lifetime(false);
    phases_and_lifetime(true);
    chains_and_contexts();
    rejection_and_capacity();
    stale_fallbacks();
    sealed_capacity_and_cancelled_chain();
    complete_receipt_freshness();
    texture_exposure_lease();
    allocation_failure_reentry();
    cross_chain_reentry();
    native_pair_receipts();
    mixed_temporal_and_fallback_nodes();
    whole_resource_copy_contract();
    completion_quarantine_and_pair_results();
    reused_addresses();
    std::cout << "temporal_eye_job_check: passed\n";
}
