#pragma once
// v1009 shadow sharing model (pure; no Windows, no engine).
//
// Static ownership result (docs/ENGINE-PERF-PLAN.md, "Shadow setup ownership (phase 8)" and
// "Why the right eye lost dynamic shadows (phase 9)"):
// the pass manager's "default" object (manager+0x1c8ac0, built at 0x35a3d0) is a single
// object shared by both views; PREPARE writes the per-view cascade matrices and fars into
// it and the second view's PREPARE overwrites them before its own forward records.  The
// per-view data that a view's forward/fog passes actually bind is the render-graph resource
// named shadowMapsInfos (job item +0x58 table, hash 0x3719f109), which is supplied per view.
//
// v1006 therefore returns to the v1004 saving (all five shadow writers skipped for the
// second view, so the shared setup and atlas keep the first view's contents) and redirects
// the second view's shadow-reading jobs to the first view's captured shadowMapsInfos
// resource.  If no first-view reader resource was captured before the second view begins,
// the frame uses the v1005 atlas-only behaviour (run the second view's setup, skip only the
// atlas writers) and records setup_unavailable.
//
// v1009 adds the per-light link that the writers own.  SHADOWS_PREPARE stores each atlas
// light's arkShadowMapInfo_t entry pointer at light+0x1a8 (0x3328ac); GATHER_LIGHTS clears
// that field for every view (0x31c074); TLFLIGHTCULL (pass 26, runs for both views) copies
// the entry's per-slice records into the view's cpu_shadowMapsInfos array (0x2f75ca,
// 0x2f7710..0x2f7890).  Skipping the second view's PREPARE therefore leaves every light's
// link null in the second view even though the first view's entries are still intact, and
// the second view's TLFLIGHTCULL publishes no shadow-map records.  In setup mode the first
// view's TLFLIGHTCULL captures (light -> entry) and the second view's TLFLIGHTCULL writes
// the captured links around the callback, so both views publish identical records.
//
// v1011 adds the entry's record count (entry+0x18, low 24 bits).  The per-view job builder
// 0x351780 resets every entry in the shared "default" pool at the start of EACH view: it
// clears entry+0x18's low 24 bits and sets entry+0x620 = -1 (0x351dec..0x351e66).  For the
// first view that is harmless because its PREPARE runs afterwards and writes the count; for
// the second view the builder runs after the first view's PREPARE and zeroes the counts of
// the very entries the first view's TLFLIGHTCULL captured.  TLFLIGHTCULL reads
// `count = [entry+0x18] & 0xffffff` (0x2f7646) and publishes nothing for a zero count, and
// the GPU upload is skipped when the published count is zero (0x2f7ffc).  v1009 therefore
// restored the link but not the count.  The capture now carries the per-light record count
// and the restore writes it back with the link, undoing exactly the builder's reset.
//
// This type is the model the runtime wraps in a mutex; it is also the lab subject.
#include <cstdint>

