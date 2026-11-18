// Plan 2A.1 lab: the static shadow cache decision model (src/static_shadow_cache_model.hpp), the
// observe-only runtime (src/static_shadow_cache.hpp) and the shadow_share frame outcome it consumes.
// No game, no D3D, no engine memory.  The atlas simulation models the engine order the design relies
// on (docs/STATIC-SHADOW-CACHE.md): SHADOWS_WORLD clears the atlas and draws world casters per light
// rect, SHADOWS_DYNAMIC draws dynamic casters on top (depth min, no clear), the readers sample after.
#include "../src/static_shadow_cache_model.hpp"
#include "../src/static_shadow_cache.hpp"
#include "../src/shadow_share_model.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace sc = d2vr::static_shadow_cache;
namespace ss = d2vr::shadow_share;

static void check(bool ok, const char* message) {if (!ok) throw std::runtime_error(message);}

static sc::Model make(std::uint32_t period = 8, std::uint32_t warmup = 2, bool per_light = false) {
    sc::Model m;
    sc::Config c;
    c.enabled = true;
    c.refresh_period = period;
    c.warmup_frames = warmup;
    c.per_light = per_light;
    m.configure(c);
    m.reset();
    return m;
}
static sc::LightKey key(std::uintptr_t light, std::uint64_t record, std::uint32_t records = 6) {
    sc::LightKey k;
    k.light = light;
    k.record_hash = record;
    k.records = records;
    return k;
}
static sc::FrameInput input(const std::vector<sc::LightKey>& lights, std::uint64_t atlas = 0xA71A5, std::uint32_t slices = 4) {
    sc::FrameInput in;
    in.lights = lights.data();
    in.light_count = static_cast<unsigned>(lights.size());
    in.atlas_generation = atlas;
    in.parallel_slices = slices;
    return in;
}
// decide + confirm a capture (what the active runtime does after issuing the copy).
static sc::Decision step(sc::Model& m, const sc::FrameInput& in, bool capture_ok = true) {
    const sc::Decision d = m.decide(in);
    if (d.capture) m.capture_result(capture_ok);
    return d;
}
// Bring a fresh model to a valid cache on `lights` (cold + warm-up).
static void warm(sc::Model& m, const std::vector<sc::LightKey>& lights) {
    for (int i = 0; i < 8 && !m.cache_valid(); ++i) step(m, input(lights));
    check(m.cache_valid(), "warm-up must end in a capture");
}

// ---- atlas simulation -------------------------------------------------------------------------
// A 1-D atlas of cells.  Each light owns `width` cells starting at its rect.  World depth depends on
// the light's pose and, for one light, a hidden caster version that the key does not see (a moved
// door drawn by the world pass).  Dynamic casters are drawn with depth-min on top every frame.
struct SimLight {std::uintptr_t id; unsigned rect; unsigned pose;};
static constexpr unsigned atlas_cells = 96;
static constexpr unsigned rect_width = 8;
static constexpr std::uint32_t far_depth = 0xffffffffu;
static std::uint32_t world_depth(const SimLight& l, unsigned cell, unsigned door) {
    std::uint64_t h = sc::mix64((static_cast<std::uint64_t>(l.id) << 20) ^ (static_cast<std::uint64_t>(l.pose) << 8) ^ cell);
    if (l.id == 0x200) h = sc::mix64(h ^ door);   // the door is a world caster of light 0x200
    return 1000u + static_cast<std::uint32_t>(h % 1000u);
}
static std::uint64_t record_of(const SimLight& l) {
    return sc::mix64((static_cast<std::uint64_t>(l.pose) << 32) ^ l.rect ^ 0xabcdef);   // matrices + placement
}
using Atlas = std::array<std::uint32_t, atlas_cells>;
static void draw_world(Atlas& a, const std::vector<SimLight>& lights, unsigned door, const std::vector<bool>* only = nullptr) {
    for (std::size_t i = 0; i < lights.size(); ++i) {
        if (only && !(*only)[i]) continue;
        for (unsigned c = 0; c < rect_width; ++c)
            a[lights[i].rect + c] = std::min(a[lights[i].rect + c], world_depth(lights[i], c, door));
    }
}
static void draw_dynamic(Atlas& a, const std::vector<SimLight>& lights, unsigned frame) {
    // One dynamic caster walks across the first light's rect.
    if (lights.empty()) return;
    const unsigned c = lights[0].rect + (frame % rect_width);
    a[c] = std::min(a[c], 5u);
}
static bool sampled_equal(const Atlas& a, const Atlas& b, const std::vector<SimLight>& lights) {
    // Readers only sample the current lights' rects.
    for (const auto& l : lights)
        for (unsigned c = 0; c < rect_width; ++c)
            if (a[l.rect + c] != b[l.rect + c]) return false;
    return true;
}
static std::vector<sc::LightKey> keys_of(const std::vector<SimLight>& lights) {
    std::vector<sc::LightKey> keys;
    for (const auto& l : lights) {
        sc::LightKey k = key(l.id, record_of(l));
        k.rect.x = static_cast<std::uint16_t>(l.rect);
        k.rect.w = rect_width;
        k.rect.h = 1;
        keys.push_back(k);
    }
    return keys;
}

