#pragma once
// v1009: second-view shadow sharing, on by default.
//
// v1005 headset result: skipping only SHADOWS_WORLD + SHADOWS_DYNAMIC kept the first view's
// atlas but ran the second view's own PREPARE, so the right eye sampled the atlas with the
// second view's cascade fit ("shadows look wrong at some spots").  v1004 (all five writers
// skipped) lost the right eye's shadows because the per-view render-graph resource named
// shadowMapsInfos was never produced for the second view.
//
// v1006 setup mode combines the two: the second view's five shadow writers are suppressed
// again (so the single shared pass-manager "default" setup object and the depth atlas keep
// the first view's contents), and each second-view shadow-reading job (FORWARD_WORLD,
// FORWARD_DYNAMIC, FOG, SCREENSHADOWS, RENDER_WATER) has its shadowMapsInfos parameter
// temporarily pointed at the first view's captured resource.  The only engine memory written
// is the one 8-byte value in that job item's own parameter table, restored before the
// executor returns.
//
// v1009: that resource is not the whole per-view state.  GATHER_LIGHTS clears each light's
// shadow-map-info link (light+0x1a8) for every view and PREPARE re-links it; with the second
// view's PREPARE skipped, TLFLIGHTCULL copies no per-light shadow records and the right eye
// loses the atlas (local-light) shadows, including the movable props.  The runtime captures
// (light -> link) during the first view's TLFLIGHTCULL job and writes those links around the
// second view's TLFLIGHTCULL job, restoring the old values afterwards.  The atlas writers stay
// skipped.
//
// v1011: the link alone was not enough.  The per-view job builder 0x351780 also zeroes the
// shared entries' record counts (entry+0x18) before each view, so the second view's PREPARE-less
// TLFLIGHTCULL saw count 0 even with the link restored and published nothing (the upload gate at
// 0x2f7ffc then skips the GPU buffer).  The capture/restore now carries the count too.
//
// Fallback is per frame: if no first-view shadowMapsInfos was captured before the second view
// begins, the frame uses the v1005 atlas-only behaviour (the second view's setup runs, only
// the atlas writers are skipped) and reports setup_unavailable.  build/shadow-share-atlas-only
// forces that behaviour for A/B; build/no-shadow-share turns everything off.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include "shadow_share_model.hpp"

namespace d2vr {
namespace shadow_share {

// v1011 diagnostic (build/shadow-share-probe, OFF by default): the first frame of each second
// logs every TLFLIGHTCULL job for both views (up to 8 per view).  `records` is the entry+0x18
// record count TLFLIGHTCULL reads; `published` is the entry+0x620 base index written after the
// callback (-1 = not published).
inline constexpr unsigned probe_light_capacity = 64;
struct ProbeLight {
    std::uintptr_t light{};
    std::uintptr_t info{};
    std::uint32_t records{};
    std::int32_t published{-1};
};

class Runtime {
public:
    Runtime() {model_.configure(false, false);}