namespace d2vr {
namespace shadow_share {

// Pass ids are the indices of render_pass_table.hpp (ARK_RENDERPASS_* registration order).
inline constexpr int pass_prepare = 11;       // ARK_RENDERPASS_SHADOWS_PREPARE
inline constexpr int pass_collect = 12;       // ARK_RENDERPASS_COLLECT_DYNAMIC_CASTERS
inline constexpr int pass_gather = 13;        // ARK_RENDERPASS_GATHER_CASTERS
inline constexpr int pass_world = 20;         // ARK_RENDERPASS_SHADOWS_WORLD
inline constexpr int pass_dynamic = 21;       // ARK_RENDERPASS_SHADOWS_DYNAMIC
inline constexpr int pass_screenshadows = 25; // ARK_RENDERPASS_SCREENSHADOWS
inline constexpr int pass_tlflightcull = 26;  // ARK_RENDERPASS_TLFLIGHTCULL
inline constexpr int pass_forward_world = 27;// ARK_RENDERPASS_FORWARD_WORLD
inline constexpr int pass_forward_dynamic = 28;// ARK_RENDERPASS_FORWARD_DYNAMIC
inline constexpr int pass_fog = 29;           // ARK_RENDERPASS_FOG
inline constexpr int pass_render_water = 32;  // ARK_RENDERPASS_RENDER_WATER
inline constexpr int pass_slots = 64;

inline bool mutating_shadow_pass(int pass) noexcept {
    return pass == pass_prepare || pass == pass_collect || pass == pass_gather ||
           pass == pass_world || pass == pass_dynamic;
}
// The passes that clear/redraw the shared depth atlas.
inline bool atlas_writer_pass(int pass) noexcept {return pass == pass_world || pass == pass_dynamic;}
// The passes that rebuild the per-view setup (setup mode suppresses these again).
inline bool setup_writer_pass(int pass) noexcept {
    return pass == pass_prepare || pass == pass_collect || pass == pass_gather;
}
// The passes whose shadow binding reads the per-view setup resource.  Static read-path
// sites: FORWARD_WORLD 0x311e54, FORWARD_DYNAMIC 0x31e0c0, FOG 0x309602 (shadowMapsInfos
// lookups at item+0x58); SCREENSHADOWS 0x32ca47 and RENDER_WATER 0x304244 bind the atlas
// through 0x32fd40 and are included so a shadowMapsInfos entry, if present, is redirected
// there too.  Any other pass that binds shadow data was not found in the static scan.
inline bool shadow_reader_pass(int pass) noexcept {
    return pass == pass_screenshadows || pass == pass_forward_world ||
           pass == pass_forward_dynamic || pass == pass_fog || pass == pass_render_water;
}

enum class Mode {atlas, setup};
inline const char* mode_name(Mode mode) noexcept {return mode == Mode::setup ? "setup" : "atlas";}

enum class Reason {
    none,
    disabled,
    view_count,
    view_mismatch,
    light_set_mismatch,
    cached_job,
    third_view,
    ordering,
    unpublished,
    prologue,
    setup_unavailable,
};

inline const char* reason_name(Reason reason) noexcept {
    switch (reason) {
    case Reason::none: return "none";
    case Reason::disabled: return "disabled";
    case Reason::view_count: return "view_count";
    case Reason::view_mismatch: return "view_mismatch";
    case Reason::light_set_mismatch: return "light_set_mismatch";
    case Reason::cached_job: return "cached_job";
    case Reason::third_view: return "third_view";
    case Reason::ordering: return "ordering";
    case Reason::unpublished: return "unpublished";
    case Reason::prologue: return "prologue_mismatch";
    case Reason::setup_unavailable: return "setup_unavailable";
    }
    return "unknown";
}

// A cheap per-view shadow signature read at the eye boundary.  `gate` is view+0x3a20,
// which 0x32ee50 tests before rendering directional shadows, and `flags` packs view+0x3a34
// and view+0x3a37, the two shadow/gather gate bytes used by 0x330c10 and 0x32d6e0.
struct ViewSignature {
    std::uintptr_t view{};
    std::uint32_t gate{};
    std::uint32_t flags{};
    bool valid{};
    bool operator==(const ViewSignature& other) const noexcept {
        return view == other.view && gate == other.gate && flags == other.flags && valid == other.valid;
    }
    bool operator!=(const ViewSignature& other) const noexcept {return !(*this == other);}
};

struct Counters {
    std::uint64_t frames_shared{};
    std::uint64_t second_view_skipped_jobs{};
    std::uint64_t fallbacks{};        // hard frame fallbacks: nothing suppressed
    std::uint64_t setup_redirects{};  // second-view job table writes the hook actually applied
    std::uint64_t setup_fallbacks{};  // frames that used atlas-only because setup was not captured
    std::uint64_t light_captures{};   // first-view TLFLIGHTCULL light -> shadow-map-info links read
    std::uint64_t light_restores{};   // second-view TLFLIGHTCULL links written from the capture
    std::uint64_t light_misses{};     // second-view lights the first view did not capture
    std::uint64_t light_record_restores{}; // v1011: entry+0x18 record counts written with the links
};

// The effect the runtime applies to one job item.
struct JobAction {
    bool suppress{};
    bool redirect{};
    std::uintptr_t redirect_value{};
};

// v1009: the per-light link handled at TLFLIGHTCULL (pass 26).  `capture` records
// light+0x1a8 during the first view's job; `restore` writes the captured values around the
// second view's job and puts the old values back afterwards.
enum class LightAction {none, capture, restore};

// One captured (light node, arkShadowMapInfo_t entry, entry record count) triple.  Bounded: a
// view has at most a few hundred lights (TLFLIGHTCULL's per-tile buffer is 0x1c0 entries), so
// 1024 is ample.  `records` is the low 24 bits of entry+0x18 that TLFLIGHTCULL reads.
inline constexpr unsigned light_capture_capacity = 1024;
struct LightCapture {std::uintptr_t light{}; std::uintptr_t info{}; std::uint32_t records{};};

class Model {
public:
    void configure(bool enabled) noexcept {configure(enabled, false);}
    void configure(bool enabled, bool atlas_only) noexcept {
        enabled_ = enabled;
        atlas_only_ = atlas_only;
        reason_ = enabled ? Reason::none : Reason::disabled;
    }
    bool enabled() const noexcept {return enabled_;}
    bool atlas_only() const noexcept {return atlas_only_;}
    void reset() noexcept {
        counters = {};
        first_ = {};
        second_ = {};
        paired_ = false;
        frame_started_ = false;
        frame_fallback_ = false;
        frame_shared_ = false;
        second_begun_ = false;
        first_prepare_seen_ = false;
        first_atlas_seen_ = false;
        second_prepare_suppressed_ = false;
        mode_ = Mode::atlas;
        mode_decided_ = false;
        canonical_ = 0;
        canonical_valid_ = false;
        frame_serial_ = 0;
        light_capture_count_ = 0;
        clear_captures();
        reason_ = enabled_ ? Reason::none : Reason::disabled;
    }