struct SimResult {unsigned mismatches{}; unsigned longest_stale{}; unsigned hits{}; unsigned partials{}; unsigned frames{};};
// per_light=false: the frame-granular active path (restore all or redraw all).
// per_light=true : the later per-light path (restore valid rects, redraw the rest).
template <class Scene>
static SimResult simulate(unsigned frames, std::uint32_t period, bool per_light, Scene scene) {
    sc::Model m = make(period, 2, per_light);
    Atlas atlas{}; atlas.fill(far_depth);
    Atlas cache{}; cache.fill(far_depth);
    SimResult r;
    unsigned stale = 0;
    for (unsigned f = 0; f < frames; ++f) {
        std::vector<SimLight> lights;
        unsigned door = 0;
        scene(f, lights, door);
        const auto keys = keys_of(lights);
        const sc::Decision d = m.decide(input(keys));
        if (d.action == sc::Action::restore && !per_light) {
            atlas = cache;                       // world jobs skipped, slice-0 restore replaces the clear
            ++r.hits;
        } else if (d.action == sc::Action::restore || d.action == sc::Action::partial) {
            // Per-light path: clear, restore the valid rects from the cache (a depth-writing draw per
            // rect in D3D11), draw the world casters of the others.
            atlas.fill(far_depth);
            std::vector<bool> redraw(lights.size());
            for (std::size_t i = 0; i < lights.size(); ++i) {
                redraw[i] = !m.light_restore(static_cast<unsigned>(i));
                if (!redraw[i])
                    for (unsigned c = 0; c < rect_width; ++c) atlas[lights[i].rect + c] = cache[lights[i].rect + c];
            }
            draw_world(atlas, lights, door, &redraw);
            if (d.action == sc::Action::restore) ++r.hits; else ++r.partials;
        } else {
            atlas.fill(far_depth);               // world atlas slice 0 clear
            draw_world(atlas, lights, door);
        }
        if (d.capture) {cache = atlas; m.capture_result(true);}   // after the world pass, before dynamic
        draw_dynamic(atlas, lights, f);
        Atlas reference{}; reference.fill(far_depth);
        draw_world(reference, lights, door);
        draw_dynamic(reference, lights, f);
        if (!sampled_equal(atlas, reference, lights)) {++r.mismatches; ++stale; r.longest_stale = std::max(r.longest_stale, stale);}
        else stale = 0;
        ++r.frames;
    }
    return r;
}