    void configure(bool enabled, bool atlas_only) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        atlas_only_ = atlas_only;
        installed_ = false;
        install_failed_ = false;
        model_.configure(enabled, atlas_only);
        if (enabled) model_.reset();
        active_.store(enabled, std::memory_order_release);
    }
    void set_installed(bool installed) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        installed_ = installed;
        install_failed_ = !installed && enabled_;
        if (install_failed_) model_.fail_installation(Reason::prologue);
        active_.store(enabled_ && installed, std::memory_order_release);
    }
    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        model_.reset();
    }
    // The dispatcher hook is being stopped (session teardown or XR recovery): no further
    // suppression, and the status must not claim the hook is live.
    void stop() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        installed_ = false;
        model_.configure(false, atlas_only_);
        active_.store(false, std::memory_order_release);
    }
    // Live on/off for a same-spot A/B. Only meaningful when the session started with the knob
    // (hooks installed); turning on resets the model so the next frame starts clean.
    void set_live(bool on) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (on == enabled_) return;
        enabled_ = on;
        model_.configure(on, atlas_only_);
        if (on) model_.reset();
        active_.store(enabled_ && installed_, std::memory_order_release);
    }
    void set_skip_mask(std::uint32_t mask) noexcept {std::lock_guard<std::mutex> lock(mutex_);model_.set_skip_mask(mask);}
    bool enabled() const noexcept {return active_.load(std::memory_order_acquire);}
    bool knob_on() const noexcept {return enabled_;}
    bool atlas_only() const noexcept {return atlas_only_;}

    void on_eye_begin(int index, std::uintptr_t view, std::uint32_t gate, std::uint32_t flags, bool valid) noexcept {
        if (!active_.load(std::memory_order_acquire)) return;
        ViewSignature signature{view, gate, flags, valid && view != 0};
        std::lock_guard<std::mutex> lock(mutex_);
        model_.on_eye_begin(index, signature);
    }
    // One call per pass job: records a first-view shadow resource and decides suppression or
    // redirect for this job.  `has_shadow_maps_infos`/`value` come from the job's +0x58 table.
    JobAction on_job(int pass, std::uintptr_t view, bool cached_list,
                     bool has_shadow_maps_infos, std::uintptr_t shadow_maps_infos) noexcept {
        JobAction action;
        if (!active_.load(std::memory_order_acquire)) return action;
        std::lock_guard<std::mutex> lock(mutex_);
        model_.observe_shadow_param(pass, view, has_shadow_maps_infos, shadow_maps_infos);
        action.suppress = model_.should_suppress(pass, view, cached_list);
        if (!action.suppress) action.redirect = model_.redirect_shadow_param(pass, view, action.redirect_value);
        return action;
    }
    // Kept for the model lab and for callers that only need the suppression answer.
    bool should_suppress(int pass, std::uintptr_t view, bool cached_list) noexcept {
        if (!active_.load(std::memory_order_acquire)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return model_.should_suppress(pass, view, cached_list);
    }
    // The hook wrote the first view's resource into the second view's job table.
    void note_redirect_applied() noexcept {
        if (!active_.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        model_.note_redirect_applied();
    }

    // v1009: per-light shadow-map-info links at TLFLIGHTCULL (pass 26).
    LightAction light_action(int pass, std::uintptr_t view) noexcept {
        if (!active_.load(std::memory_order_acquire)) return LightAction::none;
        std::lock_guard<std::mutex> lock(mutex_);
        return model_.light_action(pass, view);
    }
    void capture_light(std::uintptr_t light, std::uintptr_t info, std::uint32_t records = 0) noexcept {
        if (!active_.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        model_.capture_light(light, info, records);
    }
    bool find_light(std::uintptr_t light, std::uintptr_t& info, std::uint32_t& records) noexcept {
        if (!active_.load(std::memory_order_acquire)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return model_.find_light(light, info, records);
    }
    bool find_light(std::uintptr_t light, std::uintptr_t& info) noexcept {
        std::uint32_t records = 0;
        return find_light(light, info, records);
    }
    void note_light_restored() noexcept {
        if (!active_.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        model_.note_light_restored();
    }
    void note_light_miss() noexcept {
        if (!active_.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        model_.note_light_miss();
    }
    void note_light_record_restored() noexcept {
        if (!active_.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        model_.note_light_record_restored();
    }

    // v1011: opt-in TLFLIGHTCULL probe.  The path is configured once per session; enabling it
    // truncates the file.  The hook only pays one atomic load when the knob is absent.
    void configure_probe(bool enabled, const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        probe_path_ = path;
        probe_epoch_ = std::chrono::steady_clock::now();
        probe_second_ = ~std::uint64_t{0};
        probe_frame_ = ~std::uint64_t{0};
        probe_jobs_[0] = probe_jobs_[1] = 0;
        probe_lines_ = probe_bytes_ = probe_dropped_ = 0;
        if (enabled && !probe_path_.empty()) {
            if (probe_out_.is_open()) probe_out_.close();
            probe_out_.open(probe_path_, std::ios::out | std::ios::trunc);
            probe_enabled_.store(probe_out_.is_open(), std::memory_order_release);
        } else {
            if (probe_out_.is_open()) probe_out_.close();
            probe_enabled_.store(false, std::memory_order_release);
        }
    }
    void set_probe_enabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled == probe_enabled_.load(std::memory_order_acquire)) return;
        if (enabled && !probe_path_.empty()) {
            if (probe_out_.is_open()) probe_out_.close();
            probe_out_.open(probe_path_, std::ios::out | std::ios::trunc);
            probe_epoch_ = std::chrono::steady_clock::now();
            probe_second_ = ~std::uint64_t{0};
            probe_frame_ = ~std::uint64_t{0};
            probe_jobs_[0] = probe_jobs_[1] = 0;
            probe_lines_ = probe_bytes_ = 0;
            probe_enabled_.store(probe_out_.is_open(), std::memory_order_release);
        } else {
            if (probe_out_.is_open()) probe_out_.close();
            probe_enabled_.store(false, std::memory_order_release);
        }
    }
    bool probe_enabled() const noexcept {return probe_enabled_.load(std::memory_order_acquire);}
    // True for every TLFLIGHTCULL job (slices included) of the first frame in the current
    // second, up to a small cap per view.  Returns the model's frame serial and the view index
    // (0/1) for the JSON line.
    bool probe_begin(std::uintptr_t view, std::uint64_t& frame, int& view_index) noexcept {
        if (!probe_enabled()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        view_index = model_.view_index(view);
        if (view_index < 0 || view_index > 1) return false;
        const auto now = std::chrono::steady_clock::now();
        const auto second = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - probe_epoch_).count());
        if (second != probe_second_) {
            probe_second_ = second;
            probe_frame_ = ~std::uint64_t{0};
            probe_jobs_[0] = probe_jobs_[1] = 0;
        }
        frame = model_.frame_serial();
        if (probe_frame_ == ~std::uint64_t{0}) probe_frame_ = frame;
        if (frame != probe_frame_ || probe_jobs_[view_index] >= 8) return false;
        ++probe_jobs_[view_index];
        return true;
    }
    // One JSON line.  `record_words` are the first dwords of the cpu_shadowMapsInfos array.
    void probe_write(int view, std::uint64_t frame, std::uintptr_t light_list,
                     std::uintptr_t light_list_data, std::uintptr_t shadow_maps_infos,
                     const ProbeLight* lights, unsigned light_count, unsigned records_published,
                     const std::uint32_t* record_words, unsigned record_word_count) {
        if (!probe_enabled()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!probe_out_.is_open()) return;
        if (probe_bytes_ >= 8u * 1024u * 1024u) {
            ++probe_dropped_;
            probe_out_.close();
            probe_enabled_.store(false, std::memory_order_release);
            return;
        }
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ostringstream out;
        out << "{\"time_ms\":" << now_ms << ",\"frame\":" << frame << ",\"view\":" << view
            << ",\"pass\":26,\"lightList\":\"0x" << std::hex
            << static_cast<unsigned long long>(light_list) << "\",\"lightListData\":\"0x"
            << static_cast<unsigned long long>(light_list_data) << "\",\"shadowMapsInfos\":\"0x"
            << static_cast<unsigned long long>(shadow_maps_infos) << std::dec << "\",\"lights\":[";
        for (unsigned i = 0; i < light_count; ++i) {
            if (i) out << ',';
            out << "{\"light\":\"0x" << std::hex
                << static_cast<unsigned long long>(lights[i].light) << "\",\"info\":\"0x"
                << static_cast<unsigned long long>(lights[i].info) << "\",\"records\":"
                << std::dec << lights[i].records << ",\"published\":" << lights[i].published << '}';
        }
        out << "],\"records_published\":" << records_published << ",\"records\":[";
        for (unsigned i = 0; i < record_word_count; ++i) {
            if (i) out << ',';
            out << "\"0x" << std::hex << record_words[i] << std::dec << '"';
        }
        out << "]}\n";
        const std::string line = out.str();
        probe_out_ << line;
        probe_out_.flush();
        probe_bytes_ += line.size();
        ++probe_lines_;
    }

    std::string json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& stats = model_.stats();
        std::ostringstream out;
        out << "{\"enabled\":" << ((enabled_ && installed_) ? "true" : "false")
            << ",\"knob\":" << (enabled_ ? "true" : "false")
            << ",\"installed\":" << (installed_ ? "true" : "false")
            << ",\"mode\":\"" << mode_name(model_.mode()) << "\""
            << ",\"atlas_only\":" << (atlas_only_ ? "true" : "false")
            << ",\"skip_mask\":" << model_.skip_mask()
            << ",\"frames_shared\":" << stats.frames_shared
            << ",\"second_view_skipped_jobs\":" << stats.second_view_skipped_jobs
            << ",\"setup_redirects\":" << stats.setup_redirects
            << ",\"setup_fallbacks\":" << stats.setup_fallbacks
            << ",\"light_captures\":" << stats.light_captures
            << ",\"light_restores\":" << stats.light_restores
            << ",\"light_misses\":" << stats.light_misses
            << ",\"light_record_restores\":" << stats.light_record_restores
            << ",\"probe\":" << (probe_enabled() ? "true" : "false")
            << ",\"probe_lines\":" << probe_lines_
            << ",\"probe_dropped\":" << probe_dropped_
            << ",\"fallbacks\":" << stats.fallbacks
            << ",\"reason\":\"" << reason_name(model_.reason()) << "\"}";
        return out.str();
    }

private:
    mutable std::mutex mutex_;
    Model model_;
    bool enabled_{};
    bool atlas_only_{};
    bool installed_{};
    bool install_failed_{};
    std::atomic<bool> active_{false};
    // v1011 probe state.
    std::string probe_path_;
    std::ofstream probe_out_;
    std::atomic<bool> probe_enabled_{false};
    std::chrono::steady_clock::time_point probe_epoch_{};
    std::uint64_t probe_second_{~std::uint64_t{0}};
    std::uint64_t probe_frame_{~std::uint64_t{0}};
    unsigned probe_jobs_[2]{};
    std::uint64_t probe_lines_{};
    std::uint64_t probe_bytes_{};
    std::uint64_t probe_dropped_{};
};

} // namespace shadow_share
} // namespace d2vr