    // Eye boundary (0x351210).  `index` is the begin order of non-null views since Present;
    // the second view is the one that reuses the first view's shadow setup.
    bool on_eye_begin(int index, const ViewSignature& signature) noexcept {
        if (!enabled_) return false;
        if (index == 0) {
            begin_frame();
            first_ = signature;
            frame_started_ = true;
            if (!signature.valid) fail(Reason::unpublished);
            return !frame_fallback_;
        }
        if (!frame_started_) {
            fail(Reason::view_count);
            return false;
        }
        if (index == 1) {
            if (second_begun_) {
                fail(Reason::view_count);
                return false;
            }
            second_begun_ = true;
            if (!signature.valid) {
                fail(Reason::unpublished);
                return false;
            }
            if (!first_.valid) {
                fail(Reason::unpublished);
                return false;
            }
            if (signature.view == first_.view) {
                fail(Reason::view_mismatch);
                return false;
            }
            if (signature.gate != first_.gate ||
                (signature.flags & shadow_flag_mask) != (first_.flags & shadow_flag_mask)) {
                fail(Reason::light_set_mismatch);
                return false;
            }
            second_ = signature;
            paired_ = true;
            return !frame_fallback_;
        }
        fail(Reason::third_view);
        return false;
    }

    // Called for every first-view shadow-reading job with the value of its shadowMapsInfos
    // entry (item+0x58, hash 0x3719f109).  The first non-null value is the canonical one;
    // the per-pass value is preferred on redirect.
    void observe_shadow_param(int pass, std::uintptr_t view, bool has_value, std::uintptr_t value) noexcept {
        if (!enabled_ || frame_fallback_ || !frame_started_ || !first_.valid) return;
        if (view != first_.view) return;
        if (pass < 0 || pass >= pass_slots || !shadow_reader_pass(pass)) return;
        if (!has_value || !value) return;
        if (!captures_[pass].valid) {
            captures_[pass].valid = true;
            captures_[pass].value = value;
        }
        if (!canonical_valid_) {
            canonical_valid_ = true;
            canonical_ = value;
        }
    }

