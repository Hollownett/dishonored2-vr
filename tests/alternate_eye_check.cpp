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
// Mirrors the probe + GameXr: frame f is planned at Present f-1 from THAT frame's state (one frame of lag); Present f reads its own state.
struct Sim {
    AlternateEye plan;
    std::array<EyeRecord<V>, 2> kept{};
    std::array<bool, 64> submitted{};
    void run(const std::array<State, 64>& st, int last) {
        for (int f = 1; f <= last; ++f) {
            const State& prev = st[static_cast<std::size_t>(f - 1)];
            const State& now = st[static_cast<std::size_t>(f)];
            const int planned = plan.eye_for_frame(static_cast<std::uint64_t>(f), alternate_free_gameplay(prev.gameplay, prev.settings), prev.authored, false);
            const bool single = planned >= 0;
            const bool cut = now.authored || now.settings;
            bool ready = true;
            if (single && !alternate_single_view_allowed(now.authored, now.settings)) ready = false;
            const std::uint64_t stamp = (static_cast<std::uint64_t>(f) << 1) | static_cast<std::uint64_t>(AlternateEye::parity_eye(static_cast<std::uint64_t>(f)));
            const int fresh = alternate_fresh_eye(true, single, ready, stamp);
            if (single && !ready) continue;  // dropped frame: nothing submitted, nothing kept
            const unsigned mask = fresh < 0 ? 3u : 1u << fresh;
            auto eff = kept;
            for (int e = 0; e < 2; ++e) if (fresh < 0 || e == fresh)
                eff[static_cast<std::size_t>(e)] = EyeRecord<V>{V{f, cut}, static_cast<std::uint64_t>(f), 1, true};
            const bool tracked = alternate_pair_ready(mask, fresh, build_layer_views(eff, static_cast<std::uint64_t>(f), 1).ok);
            if (tracked) {
                submitted[static_cast<std::size_t>(f)] = true;
                for (int e = 0; e < 2; ++e) {
                    const V& v = eff[static_cast<std::size_t>(e)].view;
                    CHECK(v.cut == cut);                       // a free frame never carries a cut image and a cut frame never a pre-cut one
                    CHECK(!cut || v.made == f);                // during a cut both eyes are rendered for this very frame
                    CHECK(f - v.made <= 2);
                }
            }
            for (int e = 0; e < 2; ++e) if (fresh < 0 || e == fresh) {
                if (alternate_may_keep(now.gameplay, now.authored, now.settings)) {
                    kept[static_cast<std::size_t>(e)] = eff[static_cast<std::size_t>(e)];
                    plan.rendered(e, static_cast<std::uint64_t>(f));
                } else kept[static_cast<std::size_t>(e)] = {};
            }
            if (cut && now.gameplay) CHECK(!kept[0].valid && !kept[1].valid);  // cut images are never kept
        }
    }
};
static void authored_onset_never_pairs_stale_eye() {
    for (const bool settings : {false, true}) {
        std::array<State, 64> st{};
        for (int f = 10; f < 15; ++f) {st[static_cast<std::size_t>(f)].authored = !settings; st[static_cast<std::size_t>(f)].settings = settings;}
        Sim sim;
        sim.run(st, 30);
        CHECK(!sim.submitted[10]);   // the onset frame was rendered single-view from the lagging plan: dropped, not stale-paired
        CHECK(sim.submitted[9] && sim.submitted[11] && sim.submitted[12]);
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
    CHECK(alternate_fresh_eye(true, true, true, (7u << 1) | 0u) == 0);
    CHECK(alternate_fresh_eye(true, true, true, (8u << 1) | 1u) == 1);
    CHECK(alternate_fresh_eye(true, true, false, 1) == -1);   // not ready
    CHECK(alternate_fresh_eye(true, false, true, 1) == -1);   // paired path
    CHECK(alternate_fresh_eye(false, true, true, 1) == -1);   // knob off
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
int main() {
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
    std::puts("PASS: alternate eye (parity, paired gating, epoch, age, stale pose)");
}
