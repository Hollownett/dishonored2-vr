#pragma once
// Static shadow cache (plan 2A.1), observe-only runtime.  OFF unless build/static-shadow-cache exists.
//
// What this does today: nothing to the frame.  It never suppresses a job, never writes engine memory
// and never issues a D3D call that changes state.  With the knob it
//   - keys every frame from the first view's TLFLIGHTCULL light walk that shadow_share already does
//     (light -> shadow entry -> published records), hashing the GPU-visible records (0x84 bytes per face,
//     the bytes TLFLIGHTCULL copies into cpu_shadowMapsInfos), and runs the decision model as the active
//     cache would (a capture is assumed to succeed): the status then says how often a cache WOULD hit;
//   - counts the SHADOWS_WORLD / SHADOWS_DYNAMIC jobs per frame by path (parallel cascade slice vs atlas
//     slice, item+0xf8 against the global parallel slice count) and times their callbacks (CPU);
// and with build/static-shadow-cache-probe also
//   - reads, during first-view atlas jobs, the viewport rect and depth target of the draws (RSGetViewports /
//     OMGetRenderTargets, read-only) and hashes the draw stream per rect, so the status shows whether the
//     world-pass rects and caster draws change while the light key does not (the key's blind spots);
//   - records the atlas texture description (size, format, slices: the cache's memory cost);
//   - checks, on the immediate context, that every SHADOWS_WORLD list executes before the frame's first
//     SHADOWS_DYNAMIC list (the capture point the active design needs).
// The active path (skip the first view's world atlas jobs, restore the copy) is described in
// docs/STATIC-SHADOW-CACHE.md and is NOT implemented: it needs the live checks listed there first.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include "shadow_share_model.hpp"
#include "static_shadow_cache_model.hpp"

namespace d2vr {
namespace static_shadow_cache {

inline constexpr int pass_world = 20;    // ARK_RENDERPASS_SHADOWS_WORLD
inline constexpr int pass_dynamic = 21;  // ARK_RENDERPASS_SHADOWS_DYNAMIC
inline constexpr int pass_tlflightcull = 26;
// 0x32d4b0 compares item+0xf8 with this global: slices below it take the parallel (sun cascade) path
// 0x32ee50, the rest the spot/omni atlas path 0x32d6e0 (docs/ENGINE-PERF-PLAN.md lines 1062-1065).
inline constexpr std::uintptr_t parallel_slice_count_rva = 0x397a2f8;
// TLFLIGHTCULL publishes 0x84 bytes per record from [entry+0x10] + i*0x100 (0x2f7710..0x2f7890,
// docs/ENGINE-PERF-PLAN.md lines 1929-1931): the GPU-visible part of each face's shadow info.
inline constexpr unsigned record_stride = 0x100;
inline constexpr unsigned record_bytes = 0x84;
inline constexpr unsigned max_records = 6;

enum class Path : int {none = 0, parallel = 1, atlas = 2};
inline Path path_for_slice(std::uint32_t slice, std::uint32_t parallel_slices) noexcept {
    return slice < parallel_slices ? Path::parallel : Path::atlas;
}

// A 64-bit hash over whole words (the record block is hashed once per light per frame).
inline std::uint64_t hash_words(const void* data, unsigned bytes, std::uint64_t seed) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed ^ (0x9e3779b97f4a7c15ull * (bytes + 1u));
    unsigned i = 0;
    for (; i + 8 <= bytes; i += 8) {
        std::uint64_t v = 0;
        for (unsigned b = 0; b < 8; ++b) v |= static_cast<std::uint64_t>(p[i + b]) << (8 * b);
        h = mix64(h ^ v);
    }
    std::uint64_t tail = 0;
    for (unsigned b = 0; i + b < bytes; ++b) tail |= static_cast<std::uint64_t>(p[i + b]) << (8 * b);
    return mix64(h ^ tail ^ 0x5851f42d4c957f2dull);
}

// DXGI format numbers (dxgiformat.h) of the depth/typeless formats a shadow atlas can use.
inline unsigned depth_format_bytes(unsigned format) noexcept {
    switch (format) {
    case 19: case 20: case 21: case 22: return 8;          // R32G8X24_TYPELESS, D32_FLOAT_S8X24_UINT, ...
    case 39: case 40: case 41: case 42: case 43: return 4; // R32_TYPELESS, D32_FLOAT, R32_FLOAT, R32_UINT, R32_SINT
    case 44: case 45: case 46: case 47: return 4;          // R24G8_TYPELESS, D24_UNORM_S8_UINT, ...
    case 53: case 54: case 55: case 56: case 57: case 58: case 59: return 2; // R16_TYPELESS, R16_FLOAT, D16_UNORM, ...
    default: return 0;
    }
}

// Per-job draw accumulator for the probe (one per job thread; merged into the frame at job end).
struct JobDraws {
    static constexpr unsigned rect_capacity = 64;
    std::uint64_t serial{};
    bool active{};
    std::uint32_t draws{};
    std::uint32_t rects{};
    std::uint32_t rect_overflow{};
    AtlasRect rect[rect_capacity]{};
    std::uint64_t stream[rect_capacity]{};
    int current{-1};
    void begin(std::uint64_t frame) noexcept {*this = JobDraws{}; serial = frame; active = true;}
    // One draw at viewport `r`.  The stream hash is order dependent within a rect (a job records its
    // draws in a fixed order); rects are combined order independently at merge time.
    void note(const AtlasRect& r, std::uint32_t count, std::uint32_t instances, std::uint32_t start, std::int32_t base) noexcept {
        if (!active) return;
        ++draws;
        if (current < 0 || rect[current] != r) {
            current = -1;
            for (unsigned i = 0; i < rects; ++i) if (rect[i] == r) {current = static_cast<int>(i); break;}
            if (current < 0) {
                if (rects >= rect_capacity) {++rect_overflow; return;}
                rect[rects] = r;
                stream[rects] = mix64(rect_bits(r) ^ 0x7c3b1a5d9e2f4683ull);
                current = static_cast<int>(rects++);
            }
        }
        const std::uint64_t args = static_cast<std::uint64_t>(count) ^ (static_cast<std::uint64_t>(instances) << 32);
        const std::uint64_t at = static_cast<std::uint64_t>(start) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(base)) << 32);
        stream[current] = mix64(stream[current] ^ args) + mix64(at ^ 0x2127599bf4325c37ull);
    }
};