    // Called for a pass job.  True means suppress the job's callback (the executor still
    // finishes a command list, now empty).  A pass for the first view always runs.
    bool should_suppress(int pass, std::uintptr_t view, bool cached_list) noexcept {
        if (!enabled_ || frame_fallback_ || !mutating_shadow_pass(pass)) return false;
        if (!view) return false;
        if (frame_started_ && view == first_.view) {
            if (pass == pass_prepare) first_prepare_seen_ = true;
            if (atlas_writer_pass(pass)) first_atlas_seen_ = true;
            return false;
        }
        if (!paired_ || !second_.valid) return false;
        if (view != second_.view) {
            fail(Reason::third_view);
            return false;
        }
        // The first view's writer must be submitted before the second view's.  In setup mode
        // the shared "default" object is only valid for the second view if the first view's
        // PREPARE ran; the atlas writers need their first-view counterparts in both modes.
        if (!mode_decided_) decide_mode();
        if (mode_ == Mode::setup ? !first_prepare_seen_ : !first_atlas_seen_) {
            fail(Reason::ordering);
            return false;
        }
        if (mode_ == Mode::atlas && !atlas_writer_pass(pass)) return false;
        if (!(skip_mask_ & skip_bit(pass))) return false;   // v1008: live A/B of which second-view writers are skipped
        if (cached_list) {
            fail(Reason::cached_job);
            return false;
        }
        if (!frame_shared_) {
            frame_shared_ = true;
            ++counters.frames_shared;
            // A setup-mode reuse clears the last "setup unavailable" note; an atlas-only reuse
            // does not, so the status keeps showing why setup was not used until it is.
            if (mode_ == Mode::setup && reason_ != Reason::none && reason_ != Reason::disabled)
                reason_ = Reason::none;
        }
        if (pass == pass_prepare) second_prepare_suppressed_ = true;
        ++counters.second_view_skipped_jobs;
        return true;
    }

    // Returns true and the first view's setup resource when this second-view reading job must
    // be redirected.  In setup mode every reading pass answers true (the canonical value is
    // used when the exact pass was not captured), so a frame can never be half redirected.
    bool redirect_shadow_param(int pass, std::uintptr_t view, std::uintptr_t& value) noexcept {
        if (!enabled_ || frame_fallback_ || !paired_ || view != second_.view) return false;
        if (!mode_decided_) decide_mode();
        if (mode_ != Mode::setup) return false;
        if (pass < 0 || pass >= pass_slots || !shadow_reader_pass(pass)) return false;
        std::uintptr_t chosen = 0;
        if (captures_[pass].valid) chosen = captures_[pass].value;
        else if (canonical_valid_) chosen = canonical_;
        if (!chosen) {
            // The mode was only entered with a canonical value; if it disappeared, stop
            // redirecting for the rest of the frame rather than emit a mixed setup.
            frame_fallback_ = true;
            ++counters.fallbacks;
            ++counters.setup_fallbacks;
            if (reason_ == Reason::none || reason_ == Reason::disabled) reason_ = Reason::setup_unavailable;
            return false;
        }
        value = chosen;
        return true;
    }

    // Called by the hook only after it actually wrote the redirect into the job item.  A
    // redirect decision whose table entry is absent or already equal is not counted.
    void note_redirect_applied() noexcept {
        if (!enabled_ || frame_fallback_) return;
        ++counters.setup_redirects;
    }

