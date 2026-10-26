#pragma once
// Alternate-eye rendering (plan F task 1): the engine renders ONE eye per displayed frame (knob `alternate-eye`), the other eye is
// resubmitted from the image and pose it was rendered with. Pure logic, no engine or OpenXR types, so Linux runs its tests.
//
// Frame numbers are displayed frames (the Present counter). An image rendered for frame f is submitted at Present f.
//   AlternateEye   plans which eye the engine renders for a frame and tracks, per eye, the last frame an image was kept.
//   EyeRecord      one eye's kept image identity: the exact view (pose + FOV) it was rendered with, its frame and pose epoch.
//   build_layer_views  the two projection views of a displayed frame from the two records: each view is its own record's view
//                      (never the other eye's, never a newer pose than the image), and the frame is only submittable when both
//                      records are valid, from the same pose epoch, and no older than max_age frames.
// Eye identity is never taken from the engine's raw begin index (it is 2 for both views in XR): the view is named by address
// (EyeStamps in frame_split.hpp) and the logical eye is the alternation parity of the frame.
//
// The engine pipeline is ONE frame deep (framerate findings §1; NativeEyeSources matches a Begin only against the Setup published one
// Present earlier): a view Setup prepared while the Present counter reads F is begun in the next interval and its image is on screen at
// Present F+2, not F+1. Everything here is therefore indexed by the DISPLAY frame of an image (alternate_display_frame), the Setup's views
// wait in PendingViews until that Present promotes them, and the planner knows that the Setup it asked for last is still in flight.
// (2026-10-22 headset session: task 1 stamped the Setup for Present F+1, so with the eye flipping every frame each swapchain received the
// OTHER eye's image, a pseudo-stereo pair the eyes cannot fuse: "double vision". The sim's left/right disparity had the reversed sign.)
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2vr {

inline constexpr std::uint64_t alternate_max_age = 2; // an eye image older than this many displayed frames is never submitted
inline constexpr std::uint64_t alternate_pipeline_depth = 1; // Presents between a view Setup's interval and the one that shows its image

// The displayed frame (Present number) that shows the image of a view Setup made while the Present counter read `presented`.
inline std::uint64_t alternate_display_frame(std::uint64_t presented) noexcept {return presented + 1 + alternate_pipeline_depth;}
// The displayed frame of a Begin made while the counter read `presented`: the Present that ends its interval.
inline std::uint64_t alternate_begin_display_frame(std::uint64_t presented) noexcept {return presented + 1;}

// The views the engine was given for display frames not yet presented. One slot per display-frame parity is enough: the Setup for frame D
// is made in the interval that ends at Present D-1 and read at Present D, while the Setup for D+1 (made after Present D-1) lands in the slot
// Present D-1 has already consumed. A slot recorded for a frame that is never promoted is simply overwritten two Setups later.
template <class View>
class PendingViews {
public:
    struct Slot {
        std::uint64_t display{};             // 0 = empty
        unsigned mask{};                     // bit per eye recorded (1 or 2: a one-view frame; 3: a paired frame)
        std::uint64_t epoch{};               // pose epoch the views were located in
        std::array<View, 2> view{};
        std::array<std::int64_t, 2> display_time{}, locate_cpu_time{};
    };
    void record(std::uint64_t display, int eye, const View& view, std::uint64_t epoch, std::int64_t display_time, std::int64_t locate_cpu) noexcept {
        if ((eye != 0 && eye != 1) || display == 0) return;
        Slot& slot = slots_[display & 1u];
        if (slot.display != display) slot = Slot{};
        const auto e = static_cast<std::size_t>(eye);
        slot.display = display; slot.epoch = epoch; slot.mask |= 1u << e;
        slot.view[e] = view; slot.display_time[e] = display_time; slot.locate_cpu_time[e] = locate_cpu;
    }
    // The Setup views recorded for display frame `display`, or nothing when no Setup was recorded for it (loading, a menu, a dropped Setup).
    std::optional<Slot> take(std::uint64_t display) const noexcept {
        const Slot& slot = slots_[display & 1u];
        if (slot.display != display || slot.mask == 0) return std::nullopt;
        return slot;
    }
    void reset() noexcept {slots_ = {};}
private:
    std::array<Slot, 2> slots_{};
};

// The one eye a Setup mask rendered: 0 or 1 for a one-view frame, -1 for a paired frame (3) or nothing (0).
inline int alternate_mask_fresh_eye(unsigned mask) noexcept {return mask == 1u ? 0 : mask == 2u ? 1 : -1;}

// Layer-pose sync for the paired and perf-mono paths (knob off; 2026-10-22). The same one-frame pipeline: the legacy glue wrote a Setup's
// views into GameXr at Setup time, so Present P submitted the views of the Setup made in the interval just before it (whose image shows at
// P+1) with the image of the Setup before that (shown at P). On every head move the layer pose ran one frame ahead of its image, and the
// compositor reprojected from the wrong pose: the world lagged the head by a frame on every turn and step (headset: "unstable image").
// With sync the Setup's views wait in PendingViews for display frame F+2 and are promoted at that Present, as alternate mode does.
// A slot is promotable for a Present when it was recorded for it (have_slot), holds both eyes, and its pose epoch is still current.
inline bool paired_views_promotable(bool have_slot, unsigned mask, std::uint64_t slot_epoch, std::uint64_t epoch) noexcept {
    return have_slot && mask == 3u && slot_epoch == epoch;
}
// Which slot a paired Present promotes: its own (the Setup whose image it shows), else the latest Setup's (the one recorded for the next
// Present: exactly what the legacy glue submitted, so a Present whose own Setup bailed or was located in an old epoch is submitted as
// before instead of being dropped; the glue counts these), else nothing (the layers go out untracked, as they did before).
enum class PairedSlot {none, own, latest};
inline PairedSlot paired_slot_to_promote(bool have_own, unsigned own_mask, std::uint64_t own_epoch, bool have_latest, unsigned latest_mask,
                                         std::uint64_t latest_epoch, std::uint64_t epoch) noexcept {
    if (paired_views_promotable(have_own, own_mask, own_epoch, epoch)) return PairedSlot::own;
    if (paired_views_promotable(have_latest, latest_mask, latest_epoch, epoch)) return PairedSlot::latest;
    return PairedSlot::none;
}

// Present-side readiness of a display frame, from the Setup slot recorded for it and what the glue observed in the interval before this
// Present. A one-view frame is ready when its view's Begin was matched for this Present (mono_begin_matched) and the frame is free
// gameplay (not authored, not the settings panel); a paired frame when both Begins completed and the left image was copied out
// (pair_complete). The slot's pose epoch must still be current (a recentre since the Setup drops the frame). `single` is reported
// whether or not the frame is ready (the mono miss counter); `fresh_eye` is the one eye to refresh, -1 when paired or not ready.
struct AlternateReadiness {bool ready{}; bool single{}; int fresh_eye{-1};};
inline AlternateReadiness alternate_present_readiness(bool have_slot, unsigned mask, std::uint64_t slot_epoch, std::uint64_t epoch, bool stereo_active,
                                                      bool mono_begin_matched, bool pair_complete, bool nonfree) noexcept {
    AlternateReadiness out;
    if (!have_slot || mask == 0 || mask > 3u) return out;
    out.single = mask != 3u;
    if (slot_epoch != epoch || !stereo_active) return out;
    out.ready = out.single ? (mono_begin_matched && !nonfree) : pair_complete;
    if (out.ready) out.fresh_eye = alternate_mask_fresh_eye(mask);
    return out;
}

// The consecutive-miss counter behind the perf-mono-view fallback. Only a frame PLANNED as one view (alternated) and not ready counts as a miss;
// a ready frame, or a paired frame (menu, cutscene, doffed headset), resets it, so a long non-alternating stretch never disables the mode.
inline unsigned alternate_miss_count(unsigned misses, bool planned_single, bool ready) noexcept {return ready || !planned_single ? 0u : misses + 1u;}
// An intentional runtime retirement/backoff has no eye observation to judge.
inline bool alternate_miss_observation_allowed(bool session_ready,bool retiring,bool retry_pending) noexcept {
    return session_ready && !retiring && !retry_pending;
}

// True when an image kept at `rendered_frame` may still be submitted at displayed frame `now`.
inline bool alternate_image_fresh(std::uint64_t rendered_frame, std::uint64_t now) noexcept {
    return rendered_frame != 0 && rendered_frame <= now && now - rendered_frame <= alternate_max_age;
}

// Gating the glue uses, kept pure so the tests exercise the real decisions. Authored and settings state is read AT the Present that submits a
// frame; the plan (made one Present earlier) is never trusted for it.
inline bool alternate_free_gameplay(bool gameplay, bool settings_open) noexcept {return gameplay && !settings_open;}
// A one-view frame may be presented as an alternated eye only in free gameplay. Otherwise the frame is dropped (never one fresh eye + one stale).
inline bool alternate_single_view_allowed(bool authored, bool settings_open) noexcept {return !authored && !settings_open;}
// An eye image may be kept for later frames only if it was made in free gameplay (never a cut, takedown, menu or settings image).
inline bool alternate_may_keep(bool gameplay, bool authored, bool settings_open) noexcept {return gameplay && !authored && !settings_open;}
// Eyes whose view must have been recorded for this Present (bit per eye).
inline unsigned alternate_need_mask(int fresh_eye) noexcept {return fresh_eye == 0 ? 1u : fresh_eye == 1 ? 2u : 3u;}
inline bool alternate_pair_ready(unsigned rendered_mask, int fresh_eye, bool layers_ok) noexcept {
    const unsigned need = alternate_need_mask(fresh_eye);
    return (rendered_mask & need) == need && layers_ok;
}
// Consecutive plan/observation disagreements; true when the session must fall back to paired.
class AlternateMismatch {
public:
    static constexpr unsigned limit = 30;
    bool note(bool mismatch) noexcept {
        if (!mismatch) {run_ = 0; return false;}
        return ++run_ >= limit;
    }
    void reset() noexcept {run_ = 0;}
private:
    unsigned run_{};
};

// One-view rendering needs qualified per-eye native history, previous matrices/position and jitter before Game TAA is allowed.
// Upscalers additionally honor the performance-mode AA feature switch. Native ownership is required in every temporal mode.
inline int alternate_effective_temporal(bool alternate_on, int requested,bool native_taa=false,bool upscalers=false) noexcept {
    return alternate_on ? (native_taa && (requested==1 || upscalers)?requested:0) : requested;
}
// Spatial AA follows the effective mode. A saved FSR choice may temporarily fall back to spatial AA.
inline bool alternate_smaa_applies(bool /*alternate_on*/, int effective_temporal) noexcept {return effective_temporal < 2;}
// Per-eye state (fsr_vfov, jitter counter, eye_jitter) is indexed by the LOGICAL eye of the view being prepared, not the native slot (always 0
// for the one view).
inline std::size_t alternate_state_slot(int logical_eye, int native_slot) noexcept {return static_cast<std::size_t>(logical_eye >= 0 ? logical_eye : native_slot);}

class AlternateEye {
public:
    // The eye (0 left, 1 right) the engine renders for display frame `frame`, or -1 for a paired frame (both eyes rendered, both fresh).
    // Alternates by parity (even frames left) only in free gameplay. Menus, loading, the settings panel and authored scenes
    // (gameplay false or authored true) are -1. After reset() or an epoch change, and whenever the eye that is NOT rendered
    // this frame could not be submitted (it would be stale), the frame is -1 too: one paired frame refreshes both eyes, then
    // alternation resumes. Idempotent per frame (a second call for the same frame returns the first answer).
    // The plan is made one engine frame ahead (at Present frame-2, see alternate_display_frame), so the Setup planned last (for frame-1) is
    // still in flight when this one is planned: it counts as refreshing its eye(s) at frame-1, provided the Present just made delivered an
    // image (rendered() since the last plan). A Present that delivered nothing means the pipeline stalled, and only kept images count.
    int eye_for_frame(std::uint64_t frame, bool gameplay, bool authored, bool epoch_changed) noexcept {
        if (have_ && frame == last_frame_) return last_result_;
        if (epoch_changed) {clear_images(); have_ = false;}  // the in-flight Setup was located in the old epoch: its Present drops it
        int result = -1;
        if (gameplay && !authored) {
            const int eye = static_cast<int>(frame & 1u), other = 1 - eye;
            const bool in_flight = have_ && last_frame_ + 1 == frame && (last_result_ < 0 || last_result_ == other) && delivered_;
            // The first free frame after a menu, cut, takedown or settings panel is paired: whatever images exist were made before or during it.
            if (!refresh_ && (can_submit(other, frame) || in_flight)) result = eye;
            refresh_ = false;
        } else {
            refresh_ = true;
            clear_images();
        }
        have_ = true;
        last_frame_ = frame;
        last_result_ = result;
        delivered_ = false;
        return result;
    }
    // False when that eye's image is more than 2 frames old (or was never kept, or the index is not an eye).
    bool can_submit(int eye, std::uint64_t now_frame) const noexcept {
        return (eye == 0 || eye == 1) && alternate_image_fresh(frame_[eye], now_frame);
    }
    // An image for `eye` was kept at displayed frame `frame` (frame 0 is "never": frames start at 1).
    void rendered(int eye, std::uint64_t frame) noexcept {
        if (eye == 0 || eye == 1) {frame_[eye] = frame; delivered_ = true;}
    }
    void reset() noexcept {
        clear_images();
        have_ = false;
        refresh_ = true;
        delivered_ = false;
        last_frame_ = 0;
        last_result_ = -1;
    }
    std::uint64_t kept_frame(int eye) const noexcept {return (eye == 0 || eye == 1) ? frame_[eye] : 0;}
    static int parity_eye(std::uint64_t frame) noexcept {return static_cast<int>(frame & 1u);}

private:
    void clear_images() noexcept {frame_ = {0, 0};}
    std::array<std::uint64_t, 2> frame_{0, 0};
    std::uint64_t last_frame_{};
    int last_result_{-1};
    bool have_{};
    bool refresh_{true};
    bool delivered_{};
};

template <class View>
struct EyeRecord {
    View view{};
    std::uint64_t frame{};  // displayed frame whose Present first submitted this image; 0 = no image
    std::uint64_t epoch{};  // pose epoch the view was rendered in
    bool valid{};
    std::int64_t display_time{};  // the runtime display time and CPU stamp of the locate this view came from (frame timing only)
    std::int64_t locate_cpu_time{};
};

template <class View>
struct LayerViews {
    std::array<View, 2> views{};
    bool ok{};  // both eyes may be submitted at `now` with these views
};

template <class View>
LayerViews<View> build_layer_views(const std::array<EyeRecord<View>, 2>& records, std::uint64_t now, std::uint64_t epoch) {
    LayerViews<View> out;
    out.views = {records[0].view, records[1].view};
    out.ok = true;
    for (const auto& record : records)
        if (!record.valid || record.epoch != epoch || !alternate_image_fresh(record.frame, now)) out.ok = false;
    return out;
}

}  // namespace d2vr