struct AtlasResource {
    std::uintptr_t resource{};
    std::uint32_t width{}, height{}, format{}, array_size{}, mips{};
    std::uint32_t paths{};   // bit 1 parallel, bit 2 atlas
    std::uint64_t bytes() const noexcept {
        return static_cast<std::uint64_t>(width) * height * (array_size ? array_size : 1u) * depth_format_bytes(format);
    }
};

class Runtime {
public:
    // requested: the knob existed at session start (hooks wired, status block present).
    void configure(bool requested, bool on, std::uint32_t refresh_period, bool probe) {
        std::lock_guard<std::mutex> lock(mutex_);
        requested_ = requested;
        Config config;
        config.enabled = requested && on;
        config.refresh_period = refresh_period ? refresh_period : 8u;
        config.warmup_frames = 2;
        model_.configure(config);
        model_.reset();
        clear_frames();
        jobs_ = {};
        probe_ = {};
        order_ = {};
        atlas_count_ = 0;
        active_.store(config.enabled, std::memory_order_release);
        probe_on_.store(config.enabled && probe, std::memory_order_release);
    }
    void set_live(bool on, std::uint32_t refresh_period, bool probe) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!requested_) return;
        Config config = model_.config();
        const bool changed = config.enabled != on || (refresh_period && config.refresh_period != refresh_period);
        config.enabled = on;
        if (refresh_period) config.refresh_period = refresh_period;
        if (changed) {
            model_.configure(config);
            model_.reset();
            clear_frames();
        }
        active_.store(on, std::memory_order_release);
        probe_on_.store(on && probe, std::memory_order_release);
    }
    // XR recovery / resolution change: drop the (virtual) cache and any half-built frame.
    void invalidate() {
        std::lock_guard<std::mutex> lock(mutex_);
        model_.invalidate(Reason::external);
        clear_frames();
    }
    bool requested() const noexcept {return requested_;}
    bool active() const noexcept {return active_.load(std::memory_order_acquire);}
    bool probe() const noexcept {return probe_on_.load(std::memory_order_acquire);}

    // The first view's TLFLIGHTCULL job of frame `serial` walked its light list.  `previous` is the
    // share model's outcome of the frame before `serial`: it completes the pending frame, which is
    // then decided.  Several TLFLIGHTCULL jobs of one frame append (the light is the identity).
    void submit_lights(std::uint64_t serial, const LightKey* keys, unsigned count, bool readable,
                       std::uint32_t parallel_slices, const shadow_share::FrameOutcome& previous) {
        if (!active()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_valid_ && pending_serial_ != serial) finalize(previous);
        if (!pending_valid_) {
            pending_valid_ = true;
            pending_serial_ = serial;
            pending_count_ = 0;
            pending_readable_ = true;
            pending_overflow_ = false;
        }
        pending_readable_ = pending_readable_ && readable;
        pending_slices_ = parallel_slices;
        for (unsigned i = 0; i < count; ++i) {
            bool merged = false;
            for (unsigned j = 0; j < pending_count_; ++j)
                if (pending_[j].light == keys[i].light) {pending_[j] = keys[i]; merged = true; break;}
            if (merged) continue;
            if (pending_count_ >= light_capacity) {pending_overflow_ = true; continue;}
            pending_[pending_count_++] = keys[i];
        }
    }

    // One SHADOWS_WORLD / SHADOWS_DYNAMIC job that actually ran its callback.
    void note_job(std::uint64_t serial, int view_index, int pass, Path path, std::uint64_t cpu_ns) {
        if (!active() || view_index != 0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        FrameJobs& f = frame_slot(serial);
        const unsigned k = (pass == pass_dynamic ? 2u : 0u) + (path == Path::atlas ? 1u : 0u);
        ++f.jobs[k];
        f.cpu_ns[k] += cpu_ns;
    }
    void merge_draws(const JobDraws& job) {
        if (!probe() || !job.active) return;
        std::lock_guard<std::mutex> lock(mutex_);
        FrameJobs& f = frame_slot(job.serial);
        f.draws += job.draws;
        f.rects += job.rects;
        f.rect_overflow += job.rect_overflow;
        for (unsigned i = 0; i < job.rects; ++i) {
            f.rect_set += mix64(rect_bits(job.rect[i]) ^ 0x3c6ef372fe94f82bull);
            f.draw_sig += mix64(job.stream[i]);
        }
        f.probed = true;
    }
    void note_resource(const AtlasResource& r, Path path) {
        if (!probe() || !r.resource) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (unsigned i = 0; i < atlas_count_; ++i)
            if (atlas_[i].resource == r.resource) {atlas_[i].paths |= 1u << static_cast<unsigned>(path); return;}
        if (atlas_count_ >= 4) {++probe_.resource_overflow; return;}
        atlas_[atlas_count_] = r;
        atlas_[atlas_count_].paths = 1u << static_cast<unsigned>(path);
        ++atlas_count_;
    }
    // The immediate context executed a command list tagged `pass` during Present interval `present`.
    void note_execute(int pass, std::uint64_t present) {
        if (!probe() || (pass != pass_world && pass != pass_dynamic)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (present != order_.present) {
            if (order_.bad_this_frame) ++order_.frames_out_of_order;
            if (order_.present) ++order_.frames;
            order_.present = present;
            order_.saw_dynamic = false;
            order_.bad_this_frame = false;
        }
        if (pass == pass_world) {
            ++order_.world_lists;
            if (order_.saw_dynamic) {++order_.world_after_dynamic; order_.bad_this_frame = true;}
        } else {
            ++order_.dynamic_lists;
            order_.saw_dynamic = true;
        }
    }

    Counters stats() const {std::lock_guard<std::mutex> lock(mutex_); return model_.stats();}
    // Lab access: the last decision the observe path made.
    Decision last_decision() const {std::lock_guard<std::mutex> lock(mutex_); return last_decision_;}
    std::uint64_t probe_unkeyed_draw_changes() const {std::lock_guard<std::mutex> lock(mutex_); return probe_.unkeyed_draw_changes;}
    std::uint64_t probe_unkeyed_rect_changes() const {std::lock_guard<std::mutex> lock(mutex_); return probe_.unkeyed_rect_changes;}
    std::uint64_t order_world_after_dynamic() const {std::lock_guard<std::mutex> lock(mutex_); return order_.world_after_dynamic;}

    std::string json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Counters& c = model_.stats();
        const Config& config = model_.config();
        std::ostringstream out;
        out << "{\"enabled\":" << (active() ? "true" : "false")
            << ",\"mode\":\"observe\""
            << ",\"refresh_period\":" << config.refresh_period
            << ",\"warmup\":" << config.warmup_frames
            << ",\"frames\":" << c.frames
            << ",\"hits\":" << c.hits
            << ",\"partials\":" << c.partials
            << ",\"misses\":" << c.misses
            << ",\"invalidations\":" << c.invalidations
            << ",\"refreshes\":" << c.refreshes
            << ",\"bypasses\":" << c.bypasses
            << ",\"captures\":" << c.captures
            << ",\"hit_rate\":" << (c.frames ? static_cast<double>(c.hits) / static_cast<double>(c.frames) : 0.0)
            << ",\"lights_last\":" << c.last_lights
            << ",\"records_last\":" << c.last_records
            << ",\"cache_lights\":" << model_.cached_lights()
            << ",\"last_reason\":\"" << reason_name(model_.last_reason()) << "\""
            << ",\"reasons\":{";
        bool first = true;
        for (unsigned i = 1; i < reason_count; ++i) {
            if (!c.reasons[i]) continue;
            if (!first) out << ',';
            first = false;
            out << '"' << reason_name(static_cast<Reason>(i)) << "\":" << c.reasons[i];
        }
        const double frames = jobs_.frames ? static_cast<double>(jobs_.frames) : 1.0;
        out << "},\"jobs_per_frame\":{\"world_parallel\":" << jobs_.jobs[0] / frames
            << ",\"world_atlas\":" << jobs_.jobs[1] / frames
            << ",\"dynamic_parallel\":" << jobs_.jobs[2] / frames
            << ",\"dynamic_atlas\":" << jobs_.jobs[3] / frames
            << "},\"cpu_us_per_frame\":{\"world_parallel\":" << jobs_.cpu_ns[0] / frames / 1000.0
            << ",\"world_atlas\":" << jobs_.cpu_ns[1] / frames / 1000.0
            << ",\"dynamic_parallel\":" << jobs_.cpu_ns[2] / frames / 1000.0
            << ",\"dynamic_atlas\":" << jobs_.cpu_ns[3] / frames / 1000.0
            << "},\"job_frames\":" << jobs_.frames
            << ",\"probe\":{\"enabled\":" << (probe() ? "true" : "false")
            << ",\"frames\":" << probe_.frames
            << ",\"rects_per_frame\":" << (probe_.frames ? static_cast<double>(probe_.rects) / probe_.frames : 0.0)
            << ",\"draws_per_frame\":" << (probe_.frames ? static_cast<double>(probe_.draws) / probe_.frames : 0.0)
            << ",\"rect_overflow\":" << probe_.rect_overflow
            << ",\"key_changed\":" << probe_.key_changed
            << ",\"rect_set_changed\":" << probe_.rect_set_changed
            << ",\"draw_sig_changed\":" << probe_.draw_sig_changed
            << ",\"unkeyed_rect_changes\":" << probe_.unkeyed_rect_changes
            << ",\"unkeyed_draw_changes\":" << probe_.unkeyed_draw_changes
            << ",\"resource_overflow\":" << probe_.resource_overflow
            << ",\"atlas\":[";
        for (unsigned i = 0; i < atlas_count_; ++i) {
            const AtlasResource& r = atlas_[i];
            if (i) out << ',';
            out << "{\"width\":" << r.width << ",\"height\":" << r.height << ",\"format\":" << r.format
                << ",\"array\":" << r.array_size << ",\"mips\":" << r.mips << ",\"paths\":" << r.paths
                << ",\"bytes\":" << r.bytes() << '}';
        }
        out << "],\"order\":{\"frames\":" << order_.frames
            << ",\"world_lists\":" << order_.world_lists
            << ",\"dynamic_lists\":" << order_.dynamic_lists
            << ",\"world_after_dynamic\":" << order_.world_after_dynamic
            << ",\"frames_out_of_order\":" << order_.frames_out_of_order << "}}}";
        return out.str();
    }

private:
    struct FrameJobs {
        std::uint64_t serial{};
        bool used{};
        std::uint64_t jobs[4]{};      // world parallel, world atlas, dynamic parallel, dynamic atlas
        std::uint64_t cpu_ns[4]{};
        bool probed{};
        std::uint64_t draws{}, rects{}, rect_overflow{};
        std::uint64_t rect_set{}, draw_sig{};
    };
    FrameJobs& frame_slot(std::uint64_t serial) {
        FrameJobs& f = frames_[serial % 4];
        if (!f.used || f.serial != serial) {f = FrameJobs{}; f.used = true; f.serial = serial;}
        return f;
    }
    void clear_frames() {
        for (FrameJobs& f : frames_) f = FrameJobs{};
        pending_valid_ = false;
        pending_count_ = 0;
        last_key_valid_ = false;
        last_probe_valid_ = false;
    }
    // The pending frame is complete: decide it as the active cache would (the observe path assumes
    // every capture succeeds), then compare its probe data with the previous decided frame.
    void finalize(const shadow_share::FrameOutcome& previous) {
        FrameInput in;
        in.lights = pending_;
        in.light_count = pending_count_;
        in.key_readable = pending_readable_;
        in.overflow = pending_overflow_;
        in.parallel_slices = pending_slices_;
        in.atlas_generation = atlas_generation();
        const bool outcome_known = previous.valid && previous.serial == pending_serial_;
        in.share_active = outcome_known && !previous.fallback;
        in.secondary = outcome_known && previous.secondary;
        last_decision_ = model_.decide(in);
        if (last_decision_.capture) model_.capture_result(true);

        std::uint64_t key = 0;
        for (unsigned i = 0; i < pending_count_; ++i) key += light_hash(pending_[i]);
        key = mix64(key ^ pending_count_ ^ (static_cast<std::uint64_t>(pending_slices_) << 40));
        const bool key_same = last_key_valid_ && key == last_key_;
        if (last_key_valid_ && !key_same) ++probe_.key_changed;
        last_key_ = key;
        last_key_valid_ = pending_readable_ && !pending_overflow_;

        FrameJobs& f = frames_[pending_serial_ % 4];
        if (f.used && f.serial == pending_serial_) {
            ++jobs_.frames;
            for (unsigned k = 0; k < 4; ++k) {jobs_.jobs[k] += f.jobs[k]; jobs_.cpu_ns[k] += f.cpu_ns[k];}
            if (f.probed) {
                ++probe_.frames;
                probe_.draws += f.draws;
                probe_.rects += f.rects;
                probe_.rect_overflow += f.rect_overflow;
                if (last_probe_valid_) {
                    const bool rects_same = f.rect_set == last_rect_set_;
                    const bool draws_same = f.draw_sig == last_draw_sig_;
                    if (!rects_same) ++probe_.rect_set_changed;
                    if (!draws_same) ++probe_.draw_sig_changed;
                    // The key's blind spots: the world pass changed but the light key did not.
                    if (key_same && !rects_same) ++probe_.unkeyed_rect_changes;
                    if (key_same && !draws_same) ++probe_.unkeyed_draw_changes;
                }
                last_rect_set_ = f.rect_set;
                last_draw_sig_ = f.draw_sig;
                last_probe_valid_ = true;
            } else {
                last_probe_valid_ = false;
            }
            f = FrameJobs{};
        }
        pending_valid_ = false;
        pending_count_ = 0;
    }
    std::uint64_t atlas_generation() const {
        std::uint64_t g = 0;
        for (unsigned i = 0; i < atlas_count_; ++i)
            if (atlas_[i].paths & (1u << static_cast<unsigned>(Path::atlas)))
                g += mix64(static_cast<std::uint64_t>(atlas_[i].resource) ^ atlas_[i].bytes());
        return g;
    }

    mutable std::mutex mutex_;
    bool requested_{};
    std::atomic<bool> active_{false};
    std::atomic<bool> probe_on_{false};
    Model model_{};
    Decision last_decision_{};
    // The frame being collected (first-view TLFLIGHTCULL keys).
    bool pending_valid_{};
    std::uint64_t pending_serial_{};
    LightKey pending_[light_capacity]{};
    unsigned pending_count_{};
    bool pending_readable_{true};
    bool pending_overflow_{};
    std::uint32_t pending_slices_{};
    FrameJobs frames_[4]{};
    struct {std::uint64_t frames{}; std::uint64_t jobs[4]{}; std::uint64_t cpu_ns[4]{};} jobs_{};
    struct {
        std::uint64_t frames{}, draws{}, rects{}, rect_overflow{}, resource_overflow{};
        std::uint64_t key_changed{}, rect_set_changed{}, draw_sig_changed{}, unkeyed_rect_changes{}, unkeyed_draw_changes{};
    } probe_{};
    struct {
        std::uint64_t present{}, frames{}, world_lists{}, dynamic_lists{}, world_after_dynamic{}, frames_out_of_order{};
        bool saw_dynamic{}, bad_this_frame{};
    } order_{};
    AtlasResource atlas_[4]{};
    unsigned atlas_count_{};
    std::uint64_t last_key_{};
    bool last_key_valid_{};
    std::uint64_t last_rect_set_{}, last_draw_sig_{};
    bool last_probe_valid_{};
};

} // namespace static_shadow_cache
} // namespace d2vr
