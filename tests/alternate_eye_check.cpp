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
    CHECK(step(b, 15, true, false) == 1);               // and alternation resumes at once
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
int main() {
    parity_alternates_in_gameplay();
    menus_and_authored_render_paired();
    epoch_change_renders_paired_once();
    eye_older_than_two_frames_not_submitted();
    stale_eye_keeps_its_render_pose();
    std::puts("PASS: alternate eye (parity, paired gating, epoch, age, stale pose)");
}
