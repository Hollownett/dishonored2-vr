// Layer-pose sync for the paired / perf-mono paths (2026-10-22): the probe's two glues against the physical engine pipeline, written
// literally: a Setup made while the Present counter reads F has both eyes' images on screen at Present F+2 (NativeEyeSources matches a
// Begin against the Setup published one Present earlier; the sim trace joins Setup F to the Begin at F+1 and the Present at F+2).
// The legacy glue wrote the Setup's views into GameXr at Setup time, so Present P submitted the Setup made at counter P-1 (its image shows
// at P+1) with the image of the Setup made at counter P-2: on every head move the layer pose was one frame ahead of its image. The sync
// glue records the views for display frame F+2 (PendingViews) and promotes the slot recorded for the Present, else the latest Setup's slot
// (what the legacy glue submitted) for that one frame, else nothing.
#include "../src/alternate_eye.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

struct View {int pose{};};  // stands in for XrView: the id of the locate (pose + FOV) a view was written from; unique per frame = a moving head

constexpr std::size_t presents = 300;
struct Scenario {
    std::array<bool, presents + 4> setup_ok{};          // the Setup made while the counter reads k wrote its views (false: it bailed)
    std::array<std::uint64_t, presents + 4> epoch{};    // the pose epoch while the counter reads k (a recentre at Present R: epoch[k >= R] differs)
    Scenario() {setup_ok.fill(true); epoch.fill(1);}
};
struct Run {int shown{}, matched{}, skewed{}, untracked{}, own{}, latest{};};

// The game-side `pose` of an image: the Setup's locate id, or -1 for a view the mod did not write (its Setup bailed; the engine still renders).
static Run run_pipeline(bool sync, const Scenario& sc) {
    std::array<int, presents + 4> image{};
    PendingViews<View> pending;
    std::array<View, 2> rendered_views{}; unsigned rendered_mask = 0;   // GameXr: legacy writes it at Setup, sync at promotion; present() clears it
    int next_pose = 1; Run out;
    for (std::uint64_t present = 1; present <= presents; ++present) {
        const std::uint64_t counter = present - 1;   // the Present counter during the interval that ends at this Present
        // the Setup of that interval: both eyes from the locate made at Present `counter`, shown at Present counter+2 (the engine, literally)
        if (sc.setup_ok[counter]) {
            image[counter + 2] = next_pose;   // the engine, literally: shown two Presents after the counter value the Setup saw
            for (int e = 0; e < 2; ++e) {
                const View view{next_pose};
                if (sync) pending.record(alternate_display_frame(counter), e, view, sc.epoch[counter], 0, 0);   // the glue's belief (depth 1 = F+2)
                else {rendered_views[static_cast<std::size_t>(e)] = view; rendered_mask |= 1u << e;}
            }
            ++next_pose;
        } else image[counter + 2] = -1;
        // the Present: both Begins of the interval completed (pair_ready) once the engine has rendered anything at all
        const bool pair_ready = image[present] != 0;
        if (sync && pair_ready) {
            const auto own = pending.take(present), latest = pending.take(present + 1);
            const auto choice = paired_slot_to_promote(own.has_value(), own ? own->mask : 0u, own ? own->epoch : 0,
                                                       latest.has_value(), latest ? latest->mask : 0u, latest ? latest->epoch : 0, sc.epoch[present]);
            const auto* slot = choice == PairedSlot::own ? &*own : choice == PairedSlot::latest ? &*latest : nullptr;
            if (choice == PairedSlot::own) ++out.own; else if (choice == PairedSlot::latest) ++out.latest;
            if (slot) for (std::size_t e = 0; e < 2; ++e) {rendered_views[e] = slot->view[e]; rendered_mask |= 1u << e;}
        }
        if (pair_ready) {
            ++out.shown;
            if (rendered_mask != 3u) ++out.untracked;
            else if (rendered_views[0].pose == image[present] && rendered_views[1].pose == image[present]) ++out.matched;
            else ++out.skewed;   // the layer carries another locate than its image
        }
        rendered_mask = 0;
    }
    return out;
}