    // v1009: TLFLIGHTCULL decides whether this job captures or restores the per-light
    // shadow-map-info links.  The first view always captures; the second view restores only in
    // setup mode after its own PREPARE was actually suppressed (if the live skip mask lets
    // PREPARE run, that PREPARE wrote the second view's own links and must not be overridden).
    // Never calls decide_mode for the first view: the mode needs the first view's reader
    // capture, which is recorded after TLFLIGHTCULL in pass order.
    LightAction light_action(int pass, std::uintptr_t view) noexcept {
        if (!enabled_ || frame_fallback_ || pass != pass_tlflightcull) return LightAction::none;
        if (!frame_started_ || !first_.valid) return LightAction::none;
        if (view == first_.view) return LightAction::capture;
        if (!paired_ || !second_.valid || view != second_.view) return LightAction::none;
        if (!mode_decided_) decide_mode();
        return mode_ == Mode::setup && second_prepare_suppressed_ ? LightAction::restore : LightAction::none;
    }

    // Record one light's PREPARE link (light+0x1a8) and the entry's record count (entry+0x18,
    // low 24 bits) seen during the first view's TLFLIGHTCULL.  Null links are not recorded; a
    // repeated light updates its entry and count.
    void capture_light(std::uintptr_t light, std::uintptr_t info, std::uint32_t records = 0) noexcept {
        if (!enabled_ || frame_fallback_ || !light || !info) return;
        for (unsigned i = 0; i < light_capture_count_; ++i) {
            if (light_captures_[i].light == light) {
                light_captures_[i].info = info;
                light_captures_[i].records = records & 0xffffffu;
                ++counters.light_captures;
                return;
            }
        }
        if (light_capture_count_ >= light_capture_capacity) return;
        light_captures_[light_capture_count_++] = LightCapture{light, info, records & 0xffffffu};
        ++counters.light_captures;
    }

    // The first view's recorded link and record count for this light.  False when the first
    // view did not list the light (a second-view-only light has no first-view shadow entry).
    bool find_light(std::uintptr_t light, std::uintptr_t& info, std::uint32_t& records) const noexcept {
        if (!light) return false;
        for (unsigned i = 0; i < light_capture_count_; ++i) {
            if (light_captures_[i].light == light) {
                info = light_captures_[i].info;
                records = light_captures_[i].records;
                return info != 0;
            }
        }
        return false;
    }
    bool find_light(std::uintptr_t light, std::uintptr_t& info) const noexcept {
        std::uint32_t records = 0;
        return find_light(light, info, records);
    }

    void note_light_restored() noexcept {
        if (!enabled_ || frame_fallback_) return;
        ++counters.light_restores;
    }
    // v1011: the entry's record count was written back with the link.
    void note_light_record_restored() noexcept {
        if (!enabled_ || frame_fallback_) return;
        ++counters.light_record_restores;
    }
    void note_light_miss() noexcept {
        if (!enabled_ || frame_fallback_) return;
        ++counters.light_misses;
    }
    unsigned light_capture_count() const noexcept {return light_capture_count_;}

    // A per-frame precondition failed after mode selection (or a job cannot be suppressed).
    void fail(Reason reason) noexcept {
        if (!enabled_) return;
        if (!frame_fallback_) {
            frame_fallback_ = true;
            ++counters.fallbacks;
        }
        if (reason_ == Reason::none || reason_ == Reason::disabled) reason_ = reason;
    }
    // Installation failures happen outside a frame and must still be reported.
    void fail_installation(Reason reason) noexcept {
        if (!enabled_) return;
        ++counters.fallbacks;
        reason_ = reason;
        frame_fallback_ = true;
    }
    void note_pair_ok() noexcept {
        if (enabled_ && !frame_fallback_ && reason_ != Reason::none) reason_ = Reason::none;
    }

