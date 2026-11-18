#pragma once
// Static shadow cache (plan 2A.1), pure decision model: no Windows, no D3D, no engine memory.
//
// Design: docs/STATIC-SHADOW-CACHE.md.  SHADOWS_WORLD (pass 20) clears the local-light (spot/omni)
// shadow atlas and redraws the world casters into it every frame; SHADOWS_DYNAMIC (pass 21) then
// draws the dynamic casters on top without a clear (docs/ENGINE-PERF-PLAN.md phase 7 "One atlas per
// render system, cleared per view" and the phase-9 ownership table).  The cache keeps a copy of the
// atlas as it is after the world pass ("world-only depth").  While every shadowed light's GPU-visible
// shadow records (matrices + atlas placement, the bytes TLFLIGHTCULL publishes) are unchanged, a
// frame may skip the world pass's atlas jobs, restore the copy, and let SHADOWS_DYNAMIC draw on top.
//
// The model decides, once per frame, from a per-light key:
//   restore      - every light is cached with an equal key and no refresh is due: skip the world
//                  atlas jobs and restore the cache (a "hit");
//   draw_capture - draw the world pass normally and capture the result into the cache (a "miss":
//                  cold, invalidated, or the periodic refresh);
//   draw         - draw normally, do not capture (the key changed recently: warm-up, so an unstable
//                  key does not pay a capture copy every frame);
//   bypass       - do not use the cache this frame (no share pair, a secondary pass, an unreadable
//                  key).  Unreadable keys and resource changes also drop the cache.
// Per-light verdicts are computed as well (restore vs redraw per light, with a staggered refresh) for
// the later per-light path; the frame-granular path restores only when every light restores.
//
// This header is the lab subject (tests/static_shadow_cache_check.cpp) and the runtime's core
// (src/static_shadow_cache.hpp).  It never touches the engine; the runtime feeds it keys.
#include <cstdint>