int main() {
    try {
        // ---- OFF: nothing is decided or counted -------------------------------------------------
        {
            sc::Model m;
            const std::vector<sc::LightKey> lights{key(1, 11)};
            const auto d = m.decide(input(lights));
            check(d.action == sc::Action::bypass && d.reason == sc::Reason::disabled, "off: bypass/disabled");
            check(m.stats().frames == 0 && m.stats().bypasses == 0, "off: nothing counted");
            m.capture_result(true);
            check(!m.cache_valid(), "off: a stray capture result is ignored");
        }

        // ---- cold start, warm-up, first capture, hits ---------------------------------------------
        {
            auto m = make(8, 2);
            const std::vector<sc::LightKey> lights{key(1, 11), key(2, 22), key(3, 33)};
            auto d = step(m, input(lights));
            check(d.action == sc::Action::draw && d.reason == sc::Reason::warmup, "frame 1: draw without capture (warm-up)");
            d = step(m, input(lights));
            check(d.action == sc::Action::draw && d.reason == sc::Reason::warmup, "frame 2: still warming up");
            d = step(m, input(lights));
            check(d.action == sc::Action::draw_capture && d.reason == sc::Reason::cold, "frame 3: stable key captures");
            check(m.cache_valid() && m.cached_lights() == 3, "capture confirmed");
            d = step(m, input(lights));
            check(d.action == sc::Action::restore && d.reason == sc::Reason::none, "frame 4: hit");
            check(m.stats().hits == 1 && m.stats().misses == 3 && m.stats().captures == 1, "counters after first hit");
            check(m.stats().last_lights == 3 && m.stats().last_records == 18, "light/record counts");
        }

        // ---- periodic refresh: (N-1) hits per N frames ------------------------------------------
        {
            auto m = make(8, 2);
            const std::vector<sc::LightKey> lights{key(1, 11), key(2, 22)};
            warm(m, lights);
            const auto base = m.stats();
            unsigned hits = 0, refreshes = 0;
            for (int f = 0; f < 80; ++f) {
                const auto d = step(m, input(lights));
                if (d.action == sc::Action::restore) ++hits;
                if (d.reason == sc::Reason::refresh) {
                    ++refreshes;
                    check(d.action == sc::Action::draw_capture, "refresh redraws and recaptures");
                }
            }
            check(hits == 70 && refreshes == 10, "period 8: 7 hits + 1 refresh per 8 frames");
            check(m.stats().refreshes - base.refreshes == 10, "refresh counter");
            check(m.stats().invalidations == base.invalidations, "refresh is not an invalidation");
        }

        // ---- invalidations by key ---------------------------------------------------------------
        {
            auto m = make(64, 2);
            std::vector<sc::LightKey> lights{key(1, 11), key(2, 22), key(3, 33)};
            warm(m, lights);
            check(step(m, input(lights)).action == sc::Action::restore, "hit before the change");

            // A light moved / its records changed: invalidated, drawn, no capture until stable.
            lights[1].record_hash = 23;
            auto d = step(m, input(lights));
            check(d.reason == sc::Reason::light_changed && d.action == sc::Action::draw, "moved light invalidates, no capture while unstable");
            check(!m.cache_valid(), "cache dropped");
            check(m.stats().reasons[static_cast<unsigned>(sc::Reason::light_changed)] == 1, "reason counted");
            step(m, input(lights));
            d = step(m, input(lights));
            check(d.action == sc::Action::draw_capture, "recaptured once the new key is stable");
            check(step(m, input(lights)).action == sc::Action::restore, "hits again");

            // Light set order changes, set unchanged: still a hit (order independent).
            std::vector<sc::LightKey> permuted{lights[2], lights[0], lights[1]};
            check(step(m, input(permuted)).action == sc::Action::restore, "permuted light list still hits");
            // A light leaves the list (culled): subset of the cache, still a hit.
            std::vector<sc::LightKey> subset{lights[0], lights[2]};
            check(step(m, input(subset)).action == sc::Action::restore, "a vanished light does not invalidate");
            // A new light appears: not cached.
            std::vector<sc::LightKey> added = lights;
            added.push_back(key(4, 44));
            d = step(m, input(added));
            check(d.reason == sc::Reason::light_added && d.action != sc::Action::restore, "a new light invalidates");

            // Record count change is a record change.
            auto m2 = make(64, 0);
            std::vector<sc::LightKey> l2{key(1, 11, 6)};
            warm(m2, l2);
            l2[0].records = 1;
            check(step(m2, input(l2)).reason == sc::Reason::light_changed, "record count change invalidates");
            check(m2.cache_valid(), "warm-up 0 recaptures immediately");
        }

        // ---- rects, casters, atlas and slice changes --------------------------------------------
        {
            auto m = make(64, 0);
            std::vector<sc::LightKey> lights{key(1, 11), key(2, 22)};
            lights[0].rect = {0, 0, 512, 512, 0};
            lights[1].rect = {512, 0, 256, 256, 0};
            warm(m, lights);
            lights[1].rect.x = 768;     // re-placed, same records (the hash would normally change too)
            check(step(m, input(lights)).reason == sc::Reason::rect_changed, "rect move invalidates");
            {
                auto mc = make(64, 0);
                auto casters = lights;
                casters[0].caster_hash = 0x1111;
                warm(mc, casters);
                casters[0].caster_hash = 0x2222;
                check(step(mc, input(casters)).reason == sc::Reason::caster_changed, "measured caster change invalidates");
                casters[0].caster_hash = 0;   // unmeasured: not compared
                check(step(mc, input(casters)).action == sc::Action::restore, "an unmeasured caster signature is not a change");
            }
            check(step(m, input(lights, 0xB0B)).reason == sc::Reason::atlas_changed, "atlas identity change invalidates");
            check(step(m, input(lights, 0xB0B, 3)).reason == sc::Reason::slices_changed, "parallel slice count change invalidates");
            // Overlapping current rects: the key is not trusted.
            auto bad = lights;
            bad[1].rect = {256, 256, 512, 512, 0};
            const auto d = step(m, input(bad));
            check(d.action == sc::Action::bypass && d.reason == sc::Reason::rect_overlap && !m.cache_valid(), "overlap bypasses and drops");
            // Different pages never overlap.
            auto paged = lights;
            paged[1].rect = {0, 0, 512, 512, 1};
            check(step(m, input(paged)).action != sc::Action::bypass, "same rect on another page is fine");
        }

        // ---- bypasses: unreadable, overflow, no share, secondary --------------------------------
        {
            auto m = make(64, 2);
            const std::vector<sc::LightKey> lights{key(1, 11)};
            warm(m, lights);
            auto in = input(lights);
            in.share_active = false;
            auto d = step(m, in);
            check(d.action == sc::Action::bypass && d.reason == sc::Reason::no_share && m.cache_valid(), "no share: bypass, cache kept");
            in = input(lights);
            in.secondary = true;
            d = step(m, in);
            check(d.action == sc::Action::bypass && d.reason == sc::Reason::secondary && m.cache_valid(), "secondary pass: bypass, cache kept");
            check(step(m, input(lights)).action == sc::Action::restore, "hit after the bypassed frames");
            in = input(lights);
            in.key_readable = false;
            d = step(m, in);
            check(d.action == sc::Action::bypass && d.reason == sc::Reason::unreadable && !m.cache_valid(), "unreadable key drops the cache");
            warm(m, lights);
            in = input(lights);
            in.overflow = true;
            d = step(m, in);
            check(d.reason == sc::Reason::overflow && !m.cache_valid(), "overflow drops the cache");
            check(m.stats().bypasses == 4, "four bypasses");
        }

        // ---- capture refused / lost, external invalidation --------------------------------------
        {
            auto m = make(64, 0);
            const std::vector<sc::LightKey> lights{key(1, 11)};
            auto d = m.decide(input(lights));
            check(d.action == sc::Action::draw_capture, "warm-up 0 captures at once");
            m.capture_result(false);
            check(!m.cache_valid() && m.stats().capture_refused == 1, "refused capture keeps the cache invalid");
            d = m.decide(input(lights));
            check(d.action == sc::Action::draw_capture, "retries");
            d = m.decide(input(lights));   // never confirmed
            check(m.stats().capture_refused == 2 && d.action == sc::Action::draw_capture, "an unconfirmed capture is a refused one");
            m.capture_result(true);
            check(m.decide(input(lights)).action == sc::Action::restore, "hit after a confirmed capture");
            m.invalidate();
            check(!m.cache_valid() && m.last_reason() == sc::Reason::external, "external invalidation");
        }

        // ---- per-light verdicts: staggered refresh covers every light once per period ------------
        {
            auto m = make(8, 0);
            std::vector<sc::LightKey> lights;
            for (unsigned i = 0; i < 24; ++i) lights.push_back(key(0x1000 + 0x40 * i, 100 + i));
            warm(m, lights);
            std::vector<unsigned> redraws(lights.size());
            for (int f = 0; f < 8; ++f) {
                const auto d = m.decide(input(lights));
                if (d.capture) m.capture_result(true);
                for (unsigned i = 0; i < lights.size(); ++i) if (!m.light_restore(i)) ++redraws[i];
            }
            for (unsigned i = 0; i < lights.size(); ++i) check(redraws[i] >= 1, "every light is redrawn at least once per period");
            unsigned total = 0;
            for (auto r : redraws) total += r;
            check(total <= 2 * lights.size(), "the staggered refresh redraws about one light in N per frame");
            // A changed light is a redraw verdict, the others stay restorable.
            auto changed = lights;
            changed[5].record_hash = 999;
            m.decide(input(changed));
            check(!m.light_restore(5), "changed light redraws");
        }
        {
            // Per-light path: one light swings every frame; the others keep restoring and the cache stays.
            auto m = make(8, 2, true);
            std::vector<sc::LightKey> lights{key(1, 11), key(2, 22), key(3, 33)};
            warm(m, lights);
            unsigned partial = 0;
            for (int f = 0; f < 20; ++f) {
                lights[1].record_hash = 1000 + f;
                const auto d = step(m, input(lights));
                check(d.action != sc::Action::bypass && d.action != sc::Action::restore, "per-light: a moving light is never restored");
                check(!m.light_restore(1), "per-light: the moving light redraws");
                check(m.cache_valid(), "per-light: one light's change keeps the cache");
                if (d.action == sc::Action::partial) ++partial;
            }
            check(partial >= 15, "per-light: the static lights keep restoring while one light moves");
            check(m.stats().partials >= 15 && m.stats().invalidations >= 20, "per-light counters");
        }

        // ---- atlas simulation: correctness and the staleness bound -------------------------------
        {
            // Static lights, a dynamic caster walking under light 0x100, nothing hidden changes.
            const auto static_scene = [](unsigned, std::vector<SimLight>& l, unsigned& door) {
                l = {{0x100, 0, 1}, {0x200, 16, 1}, {0x300, 40, 1}};
                door = 0;
            };
            const auto a = simulate(200, 8, false, static_scene);
            check(a.mismatches == 0, "frame path: identical to a full redraw while only dynamic casters move");
            check(a.hits >= 160, "frame path: mostly hits");
            const auto b = simulate(200, 8, true, static_scene);
            check(b.mismatches == 0, "per-light path: identical to a full redraw");

            // Keyed changes (a light moves every 13 frames, rects re-allocated every 29) are exact.
            const auto keyed_scene = [](unsigned f, std::vector<SimLight>& l, unsigned& door) {
                const unsigned shift = (f / 29) % 2 ? 48 : 0;
                l = {{0x100, 0 + shift, 1}, {0x200, 16 + shift, 1 + f / 13}, {0x300, 32 + shift, 1}};
                door = 0;
            };
            check(simulate(300, 8, false, keyed_scene).mismatches == 0, "frame path: keyed changes never show a stale frame");
            check(simulate(300, 8, true, keyed_scene).mismatches == 0, "per-light path: keyed changes never show a stale frame");

            // Unkeyed change: a world caster (door) of light 0x200 moves at frame 50 and 120 without any
            // key change.  The refresh bounds the stale shadow to fewer than N frames.
            const auto door_scene = [](unsigned f, std::vector<SimLight>& l, unsigned& door) {
                l = {{0x100, 0, 1}, {0x200, 16, 1}, {0x300, 40, 1}};
                door = f >= 120 ? 2 : f >= 50 ? 1 : 0;
            };
            for (const std::uint32_t period : {4u, 8u, 16u}) {
                const auto r = simulate(240, period, false, door_scene);
                check(r.mismatches > 0, "an unkeyed world change is visible (the key's blind spot)");
                check(r.longest_stale < period, "frame path: the refresh bounds staleness below N frames");
                const auto p = simulate(240, period, true, door_scene);
                check(p.longest_stale < period, "per-light path: the staggered refresh bounds staleness below N frames");
            }
            // Counterfactual: without the periodic refresh the stale door shadow never goes away.
            check(simulate(240, 0, false, door_scene).longest_stale > 100, "no refresh: the blind spot persists");
            // The door moves while another light (0x300) swings every frame: the frame path never hits
            // (any change redraws everything, so it is exact); the per-light path keeps restoring 0x100
            // and still corrects the door within N frames.
            const auto swing_scene = [](unsigned f, std::vector<SimLight>& l, unsigned& door) {
                l = {{0x100, 0, 1}, {0x200, 16, 1}, {0x300, 40, 1 + f}};
                door = f >= 60 ? 1 : 0;
            };
            const auto fs = simulate(160, 8, false, swing_scene);
            check(fs.mismatches == 0 && fs.hits == 0, "frame path: a light moving every frame means no hits, no error");
            const auto ps = simulate(160, 8, true, swing_scene);
            check(ps.partials > 100, "per-light path: partial restores while one light swings");
            check(ps.mismatches > 0 && ps.longest_stale < 8, "per-light path: door corrected within N frames while another light moves");
        }

        // ---- shadow_share frame outcome (the cache's view of the finished frame) ----------------
        {
            ss::Model share;
            share.configure(true);
            share.reset();
            ss::ViewSignature a{0xA000, 0, 2, true}, b{0xB000, 0, 2, true};
            share.on_eye_begin(0, a);
            share.on_eye_begin(1, b);
            check(!share.should_suppress(ss::pass_prepare, 0xA000, false), "first view prepare runs");
            share.observe_shadow_param(ss::pass_forward_world, 0xA000, true, 0x5000);
            check(share.should_suppress(ss::pass_world, 0xA000, false) == false, "first view world runs");
            check(share.should_suppress(ss::pass_prepare, 0xB000, false), "second view prepare suppressed");
            const auto serial = share.frame_serial();
            share.on_eye_begin(0, a);   // next frame
            const auto& prev = share.previous_frame();
            check(prev.valid && prev.serial == serial && prev.paired && prev.shared && !prev.fallback && !prev.secondary,
                  "previous frame outcome: paired and shared");
            share.on_secondary_begin(0xB000, true);
            share.on_secondary_end();
            share.on_eye_begin(0, a);
            check(share.previous_frame().secondary && share.previous_frame().fallback, "a lens between the eyes marks the frame");
        }

        // ---- runtime (observe path) --------------------------------------------------------------
        {
            sc::Runtime rt;
            rt.configure(false, false, 8, false);
            const sc::LightKey k1 = key(0x10, 1), k2 = key(0x20, 2);
            const sc::LightKey keys[2] = {k1, k2};
            ss::FrameOutcome prev{};
            rt.submit_lights(1, keys, 2, true, 4, prev);
            check(rt.stats().frames == 0, "runtime off: nothing decided");

            rt.configure(true, true, 8, true);
            check(rt.active() && rt.probe(), "runtime on");
            // Frames 1..20: same key, paired and shared.  Each submission finalizes the previous serial.
            for (std::uint64_t s = 1; s <= 20; ++s) {
                prev = ss::FrameOutcome{s - 1, s > 1, true, true, false, false};
                sc::JobDraws job;
                job.begin(s);
                job.note({0, 0, 512, 512, 0}, 300, 1, 0, 0);
                job.note({512, 0, 512, 512, 0}, 120, 1, 900, 0);
                rt.merge_draws(job);
                rt.note_job(s, 0, sc::pass_world, sc::Path::atlas, 2000);
                rt.note_job(s, 0, sc::pass_world, sc::Path::parallel, 500);
                rt.note_job(s, 1, sc::pass_world, sc::Path::atlas, 2000);   // second view: not counted
                rt.submit_lights(s, keys, 2, true, 4, prev);
            }
            const auto st = rt.stats();
            check(st.frames == 19, "19 frames finalized");
            check(st.hits >= 14 && st.refreshes >= 1, "observe path reports would-be hits and refreshes");
            check(rt.probe_unkeyed_draw_changes() == 0 && rt.probe_unkeyed_rect_changes() == 0, "stable draws: no blind-spot changes");
            // Frame 21: same key, different world draws (a caster the key does not see).
            {
                sc::JobDraws job;
                job.begin(21);
                job.note({0, 0, 512, 512, 0}, 301, 1, 0, 0);
                job.note({512, 0, 512, 512, 0}, 120, 1, 900, 0);
                rt.merge_draws(job);
                rt.submit_lights(21, keys, 2, true, 4, ss::FrameOutcome{20, true, true, true, false, false});
                rt.submit_lights(22, keys, 2, true, 4, ss::FrameOutcome{21, true, true, true, false, false});
            }
            check(rt.probe_unkeyed_draw_changes() == 1, "a draw change under an unchanged key is reported");
            // A fallback frame is a bypass.
            rt.submit_lights(23, keys, 2, true, 4, ss::FrameOutcome{22, true, true, false, true, false});
            check(rt.last_decision().action == sc::Action::bypass && rt.last_decision().reason == sc::Reason::no_share, "fallback frame bypasses");
            // Execution order probe.
            rt.note_execute(sc::pass_world, 100);
            rt.note_execute(sc::pass_world, 100);
            rt.note_execute(sc::pass_dynamic, 100);
            rt.note_execute(sc::pass_world, 101);
            rt.note_execute(sc::pass_dynamic, 101);
            rt.note_execute(sc::pass_world, 101);   // out of order
            rt.note_execute(sc::pass_world, 102);
            check(rt.order_world_after_dynamic() == 1, "world list after a dynamic list is counted");
            sc::AtlasResource atlas{};
            atlas.resource = 0x1234; atlas.width = 4096; atlas.height = 4096; atlas.format = 40; atlas.array_size = 1;
            rt.note_resource(atlas, sc::Path::atlas);
            check(atlas.bytes() == 4096ull * 4096ull * 4ull, "atlas bytes from the description");
            const std::string json = rt.json();
            for (const char* field : {"\"enabled\":true", "\"hits\":", "\"misses\":", "\"invalidations\":", "\"refreshes\":",
                                      "\"jobs_per_frame\":", "\"world_atlas\":1", "\"probe\":", "\"unkeyed_draw_changes\":1",
                                      "\"world_after_dynamic\":1", "\"frames_out_of_order\":1", "\"bytes\":67108864", "\"mode\":\"observe\""})
                check(json.find(field) != std::string::npos, field);
            rt.set_live(false, 8, false);
            check(!rt.active() && rt.json().find("\"enabled\":false") != std::string::npos, "live off");
            rt.invalidate();
        }

        // ---- helpers ----------------------------------------------------------------------------
        {
            check(sc::path_for_slice(0, 4) == sc::Path::parallel && sc::path_for_slice(3, 4) == sc::Path::parallel &&
                  sc::path_for_slice(4, 4) == sc::Path::atlas, "slice classification");
            unsigned char a[0x84]{}, b[0x84]{};
            b[0x83] = 1;
            check(sc::hash_words(a, 0x84, 1) != sc::hash_words(b, 0x84, 1), "record hash sees the last byte");
            check(sc::hash_words(a, 0x84, 1) == sc::hash_words(a, 0x84, 1), "record hash is stable");
            check(sc::depth_format_bytes(55) == 2 && sc::depth_format_bytes(40) == 4 && sc::depth_format_bytes(20) == 8 &&
                  sc::depth_format_bytes(28) == 0, "depth format sizes");
        }

        std::puts("PASS: static shadow cache model (warm-up, capture, hits, (N-1)/N refresh, light/rect/caster/atlas/slice "
                  "invalidations, subset and order independence, bypasses, refused captures, per-light staggered refresh), "
                  "atlas simulation (exact under keyed changes and moving dynamic casters, unkeyed world change bounded below N "
                  "frames for N=4/8/16, frame and per-light paths), shadow_share frame outcome, observe runtime and probe JSON");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