    // v1008: which of the five second-view writers are skipped (bit per pass: prepare 1, collect 2, gather 4, world 8, dynamic 16).
    static std::uint32_t skip_bit(int pass) noexcept {
        return pass == pass_prepare ? 1u : pass == pass_collect ? 2u : pass == pass_gather ? 4u : pass == pass_world ? 8u : pass == pass_dynamic ? 16u : 0u;
    }
    void set_skip_mask(std::uint32_t mask) noexcept {skip_mask_ = mask & 31u;}
    std::uint32_t skip_mask() const noexcept {return skip_mask_;}
    Reason reason() const noexcept {return reason_;}
    Mode mode() const noexcept {return mode_;}
    bool mode_decided() const noexcept {return mode_decided_;}
    bool frame_fallback() const noexcept {return frame_fallback_;}
    bool frame_shared() const noexcept {return frame_shared_;}
    bool paired() const noexcept {return paired_;}
    bool prepare_seen() const noexcept {return first_prepare_seen_;}
    bool second_prepare_suppressed() const noexcept {return second_prepare_suppressed_;}
    const ViewSignature& first() const noexcept {return first_;}
    const ViewSignature& second() const noexcept {return second_;}
    const Counters& stats() const noexcept {return counters;}
    // 0 = first (reuse source) view, 1 = second view, -1 = neither.  The probe uses this to
    // label both views without exposing the signatures.
    int view_index(std::uintptr_t view) const noexcept {
        if (!view) return -1;
        if (frame_started_ && view == first_.view) return 0;
        if (paired_ && view == second_.view) return 1;
        return -1;
    }
    std::uint64_t frame_serial() const noexcept {return frame_serial_;}
    bool has_capture(int pass) const noexcept {
        return pass >= 0 && pass < pass_slots && captures_[pass].valid;
    }
    std::uintptr_t capture(int pass) const noexcept {
        return pass >= 0 && pass < pass_slots ? captures_[pass].value : 0;
    }
    std::uintptr_t canonical() const noexcept {return canonical_;}

    static constexpr std::uint32_t shadow_flag_mask = 0x0002u; // view+0x3a34 bit 1 (directional/gather gate)

private:
    struct Capture {
        std::uintptr_t value{};
        bool valid{};
    };
    void begin_frame() noexcept {
        if (!frame_fallback_ && frame_shared_) reason_ = Reason::none;
        ++frame_serial_;
        first_ = {};
        second_ = {};
        paired_ = false;
        frame_started_ = true;
        frame_fallback_ = false;
        frame_shared_ = false;
        second_begun_ = false;
        first_prepare_seen_ = false;
        first_atlas_seen_ = false;
        second_prepare_suppressed_ = false;
        mode_ = Mode::atlas;
        mode_decided_ = false;
        canonical_ = 0;
        canonical_valid_ = false;
        light_capture_count_ = 0;
        clear_captures();
    }
    void clear_captures() noexcept {
        for (Capture& capture : captures_) capture = Capture{};
    }
    void decide_mode() noexcept {
        if (mode_decided_) return;
        mode_decided_ = true;
        if (!enabled_) {mode_ = Mode::atlas; return;}
        if (!atlas_only_ && canonical_valid_) {
            mode_ = Mode::setup;
            return;
        }
        mode_ = Mode::atlas;
        if (!atlas_only_) {
            ++counters.setup_fallbacks;
            if (reason_ == Reason::none || reason_ == Reason::disabled) reason_ = Reason::setup_unavailable;
        }
    }

    bool enabled_{};
    std::uint32_t skip_mask_{31u};
    bool atlas_only_{};
    bool frame_started_{};
    bool paired_{};
    bool frame_fallback_{};
    bool frame_shared_{};
    bool second_begun_{};
    bool first_prepare_seen_{};
    bool first_atlas_seen_{};
    bool second_prepare_suppressed_{};
    bool mode_decided_{};
    bool canonical_valid_{};
    Mode mode_{Mode::atlas};
    Reason reason_{Reason::disabled};
    ViewSignature first_{}, second_{};
    std::uintptr_t canonical_{};
    std::uint64_t frame_serial_{};
    Capture captures_[pass_slots]{};
    LightCapture light_captures_[light_capture_capacity]{};
    unsigned light_capture_count_{};
    Counters counters{};
};

} // namespace shadow_share
} // namespace d2vr