namespace d2vr {
namespace static_shadow_cache {

inline constexpr unsigned light_capacity = 256;

// An atlas rectangle in texels (the viewport the world pass renders a light face into), plus the
// atlas page/array slice.  All-zero = unknown (the record hash still covers placement, because the
// shader can only find a light's tile through its published record).
struct AtlasRect {
    std::uint16_t x{}, y{}, w{}, h{};
    std::uint16_t page{};
    bool known() const noexcept {return w != 0 && h != 0;}
    bool operator==(const AtlasRect& o) const noexcept {
        return x == o.x && y == o.y && w == o.w && h == o.h && page == o.page;
    }
    bool operator!=(const AtlasRect& o) const noexcept {return !(*this == o);}
    bool overlaps(const AtlasRect& o) const noexcept {
        if (!known() || !o.known() || page != o.page) return false;
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
};

// One shadowed (atlas) light as the first view publishes it this frame.
struct LightKey {
    std::uintptr_t light{};        // light node (TLFLIGHTCULL list entry; [entry+0] of its shadow entry)
    std::uint64_t record_hash{};   // hash of the published records (0x84 bytes per face)
    std::uint32_t records{};       // face/record count (entry+0x18 & 0xffffff)
    AtlasRect rect{};              // optional, from the draw probe
    std::uint64_t caster_hash{};   // optional world-caster signature for this light; 0 = not measured
};

struct FrameInput {
    const LightKey* lights{};
    unsigned light_count{};
    bool key_readable{true};       // every engine read succeeded
    bool overflow{};               // more lights than light_capacity
    std::uint64_t atlas_generation{}; // identity of the atlas resource(s); 0 = unknown
    std::uint32_t parallel_slices{};  // global parallel slice count (selects which world jobs are atlas jobs)
    bool share_active{true};       // the first view owns the shared atlas this frame (shadow_share pair)
    bool secondary{};              // a secondary (Timepiece lens) pass ran in this frame
};

// partial is the per-light path only: restore the lights whose verdict is restore, redraw the rest.
enum class Action {bypass, draw, draw_capture, restore, partial};
inline const char* action_name(Action a) noexcept {
    switch (a) {
    case Action::bypass: return "bypass";
    case Action::draw: return "draw";
    case Action::draw_capture: return "draw_capture";
    case Action::restore: return "restore";
    case Action::partial: return "partial";
    }
    return "unknown";
}

enum class Reason {
    none,
    disabled,
    no_share,        // shadow_share not pairing: not the cache's frame
    secondary,       // a secondary pass ran: bypass, keep the cache
    unreadable,      // a key read faulted: bypass and drop the cache
    overflow,        // too many lights for the key: bypass and drop
    rect_overlap,    // two current lights claim overlapping rects: the key is not trusted
    cold,            // no cache yet
    warmup,          // key changed recently: draw without capture
    refresh,         // periodic safety refresh
    light_added,     // a light is not in the cache
    light_changed,   // a light's records changed (moved, rotated, re-projected or re-placed)
    rect_changed,    // a light's measured rect changed
    caster_changed,  // a light's measured caster signature changed
    atlas_changed,   // atlas resource identity changed
    slices_changed,  // parallel slice count changed (world job split moved)
    external,        // invalidate() from the runtime (recovery, resize, knob)
    capture_refused, // the runtime could not capture (order/size check)
    count
};
inline const char* reason_name(Reason r) noexcept {
    switch (r) {
    case Reason::none: return "none";
    case Reason::disabled: return "disabled";
    case Reason::no_share: return "no_share";
    case Reason::secondary: return "secondary";
    case Reason::unreadable: return "unreadable";
    case Reason::overflow: return "overflow";
    case Reason::rect_overlap: return "rect_overlap";
    case Reason::cold: return "cold";
    case Reason::warmup: return "warmup";
    case Reason::refresh: return "refresh";
    case Reason::light_added: return "light_added";
    case Reason::light_changed: return "light_changed";
    case Reason::rect_changed: return "rect_changed";
    case Reason::caster_changed: return "caster_changed";
    case Reason::atlas_changed: return "atlas_changed";
    case Reason::slices_changed: return "slices_changed";
    case Reason::external: return "external";
    case Reason::capture_refused: return "capture_refused";
    case Reason::count: break;
    }
    return "unknown";
}
inline constexpr unsigned reason_count = static_cast<unsigned>(Reason::count);

struct Config {
    bool enabled{};
    std::uint32_t refresh_period{8};  // a full (or per-light staggered) refresh at least every N frames; 0 = never (lab only)
    std::uint32_t warmup_frames{2};   // identical consecutive keys required before a capture
    // false: frame-granular path (restore only when every light is valid; any change drops the cache).
    // true:  per-light path (a changed light is redrawn, the others restore; staggered per-light refresh).
    bool per_light{};
};

struct Decision {
    Action action{Action::bypass};
    Reason reason{Reason::disabled};
    bool capture{};              // capture the atlas after the world pass (confirm with capture_result)
    unsigned restore_lights{};   // per-light verdicts: lights whose cached rect is valid
    unsigned redraw_lights{};    // lights that need their world casters drawn
};

struct Counters {
    std::uint64_t frames{};          // decide() calls
    std::uint64_t hits{};            // restore frames (every light restored)
    std::uint64_t partials{};        // per-light path: some lights restored, some redrawn
    std::uint64_t misses{};          // draw / draw_capture frames
    std::uint64_t bypasses{};
    std::uint64_t invalidations{};   // frames whose key no longer matched the cache, plus dropped caches
    std::uint64_t refreshes{};       // periodic refresh frames
    std::uint64_t captures{};        // confirmed captures
    std::uint64_t capture_refused{};
    std::uint64_t light_restores{};  // per-light verdicts (for the per-light path)
    std::uint64_t light_redraws{};
    std::uint64_t reasons[reason_count]{};
    std::uint32_t last_lights{};
    std::uint32_t last_records{};
};

// Order-independent 64-bit mixing used for frame keys (the light list order may change while the
// set and every light's records stay identical).
inline std::uint64_t mix64(std::uint64_t v) noexcept {
    v ^= v >> 33; v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ull;
    v ^= v >> 33;
    return v;
}
inline std::uint64_t rect_bits(const AtlasRect& r) noexcept {
    return (static_cast<std::uint64_t>(r.x) | (static_cast<std::uint64_t>(r.y) << 16) |
            (static_cast<std::uint64_t>(r.w) << 32) | (static_cast<std::uint64_t>(r.h) << 48)) ^
           (static_cast<std::uint64_t>(r.page) * 0x9e3779b97f4a7c15ull);
}
inline std::uint64_t light_hash(const LightKey& k) noexcept {
    std::uint64_t h = mix64(static_cast<std::uint64_t>(k.light) ^ 0x51ed270b27f3a9c5ull);
    h = mix64(h ^ k.record_hash);
    h = mix64(h ^ k.records);
    h = mix64(h ^ rect_bits(k.rect));
    h = mix64(h ^ k.caster_hash);
    return h;
}

class Model {
public:
    void configure(const Config& config) noexcept {
        config_ = config;
        if (!config_.enabled) drop();
    }
    const Config& config() const noexcept {return config_;}
    bool enabled() const noexcept {return config_.enabled;}