static void legacy_glue_skews_every_frame_sync_glue_none() {
    const Scenario steady;
    const auto legacy = run_pipeline(false, steady);
    CHECK(legacy.shown >= 290 && legacy.matched == 0 && legacy.skewed == legacy.shown);   // one frame ahead on EVERY Present
    const auto sync = run_pipeline(true, steady);
    CHECK(sync.shown == legacy.shown && sync.skewed == 0 && sync.untracked == 0 && sync.matched == sync.shown);
    CHECK(sync.own == sync.shown && sync.latest == 0);
}
static void a_setup_that_bailed_falls_back_to_the_latest_once() {
    Scenario sc; sc.setup_ok[100] = false;   // the Setup made while the counter read 100 wrote nothing; the engine still shows a frame at Present 102
    const auto sync = run_pipeline(true, sc);
    CHECK(sync.latest == 1 && sync.untracked == 0);                       // Present 102: no own slot, the latest Setup's (counter 101) is promoted
    CHECK(sync.skewed == 1 && sync.matched == sync.shown - 1);            // that one Present is submitted as the legacy glue would; the rest are exact
    const auto legacy = run_pipeline(false, sc);
    CHECK(legacy.untracked == 1 && legacy.skewed == legacy.shown - 1);   // the legacy glue: nothing recorded = untracked at Present 101, skewed on every other
    Scenario twice; twice.setup_ok[100] = false; twice.setup_ok[101] = false;
    const auto gap = run_pipeline(true, twice);
    CHECK(gap.latest == 1 && gap.untracked == 1);                         // Present 102: nothing recorded for it or the next: untracked (fail closed); 103: fallback
}
static void a_recentre_refuses_old_epoch_slots() {
    Scenario sc; for (std::size_t k = 150; k < sc.epoch.size(); ++k) sc.epoch[k] = 2;   // recentred at Present 150
    const auto sync = run_pipeline(true, sc);
    CHECK(sync.untracked == 1);        // Present 150: both slots (counters 148, 149) were located in epoch 1
    CHECK(sync.latest == 1);           // Present 151: its own slot (counter 149) is old; the latest (counter 150) is current and promoted as before
    CHECK(sync.skewed == 1 && sync.matched == sync.shown - 2);
}
static void slot_choice_rules() {
    CHECK(paired_views_promotable(true, 3u, 5, 5));
    CHECK(!paired_views_promotable(false, 3u, 5, 5));   // no slot recorded for this Present
    CHECK(!paired_views_promotable(true, 1u, 5, 5));    // a one-view slot (alternate mode's) is never a paired frame
    CHECK(!paired_views_promotable(true, 2u, 5, 5));
    CHECK(!paired_views_promotable(true, 0u, 5, 5));
    CHECK(!paired_views_promotable(true, 3u, 4, 5));    // located before a recentre
    CHECK(paired_slot_to_promote(true, 3u, 5, true, 3u, 5, 5) == PairedSlot::own);      // own first, even when the latest would do
    CHECK(paired_slot_to_promote(false, 0u, 0, true, 3u, 5, 5) == PairedSlot::latest);
    CHECK(paired_slot_to_promote(true, 3u, 4, true, 3u, 5, 5) == PairedSlot::latest);   // own from the old epoch
    CHECK(paired_slot_to_promote(true, 3u, 4, true, 3u, 4, 5) == PairedSlot::none);
    CHECK(paired_slot_to_promote(false, 0u, 0, false, 0u, 0, 5) == PairedSlot::none);
    CHECK(paired_slot_to_promote(false, 0u, 0, true, 1u, 5, 5) == PairedSlot::none);    // a one-view latest slot is not a fallback
}
static void pending_slots_hold_a_pair_for_its_display_frame() {
    PendingViews<View> p;
    p.record(10, 0, View{1}, 1, 0, 0); p.record(10, 1, View{1}, 1, 0, 0);   // the Setup made at counter 8
    p.record(11, 0, View{2}, 1, 0, 0); p.record(11, 1, View{2}, 1, 0, 0);   // the Setup made at counter 9
    const auto ten = p.take(10), eleven = p.take(11);
    CHECK(ten && ten->mask == 3u && ten->view[0].pose == 1 && ten->view[1].pose == 1);
    CHECK(eleven && eleven->mask == 3u && eleven->view[0].pose == 2);
    CHECK(!p.take(9) && !p.take(12));
    p.record(12, 0, View{3}, 1, 0, 0);                                      // the next Setup overwrites the parity slot of 10
    CHECK(!p.take(10) && p.take(12) && p.take(12)->mask == 1u);             // half a pair so far: not promotable as a paired frame
    CHECK(!paired_views_promotable(true, p.take(12)->mask, 1, 1));
    p.record(12, 1, View{3}, 1, 0, 0);
    CHECK(paired_views_promotable(true, p.take(12)->mask, 1, 1));
}
int main() {
    slot_choice_rules();
    pending_slots_hold_a_pair_for_its_display_frame();
    legacy_glue_skews_every_frame_sync_glue_none();
    a_setup_that_bailed_falls_back_to_the_latest_once();
    a_recentre_refuses_old_epoch_slots();
    std::puts("PASS: layer-pose sync (paired path keyed to the display frame; the Setup-time record is one frame ahead on every Present)");
}
