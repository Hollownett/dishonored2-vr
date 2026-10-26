#include "../src/alternate_eye.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

struct Pose {int id{};};  // stands in for XrView: a pose + FOV tagged with the render it belongs to

// A frame loop the way the probe drives it: plan, then record the image(s) the plan produced.
static int step(AlternateEye& a, std::uint64_t f, bool gameplay = true, bool authored = false, bool epoch = false) {
    const int e = a.eye_for_frame(f, gameplay, authored, epoch);
    if (e < 0) {if (gameplay) {a.rendered(0, f); a.rendered(1, f);}}
    else a.rendered(e, f);
    return e;
}

static void parity_alternates_in_gameplay() {
    AlternateEye a;
    CHECK(step(a, 1) == -1);  // first frame: both eyes unknown, paired
    int expect = 0;           // frame 2 -> left
    for (std::uint64_t f = 2; f < 40; ++f) {
        CHECK(step(a, f) == expect);
        CHECK(expect == static_cast<int>(f & 1u));
        expect = 1 - expect;
    }
    CHECK(a.eye_for_frame(39, true, false, false) == 1);  // idempotent per frame
    CHECK(a.eye_for_frame(39, false, true, true) == 1);
}
static void menus_and_authored_render_paired() {
    AlternateEye a;
    step(a, 1); step(a, 2); step(a, 3);
    CHECK(step(a, 4, false, false) == -1);  // menu / loading / settings panel (gameplay false)
    CHECK(step(a, 5, true, true) == -1);    // takedown, cutscene
    CHECK(step(a, 6, true, true) == -1);
    AlternateEye b;  // authored scene entered mid-alternation, then left
    for (std::uint64_t f = 1; f < 10; ++f) step(b, f);
    for (std::uint64_t f = 10; f < 15; ++f) CHECK(step(b, f, true, true) == -1);
    CHECK(b.can_submit(0, 14) && b.can_submit(1, 14));  // paired authored frames keep both eyes fresh
    CHECK(step(b, 15, true, false) == -1);              // the first free frame after the cut is paired (nothing from the cut is reused)
    CHECK(step(b, 16, true, false) == 0);               // then alternation resumes
    // a menu spell leaves no fresh image: the first gameplay frame is paired, not a half-stale one
    AlternateEye c;
    for (std::uint64_t f = 1; f < 6; ++f) step(c, f);
    for (std::uint64_t f = 6; f < 12; ++f) step(c, f, false);
    CHECK(step(c, 12, true, false) == -1);
    CHECK(step(c, 13, true, false) == 1);
}
static void epoch_change_renders_paired_once() {
    AlternateEye a;
    for (std::uint64_t f = 1; f < 8; ++f) step(a, f);
    CHECK(step(a, 8, true, false, true) == -1);  // recentre / reset of the pose epoch
    CHECK(step(a, 9) == 1);                       // then alternation resumes
    CHECK(step(a, 10) == 0);
    CHECK(step(a, 11) == 1);
    AlternateEye b;
    for (std::uint64_t f = 1; f < 8; ++f) step(b, f);
    b.reset();
    CHECK(step(b, 8) == -1);
    CHECK(step(b, 9) == 1);
    // a hitch (frames not rendered) also gets one paired frame
    AlternateEye c;
    for (std::uint64_t f = 1; f < 8; ++f) step(c, f);
    CHECK(step(c, 12) == -1);
    CHECK(step(c, 13) == 1);
}
static void eye_older_than_two_frames_not_submitted() {
    AlternateEye a;
    CHECK(!a.can_submit(0, 1));  // never rendered
    a.rendered(0, 10);
    CHECK(a.can_submit(0, 10));
    CHECK(a.can_submit(0, 11));
    CHECK(a.can_submit(0, 12));
    CHECK(!a.can_submit(0, 13));
    CHECK(!a.can_submit(0, 9));   // an image from the future is not an image
    CHECK(!a.can_submit(2, 10));
    CHECK(!a.can_submit(-1, 10));
    a.reset();
    CHECK(!a.can_submit(0, 10));
}
static void stale_eye_keeps_its_render_pose() {
    using R = EyeRecord<Pose>;
    std::array<R, 2> rec{};
    rec[0] = R{Pose{100}, 10, 7, true};  // left rendered at frame 10
    rec[1] = R{Pose{101}, 11, 7, true};  // right rendered at frame 11
    auto v = build_layer_views(rec, 11, 7);
    CHECK(v.ok && v.views[0].id == 100 && v.views[1].id == 101);
    rec[0] = R{Pose{102}, 12, 7, true};  // left re-rendered at 12; the right keeps its own pose
    v = build_layer_views(rec, 12, 7);
    CHECK(v.ok && v.views[0].id == 102 && v.views[1].id == 101);
    v = build_layer_views(rec, 13, 7);   // right is now 2 old: still fine
    CHECK(v.ok && v.views[1].id == 101);
    v = build_layer_views(rec, 14, 7);   // right is 3 old: never submitted
    CHECK(!v.ok);
    v = build_layer_views(rec, 12, 8);   // a different pose epoch: not submittable
    CHECK(!v.ok);
    rec[1].valid = false;
    CHECK(!build_layer_views(rec, 12, 7).ok);
    CHECK(!build_layer_views(std::array<R, 2>{}, 1, 0).ok);  // nothing recorded
}
// --- plan F fix round 1: authored / settings onset and exit, and the glue decisions -----------------------------------------------------
struct State {bool gameplay{true}, authored{false}, settings{false};};
struct V {int made{}; bool cut{};};
// Mirrors the probe + GameXr with the one-frame engine pipeline: Present f reads its own state, then plans display frame f+2 from it; the
// Setup for f+2 is made in the interval that ends at Present f+1 (one view for the planned eye, else two) and shown at Present f+2.
struct Sim {
    AlternateEye plan;
    PendingViews<V> pending;
    std::array<EyeRecord<V>, 2> kept{};
    std::array<bool, 64> submitted{};
    void run(const std::array<State, 64>& st, int last) {
        for (int f = 1; f <= last; ++f) {
            const State& now = st[static_cast<std::size_t>(f)];
            const std::uint64_t display = static_cast<std::uint64_t>(f);
            const bool cut = now.authored || now.settings;
            const auto slot = pending.take(display);
            const auto ready = alternate_present_readiness(slot.has_value(), slot ? slot->mask : 0u, 1, 1, true, true, true,
                                                           !alternate_single_view_allowed(now.authored, now.settings));
            const int fresh = ready.fresh_eye;
            if (ready.ready) {
                const unsigned mask = slot->mask;
                auto eff = kept;
                for (int e = 0; e < 2; ++e) if (mask & (1u << e))
                    eff[static_cast<std::size_t>(e)] = EyeRecord<V>{slot->view[static_cast<std::size_t>(e)], display, 1, true};
                const bool tracked = alternate_pair_ready(mask, fresh, build_layer_views(eff, display, 1).ok);
                if (tracked) {
                    submitted[static_cast<std::size_t>(f)] = true;
                    for (int e = 0; e < 2; ++e) {
                        const V& v = eff[static_cast<std::size_t>(e)].view;
                        CHECK(v.cut == cut);                       // a free frame never carries a cut image and a cut frame never a pre-cut one
                        CHECK(!cut || v.made == f);                // during a cut both eyes are rendered for this very frame
                        CHECK(f - v.made <= 2);
                    }
                }
                for (int e = 0; e < 2; ++e) if (mask & (1u << e)) {
                    if (alternate_may_keep(now.gameplay, now.authored, now.settings)) {
                        kept[static_cast<std::size_t>(e)] = eff[static_cast<std::size_t>(e)];
                        plan.rendered(e, display);
                    } else kept[static_cast<std::size_t>(e)] = {};
                }
                if (cut && now.gameplay) CHECK(!kept[0].valid && !kept[1].valid);  // cut images are never kept
            }
            // after this Present: the plan for the frame the next Setup shows, from this Present's state (the lag the glue really has)
            const std::uint64_t next = display + 2;
            const int planned = plan.eye_for_frame(next, alternate_free_gameplay(now.gameplay, now.settings), now.authored, false);
            const State& then = st[static_cast<std::size_t>(next)];
            const bool cut_then = then.authored || then.settings;
            if (planned >= 0) pending.record(next, planned, V{static_cast<int>(next), cut_then}, 1, 0, 0);
            else for (int e = 0; e < 2; ++e) pending.record(next, e, V{static_cast<int>(next), cut_then}, 1, 0, 0);
        }
    }
};
static void authored_onset_never_pairs_stale_eye() {
    for (const bool settings : {false, true}) {
        std::array<State, 64> st{};
        for (int f = 10; f < 15; ++f) {st[static_cast<std::size_t>(f)].authored = !settings; st[static_cast<std::size_t>(f)].settings = settings;}
        Sim sim;
        sim.run(st, 30);
        CHECK(!sim.submitted[10] && !sim.submitted[11]);   // the onset frames were rendered single-view from the lagging plan (two frames of
        CHECK(sim.submitted[9] && sim.submitted[12]);         // pipeline): dropped, never a fresh cut eye paired with a stale free one
    }
}
static void authored_exit_never_reuses_cut_eye() {
    for (const bool settings : {false, true}) {
        std::array<State, 64> st{};
        for (int f = 10; f < 15; ++f) {st[static_cast<std::size_t>(f)].authored = !settings; st[static_cast<std::size_t>(f)].settings = settings;}
        Sim sim;
        sim.run(st, 30);
        CHECK(sim.submitted[15] && sim.submitted[16] && sim.submitted[20]);  // submission resumes at once, with free images only (checked in run)
    }
    // the plan alone: even when the cut frames' images were recorded as rendered, the first free frame is paired
    AlternateEye a;
    for (std::uint64_t f = 1; f < 10; ++f) step(a, f);
    for (std::uint64_t f = 10; f < 13; ++f) step(a, f, true, true);
    CHECK(step(a, 13, true, false) == -1);
    AlternateEye b;
    for (std::uint64_t f = 1; f < 10; ++f) step(b, f);
    for (std::uint64_t f = 10; f < 12; ++f) {b.eye_for_frame(f, false, false, false); b.rendered(0, f); b.rendered(1, f);}  // settings panel open
    CHECK(b.eye_for_frame(12, true, false, false) == -1);
}
static void gating_decisions() {
    CHECK(alternate_free_gameplay(true, false) && !alternate_free_gameplay(true, true) && !alternate_free_gameplay(false, false) && !alternate_free_gameplay(false, true));
    CHECK(alternate_single_view_allowed(false, false) && !alternate_single_view_allowed(true, false) && !alternate_single_view_allowed(false, true) && !alternate_single_view_allowed(true, true));
    CHECK(alternate_may_keep(true, false, false) && !alternate_may_keep(false, false, false) && !alternate_may_keep(true, true, false) && !alternate_may_keep(true, false, true));
}
static void fresh_eye_and_need_mask() {
    CHECK(alternate_mask_fresh_eye(1u) == 0 && alternate_mask_fresh_eye(2u) == 1 && alternate_mask_fresh_eye(3u) == -1 && alternate_mask_fresh_eye(0u) == -1);
    CHECK(alternate_need_mask(-1) == 3 && alternate_need_mask(0) == 1 && alternate_need_mask(1) == 2);
    CHECK(alternate_pair_ready(1, 0, true) && !alternate_pair_ready(0, 0, true) && !alternate_pair_ready(2, 0, true));
    CHECK(alternate_pair_ready(2, 1, true) && !alternate_pair_ready(1, 1, true) && !alternate_pair_ready(0, 1, true));
    CHECK(alternate_pair_ready(3, -1, true) && !alternate_pair_ready(1, -1, true) && !alternate_pair_ready(2, -1, true) && !alternate_pair_ready(0, -1, true));
    CHECK(!alternate_pair_ready(3, -1, false) && !alternate_pair_ready(1, 0, false));
}
static void mismatch_falls_back_after_thirty() {
    AlternateMismatch m;
    for (int i = 0; i < 29; ++i) CHECK(!m.note(true));
    CHECK(m.note(true));            // the 30th consecutive
    AlternateMismatch r;
    for (int round = 0; round < 5; ++round) {for (int i = 0; i < 29; ++i) CHECK(!r.note(true)); CHECK(!r.note(false));}  // a good frame resets the run
    r.reset();
    for (int i = 0; i < 29; ++i) CHECK(!r.note(true));
    CHECK(r.note(true));
}
static void temporal_aa_requires_verified_alternate_history() {
    for (int requested = 0; requested <= 8; ++requested) {
        CHECK(alternate_effective_temporal(true, requested, true) == (requested == 1 ? 1 : 0));
        CHECK(alternate_effective_temporal(true, requested) == 0);          // TAA / FSR 3 / FSR 4 all become the SMAA row
        CHECK(alternate_effective_temporal(false, requested) == requested);  // every other mode keeps what was asked
        CHECK(alternate_effective_temporal(true, requested, true, true) == requested);
        CHECK(alternate_effective_temporal(true, requested, false, true) == 0); // opt-in never bypasses native ownership
        CHECK(alternate_smaa_applies(true, requested) == (requested < 2));
        CHECK(alternate_smaa_applies(false, requested) == (requested < 2));
    }
}
static void per_eye_state_follows_the_logical_eye() {
    CHECK(alternate_state_slot(0, 0) == 0 && alternate_state_slot(1, 0) == 1);   // the one view is native slot 0 whichever eye it renders
    CHECK(alternate_state_slot(-1, 0) == 0 && alternate_state_slot(-1, 1) == 1); // paired / knob off: the native slot, unchanged
}
// --- 2026-10-22 double vision: the engine pipeline is ONE frame deep -----------------------------------------------------------------------
// NativeEyeSources matches a Begin only against the Setup published one Present earlier, and a Begin's image is on screen at the Present
// that ends its interval: the Setup made while the counter reads F is shown at Present F+2. Task 1 stamped it for F+1, so each swapchain
// got the OTHER eye's image (the sim's left/right disparity had the reversed sign; the headset showed double vision).
static void setup_image_shows_one_present_later() {
    CHECK(alternate_pipeline_depth == 1);
    CHECK(alternate_display_frame(5) == 7 && alternate_begin_display_frame(6) == 7);  // Setup at counter 5, Begin at counter 6: the same image, Present 7
    CHECK(alternate_display_frame(0) == 2);                                           // the very first Setup (counter 0) is shown at Present 2
    PendingViews<Pose> p;
    p.record(alternate_display_frame(5), 1, Pose{51}, 3, 100, 200);
    CHECK(!p.take(6));                                      // the next Present does not show it
    auto s = p.take(7);
    CHECK(s && s->mask == 2 && s->view[1].id == 51 && s->epoch == 3 && s->display_time[1] == 100 && s->locate_cpu_time[1] == 200);
    p.record(alternate_display_frame(6), 0, Pose{60}, 3, 0, 0);  // the Setup of the next interval (display 8) leaves display 7 alone
    CHECK(p.take(7) && p.take(7)->view[1].id == 51 && p.take(8) && p.take(8)->mask == 1 && p.take(8)->view[0].id == 60);
    p.record(9, 0, Pose{90}, 3, 0, 0);                           // two Setups later the slot is reused
    CHECK(!p.take(7) && p.take(9) && p.take(9)->view[0].id == 90);
    p.record(10, 0, Pose{100}, 4, 0, 0); p.record(10, 1, Pose{101}, 4, 0, 0);  // a paired frame: both views in one slot
    CHECK(p.take(10) && p.take(10)->mask == 3 && p.take(10)->view[0].id == 100 && p.take(10)->view[1].id == 101 && p.take(10)->epoch == 4);
    p.record(11, 2, Pose{1}, 4, 0, 0); p.record(0, 0, Pose{1}, 4, 0, 0);  // not an eye / not a frame: ignored
    CHECK(!p.take(11) && !p.take(0));
    p.reset();
    CHECK(!p.take(9) && !p.take(10));
}
static void present_readiness_from_the_setup_slot() {
    auto r = alternate_present_readiness(true, 1u, 7, 7, true, true, false, false);
    CHECK(r.ready && r.single && r.fresh_eye == 0);
    r = alternate_present_readiness(true, 2u, 7, 7, true, true, false, false);
    CHECK(r.ready && r.single && r.fresh_eye == 1);
    r = alternate_present_readiness(true, 3u, 7, 7, true, false, true, false);   // paired: both Begins done, left copied out; the mono match is irrelevant
    CHECK(r.ready && !r.single && r.fresh_eye == -1);
    r = alternate_present_readiness(true, 3u, 7, 7, true, true, false, false);   // paired, pair incomplete
    CHECK(!r.ready && !r.single && r.fresh_eye == -1);
    r = alternate_present_readiness(true, 1u, 7, 7, true, false, true, false);   // one view, its Begin not matched for this Present
    CHECK(!r.ready && r.single && r.fresh_eye == -1);
    r = alternate_present_readiness(true, 1u, 7, 7, true, true, true, true);     // one view in a cut / settings panel: dropped
    CHECK(!r.ready && r.single && r.fresh_eye == -1);
    r = alternate_present_readiness(true, 3u, 7, 7, true, false, true, true);    // a pair in a cut is fine
    CHECK(r.ready && !r.single);
    r = alternate_present_readiness(true, 1u, 7, 8, true, true, true, false);    // recentred since the Setup
    CHECK(!r.ready && r.single);
    r = alternate_present_readiness(true, 1u, 7, 7, false, true, true, false);   // stereo off
    CHECK(!r.ready);
    r = alternate_present_readiness(false, 1u, 7, 7, true, true, true, false);   // no Setup recorded for this Present
    CHECK(!r.ready && !r.single && r.fresh_eye == -1);
    r = alternate_present_readiness(true, 0u, 7, 7, true, true, true, false);
    CHECK(!r.ready && !r.single);
    r = alternate_present_readiness(true, 4u, 7, 7, true, true, true, false);
    CHECK(!r.ready && !r.single);
}
static void planner_counts_the_setup_in_flight() {
    // Present P keeps parity(P)'s image and plans display P+2. The other eye was last kept at P-1 (age 3 at P+2), but the Setup planned for
    // P+1 renders it: alternation continues without a paired refresh.
    AlternateEye a;
    CHECK(a.eye_for_frame(3, true, false, false) == -1);   // planned at Present 1: nothing kept, paired
    a.rendered(0, 2);                                       // Present 2 showed the unplanned first Setup (eye 0)
    CHECK(a.eye_for_frame(4, true, false, false) == 0);    // the paired Setup for 3 is in flight: eye 1 will be fresh
    a.rendered(0, 3); a.rendered(1, 3);
    CHECK(a.eye_for_frame(5, true, false, false) == 1);
    a.rendered(0, 4);
    CHECK(a.eye_for_frame(6, true, false, false) == 0);    // eye 1 kept at 3 is too old by itself; the Setup for 5 renders it
    a.rendered(1, 5);
    CHECK(a.eye_for_frame(7, true, false, false) == 1);
    a.rendered(0, 6);
    CHECK(a.eye_for_frame(8, true, false, false) == 0);
    // Present 7 delivered nothing (dropped): the Setup in flight for 8 is not trusted, eye 1 kept at 5 is too old at 9: one paired frame
    CHECK(a.eye_for_frame(9, true, false, false) == -1);
    a.rendered(0, 8);
    CHECK(a.eye_for_frame(10, true, false, false) == 0);   // the paired Setup for 9 is in flight and Present 8 delivered
    a.rendered(0, 9); a.rendered(1, 9);
    CHECK(a.eye_for_frame(11, true, false, false) == 1);
    // an epoch change: the in-flight Setup belongs to the old epoch (its Present drops it) and the kept images are gone: paired until a
    // delivered paired frame
    CHECK(a.eye_for_frame(12, true, false, true) == -1);
    CHECK(a.eye_for_frame(13, true, false, false) == -1);  // Present 11 delivered nothing in the new epoch
    a.rendered(0, 12); a.rendered(1, 12);
    CHECK(a.eye_for_frame(14, true, false, false) == 0);
    a.rendered(0, 13); a.rendered(1, 13);
    CHECK(a.eye_for_frame(15, true, false, false) == 1);
    // a menu: nothing in flight counts afterwards (refresh), as before
    CHECK(a.eye_for_frame(16, false, false, false) == -1);
    a.rendered(0, 15);
    CHECK(a.eye_for_frame(17, true, false, false) == -1);
    a.rendered(0, 16); a.rendered(1, 16);
    CHECK(a.eye_for_frame(18, true, false, false) == 0);
}
// The probe + engine + GameXr, end to end, against an engine that delivers Setup(F) at Present F+2 (the physical pipeline, written literally
// here). `stamp_depth` is how many Presents ahead the glue believes a Setup shows: 1 = the task 1 stamping (the bug), 2 = the fix. With the
// bug the eye named fresh at a Present is never the eye of the image in its swapchain.
struct PipelineRun {int alternated{}, eye_mismatches{}, pose_mismatches{}, stale_pose_mismatches{};};
static PipelineRun run_pipeline(std::uint64_t stamp_depth) {
    struct Image {unsigned mask{}; std::array<int, 2> pose{};};
    std::array<Image, 400> shown{};        // the engine: what the backbuffer (and the copied-out left) hold at each Present
    AlternateEye plan; PendingViews<Pose> pending; std::array<EyeRecord<Pose>, 2> kept{};
    std::array<int, 2> kept_image_pose{};  // the pose id of the image each kept copy holds (for the stale-eye check)
    std::uint64_t plan_stamp = 0; bool descriptor_paired = false; int next_pose = 1;
    PipelineRun out;
    for (std::uint64_t present = 1; present <= 300; ++present) {
        const std::uint64_t counter = present - 1;                       // the Present counter during the interval that ends at this Present
        // the Setup of that interval: one view (the parity eye of the frame it believes it renders) or two, by the installed descriptor
        const std::uint64_t believed = counter + stamp_depth;
        const bool planned = (plan_stamp >> 2) == believed;
        const bool wanted_paired = planned && (plan_stamp & 3u) == 0, wanted_single = planned && (plan_stamp & 3u) != 0;
        const bool mismatch = descriptor_paired ? wanted_single : wanted_paired;
        if (!(mismatch && !descriptor_paired)) {                          // a single view disagreeing with the plan is dropped (not recorded)
            if (descriptor_paired) for (int e = 0; e < 2; ++e) {
                shown[counter + 2].mask |= 1u << e; shown[counter + 2].pose[static_cast<std::size_t>(e)] = next_pose;  // the engine, literally
                pending.record(believed, e, Pose{next_pose}, 1, 0, 0); ++next_pose;
            } else {
                const int eye = AlternateEye::parity_eye(believed);
                shown[counter + 2].mask |= 1u << eye; shown[counter + 2].pose[static_cast<std::size_t>(eye)] = next_pose;
                pending.record(believed, eye, Pose{next_pose}, 1, 0, 0); ++next_pose;
            }
        }
        // the Present: the glue promotes the slot it recorded for this Present; the engine shows what it rendered
        const Image& image = shown[present];
        const auto slot = pending.take(present);
        const auto ready = alternate_present_readiness(slot.has_value(), slot ? slot->mask : 0u, 1, 1, true,
                                                       image.mask == 1u || image.mask == 2u, image.mask == 3u, false);
        int fresh = -1;
        if (ready.ready) {
            fresh = ready.fresh_eye;
            auto eff = kept;
            for (int e = 0; e < 2; ++e) if (slot->mask & (1u << e)) eff[static_cast<std::size_t>(e)] = EyeRecord<Pose>{slot->view[static_cast<std::size_t>(e)], present, 1, true};
            const auto layers = build_layer_views(eff, present, 1);
            if (alternate_pair_ready(slot->mask, fresh, layers.ok)) {
                if (fresh >= 0) {
                    ++out.alternated;
                    const auto f = static_cast<std::size_t>(fresh), o = static_cast<std::size_t>(1 - fresh);
                    if (image.mask != (1u << fresh)) ++out.eye_mismatches;                      // the swapchain of eye `fresh` gets the image of the other eye
                    else if (layers.views[f].id != image.pose[f]) ++out.pose_mismatches;       // or an image rendered with another pose
                    if (layers.views[o].id != kept_image_pose[o]) ++out.stale_pose_mismatches;  // the stale eye: the pose of the image its copy holds
                }
                for (int e = 0; e < 2; ++e) if (slot->mask & (1u << e)) {
                    kept[static_cast<std::size_t>(e)] = eff[static_cast<std::size_t>(e)];
                    kept_image_pose[static_cast<std::size_t>(e)] = image.mask & (1u << e) ? image.pose[static_cast<std::size_t>(e)] : -1;
                    plan.rendered(e, present);
                }
            }
        }
        // after the Present: plan the Setup of the next interval and install its descriptor
        const std::uint64_t next_display = present + stamp_depth;
        const int next = plan.eye_for_frame(next_display, true, false, false);
        plan_stamp = (next_display << 2) | static_cast<std::uint64_t>(next + 1);
        descriptor_paired = next < 0;
    }
    return out;
}
static void image_shown_at_present_carries_the_submitted_eye_and_pose() {
    const auto fixed = run_pipeline(alternate_pipeline_depth + 1);
    CHECK(fixed.alternated >= 290);               // alternation is sustained (the planner trusts the Setup in flight)
    CHECK(fixed.eye_mismatches == 0 && fixed.pose_mismatches == 0 && fixed.stale_pose_mismatches == 0);
    const auto bug = run_pipeline(1);              // the task 1 stamping: the Setup for "the next Present"
    CHECK(bug.alternated > 0 && bug.eye_mismatches == bug.alternated);  // every alternated Present put the other eye's image in the swapchain
}
static void paired_frames_never_trip_the_miss_fallback() {
    unsigned m = 0;
    for (int i = 0; i < 1000; ++i) m = alternate_miss_count(m, false, false);   // a long menu / cutscene: paired, not ready
    CHECK(m == 0);
    for (int i = 0; i < 179; ++i) m = alternate_miss_count(m, true, false);     // one-view frames that miss
    CHECK(m == 179);
    m = alternate_miss_count(m, false, false);                                  // a paired frame resets the run
    CHECK(m == 0);
    for (int i = 0; i < 100; ++i) m = alternate_miss_count(m, true, false);
    m = alternate_miss_count(m, true, true);                                    // a ready frame resets it
    CHECK(m == 0);
    for (int i = 0; i < 180; ++i) m = alternate_miss_count(m, true, false);
    CHECK(m >= 180);                                                            // 180 consecutive one-view misses still fall back
}
int main() {
    paired_frames_never_trip_the_miss_fallback();
    parity_alternates_in_gameplay();
    menus_and_authored_render_paired();
    epoch_change_renders_paired_once();
    eye_older_than_two_frames_not_submitted();
    stale_eye_keeps_its_render_pose();
    authored_onset_never_pairs_stale_eye();
    authored_exit_never_reuses_cut_eye();
    gating_decisions();
    fresh_eye_and_need_mask();
    mismatch_falls_back_after_thirty();
    temporal_aa_requires_verified_alternate_history();
    per_eye_state_follows_the_logical_eye();
    setup_image_shows_one_present_later();
    present_readiness_from_the_setup_slot();
    planner_counts_the_setup_in_flight();
    image_shown_at_present_carries_the_submitted_eye_and_pose();
    std::puts("PASS: alternate eye (parity, paired gating, epoch, age, stale pose, one-frame pipeline)");
}