    // Clears the cache and the counters (a new session or a live re-enable).
    void reset() noexcept {
        counters_ = {};
        drop();
        cold_frames_ = 0;
        stable_ = 0;
        last_frame_hash_ = 0;
        last_frame_valid_ = false;
        serial_ = 0;
        last_reason_ = config_.enabled ? Reason::none : Reason::disabled;
    }

    // External invalidation (XR recovery, resolution change, atlas recreated): drop the cache.
    void invalidate(Reason reason = Reason::external) noexcept {
        if (cache_valid_ || pending_) {
            ++counters_.invalidations;
            ++counters_.reasons[static_cast<unsigned>(reason)];
        }
        drop();
        stable_ = 0;
        last_reason_ = reason;
    }

    // One call per frame, before the first-view world atlas jobs would run (active path) or at the
    // frame's end (observe-only path).  Per-light verdicts are readable through light_restores().
    Decision decide(const FrameInput& in) noexcept {
        Decision d;
        ++serial_;
        if (pending_) {
            // The runtime never confirmed the previous frame's capture.
            pending_ = false;
            ++counters_.capture_refused;
            ++counters_.reasons[static_cast<unsigned>(Reason::capture_refused)];
        }
        verdict_count_ = 0;
        if (!config_.enabled) return finish_bypass(d, Reason::disabled, false);
        ++counters_.frames;
        if (!in.share_active) return finish_bypass(d, Reason::no_share, false);
        if (in.secondary) return finish_bypass(d, Reason::secondary, false);
        if (!in.key_readable || (in.light_count && !in.lights)) return finish_bypass(d, Reason::unreadable, true);
        if (in.overflow || in.light_count > light_capacity) return finish_bypass(d, Reason::overflow, true);
        for (unsigned i = 0; i < in.light_count; ++i)
            for (unsigned j = i + 1; j < in.light_count; ++j)
                if (in.lights[i].rect.overlaps(in.lights[j].rect)) return finish_bypass(d, Reason::rect_overlap, true);

        // Key stability (identical consecutive frame keys) gates captures, not hits.
        std::uint64_t frame_hash = mix64(in.atlas_generation ^ 0x2545f4914f6cdd1dull) ^ mix64(in.parallel_slices);
        std::uint32_t records = 0;
        for (unsigned i = 0; i < in.light_count; ++i) {
            frame_hash += light_hash(in.lights[i]);
            records += in.lights[i].records;
        }
        frame_hash = mix64(frame_hash ^ in.light_count);
        if (last_frame_valid_ && frame_hash == last_frame_hash_) ++stable_;
        else stable_ = 0;
        last_frame_hash_ = frame_hash;
        last_frame_valid_ = true;
        counters_.last_lights = in.light_count;
        counters_.last_records = records;

        // Per-light verdicts against the cache.
        Reason first_change = Reason::none;
        unsigned restores = 0, redraws = 0, refresh_redraws = 0;
        const bool frame_level_change = cache_valid_ &&
            (in.atlas_generation != cached_atlas_ || in.parallel_slices != cached_slices_);
        if (frame_level_change)
            first_change = in.atlas_generation != cached_atlas_ ? Reason::atlas_changed : Reason::slices_changed;
        for (unsigned i = 0; i < in.light_count; ++i) {
            const LightKey& k = in.lights[i];
            bool valid = false;
            Reason why = Reason::cold;
            if (cache_valid_ && !frame_level_change) {
                const LightKey* c = find_cached(k.light);
                if (!c) why = Reason::light_added;
                else if (k.rect.known() && c->rect.known() && k.rect != c->rect) why = Reason::rect_changed;
                else if (k.record_hash != c->record_hash || k.records != c->records) why = Reason::light_changed;
                else if (k.caster_hash && c->caster_hash && k.caster_hash != c->caster_hash) why = Reason::caster_changed;
                else valid = true;
                if (!valid && first_change == Reason::none) first_change = why;
            }
            // Staggered per-light refresh: each light's slot comes round once per refresh period.
            if (valid && config_.refresh_period &&
                ((serial_ + (mix64(static_cast<std::uint64_t>(k.light)) % config_.refresh_period)) % config_.refresh_period) == 0) {
                valid = false;
                ++refresh_redraws;
            }
            verdicts_[verdict_count_++] = valid;
            if (valid) ++restores; else ++redraws;
        }
        d.restore_lights = restores;
        d.redraw_lights = redraws;
        counters_.light_restores += restores;
        counters_.light_redraws += redraws;

        if (!cache_valid_) {
            // The per-light path cannot wait for a stable frame key (one swinging lamp would keep it
            // from ever caching the others): it captures after warmup_frames cold frames.
            ++cold_frames_;
            const bool ready = stable_ >= config_.warmup_frames ||
                               (config_.per_light && cold_frames_ > config_.warmup_frames);
            d.reason = ready ? Reason::cold : Reason::warmup;
            return finish_miss(d, in, false, ready);
        }
        if (config_.per_light && !frame_level_change)
            return per_light_decision(d, in, first_change, restores, redraws, refresh_redraws);
        if (first_change != Reason::none) {
            // The key no longer matches the cache: an invalidation.  The frame draws; it captures
            // only once the new key has been stable for warmup_frames.
            ++counters_.invalidations;
            ++counters_.reasons[static_cast<unsigned>(first_change)];
            drop();
            d.reason = first_change;
            return finish_miss(d, in);
        }
        // Frame-granular refresh: the whole atlas is redrawn and recaptured every refresh_period frames.
        if (config_.refresh_period && age_ + 1 >= config_.refresh_period) {
            ++counters_.refreshes;
            d.reason = Reason::refresh;
            return finish_miss(d, in, true);
        }
        ++age_;
        ++counters_.hits;
        d.action = Action::restore;
        d.reason = Reason::none;
        last_reason_ = Reason::none;
        return d;
    }

    // The runtime reports whether the capture of a draw_capture frame was actually issued.
    void capture_result(bool ok) noexcept {
        if (!pending_) return;
        pending_ = false;
        if (!ok) {
            ++counters_.capture_refused;
            ++counters_.reasons[static_cast<unsigned>(Reason::capture_refused)];
            drop();
            last_reason_ = Reason::capture_refused;
            return;
        }
        for (unsigned i = 0; i < pending_count_; ++i) cached_[i] = pending_lights_[i];
        cached_count_ = pending_count_;
        cached_atlas_ = pending_atlas_;
        cached_slices_ = pending_slices_;
        cache_valid_ = true;
        age_ = 0;
        cold_frames_ = 0;
        ++counters_.captures;
    }

    bool cache_valid() const noexcept {return cache_valid_;}
    bool capture_pending() const noexcept {return pending_;}
    std::uint32_t age() const noexcept {return age_;}
    unsigned cached_lights() const noexcept {return cache_valid_ ? cached_count_ : 0;}
    std::uint32_t stable_frames() const noexcept {return stable_;}
    Reason last_reason() const noexcept {return last_reason_;}
    // Per-light verdict of the last decide(): true = restore this light's rect from the cache.
    bool light_restore(unsigned index) const noexcept {return index < verdict_count_ && verdicts_[index];}
    unsigned verdict_count() const noexcept {return verdict_count_;}
    const Counters& stats() const noexcept {return counters_;}

private:
    Decision finish_bypass(Decision& d, Reason reason, bool drop_cache) noexcept {
        if (config_.enabled) {
            ++counters_.bypasses;
            ++counters_.reasons[static_cast<unsigned>(reason)];
        }
        if (drop_cache && (cache_valid_ || pending_)) {
            ++counters_.invalidations;
            drop();
        }
        if (drop_cache) {
            stable_ = 0;
            last_frame_valid_ = false;
        }
        d.action = Action::bypass;
        d.reason = reason;
        last_reason_ = reason;
        return d;
    }
    // Per-light path: changed lights are redrawn, the others restored.  The cache is not dropped; a
    // capture (whole atlas after the world pass) replaces the cached keys with this frame's.
    // A frame with a staggered refresh redraw always captures, so a light whose world casters changed
    // without a key change is corrected in the cache within one refresh period even while another
    // light keeps moving (the moving light alone would otherwise hold every capture back).
    Decision per_light_decision(Decision& d, const FrameInput& in, Reason first_change, unsigned restores,
                                unsigned redraws, unsigned refresh_redraws) noexcept {
        if (first_change != Reason::none) {
            ++counters_.invalidations;
            ++counters_.reasons[static_cast<unsigned>(first_change)];
        }
        if (!redraws) {
            ++age_;
            ++counters_.hits;
            d.action = Action::restore;
            d.reason = Reason::none;
            last_reason_ = Reason::none;
            return d;
        }
        if (first_change == Reason::none) {
            ++counters_.refreshes;   // only staggered refresh verdicts: the key is stable by definition
            ++counters_.reasons[static_cast<unsigned>(Reason::refresh)];
        }
        d.reason = first_change == Reason::none ? Reason::refresh : first_change;
        d.action = restores ? Action::partial : Action::draw;
        if (restores) ++counters_.partials; else ++counters_.misses;
        if (first_change == Reason::none || refresh_redraws || stable_ >= config_.warmup_frames) set_pending(d, in);
        last_reason_ = d.reason;
        return d;
    }
    void set_pending(Decision& d, const FrameInput& in) noexcept {
        d.capture = true;
        pending_ = true;
        pending_count_ = in.light_count;
        for (unsigned i = 0; i < in.light_count; ++i) pending_lights_[i] = in.lights[i];
        pending_atlas_ = in.atlas_generation;
        pending_slices_ = in.parallel_slices;
    }
    Decision finish_miss(Decision& d, const FrameInput& in, bool refresh = false, bool ready = false) noexcept {
        ++counters_.misses;
        if (d.reason == Reason::cold || d.reason == Reason::warmup) ++counters_.reasons[static_cast<unsigned>(d.reason)];
        else if (refresh) ++counters_.reasons[static_cast<unsigned>(Reason::refresh)];
        // A refresh keeps a stable key by definition; any other miss captures once stable.
        if (refresh || ready || stable_ >= config_.warmup_frames) {
            d.action = Action::draw_capture;
            set_pending(d, in);
        } else {
            d.action = Action::draw;
        }
        last_reason_ = d.reason;
        return d;
    }
    const LightKey* find_cached(std::uintptr_t light) const noexcept {
        for (unsigned i = 0; i < cached_count_; ++i)
            if (cached_[i].light == light) return &cached_[i];
        return nullptr;
    }
    void drop() noexcept {
        if (cache_valid_) cold_frames_ = 0;
        cache_valid_ = false;
        pending_ = false;
        cached_count_ = 0;
        age_ = 0;
    }

    Config config_{};
    Counters counters_{};
    bool cache_valid_{};
    bool pending_{};
    std::uint32_t age_{};
    std::uint32_t cold_frames_{};
    std::uint32_t stable_{};
    std::uint64_t serial_{};
    std::uint64_t last_frame_hash_{};
    bool last_frame_valid_{};
    Reason last_reason_{Reason::disabled};
    LightKey cached_[light_capacity]{};
    unsigned cached_count_{};
    std::uint64_t cached_atlas_{};
    std::uint32_t cached_slices_{};
    LightKey pending_lights_[light_capacity]{};
    unsigned pending_count_{};
    std::uint64_t pending_atlas_{};
    std::uint32_t pending_slices_{};
    bool verdicts_[light_capacity]{};
    unsigned verdict_count_{};
};

} // namespace static_shadow_cache
} // namespace d2vr
