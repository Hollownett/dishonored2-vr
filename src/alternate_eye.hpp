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
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2vr {

inline constexpr std::uint64_t alternate_max_age = 2; // an eye image older than this many displayed frames is never submitted

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
// The one eye rendered for this Present: the recorded view's eye when a one-view frame is ready, else -1 (paired / both eyes fresh).
inline int alternate_fresh_eye(bool on, bool single_view_path, bool ready, std::uint64_t head_stamp) noexcept {
    return on && single_view_path && ready ? static_cast<int>(head_stamp & 1u) : -1;
}
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

// Temporal AA in alternate-eye mode (plan F task 3, path B). The engine keeps ONE view object in one-view mode: its rolled previous matrices
// and jitter belong to the OTHER eye (IPD offset + the pose change), so TAA/FSR motion vectors and history would be wrong and ghost. Alternate
// mode therefore forces the temporal mode to 0 (the SMAA row), whatever was requested, and the SMAA pass runs even when the saved preference
// names an FSR mode (which normally skips SMAA).
inline int alternate_effective_temporal(bool alternate_on, int requested) noexcept {return alternate_on ? 0 : requested;}
inline bool alternate_smaa_applies(bool alternate_on, int saved_temporal) noexcept {return saved_temporal < 2 || alternate_on;}
// Per-eye state (fsr_vfov, jitter counter, eye_jitter) is indexed by the LOGICAL eye of the view being prepared, not the native slot (always 0
// for the one view).
inline std::size_t alternate_state_slot(int logical_eye, int native_slot) noexcept {return static_cast<std::size_t>(logical_eye >= 0 ? logical_eye : native_slot);}

class AlternateEye {
public:
    // The eye (0 left, 1 right) the engine renders for `frame`, or -1 for a paired frame (both eyes rendered, both fresh).
    // Alternates by parity (even frames left) only in free gameplay. Menus, loading, the settings panel and authored scenes
    // (gameplay false or authored true) are -1. After reset() or an epoch change, and whenever the eye that is NOT rendered
    // this frame could not be submitted (it would be stale), the frame is -1 too: one paired frame refreshes both eyes, then
    // alternation resumes. Idempotent per frame (a second call for the same frame returns the first answer).
    int eye_for_frame(std::uint64_t frame, bool gameplay, bool authored, bool epoch_changed) noexcept {
        if (have_ && frame == last_frame_) return last_result_;
        if (epoch_changed) clear_images();
        int result = -1;
        if (gameplay && !authored) {
            const int eye = static_cast<int>(frame & 1u);
            // The first free frame after a menu, cut, takedown or settings panel is paired: whatever images exist were made before or during it.
            if (!refresh_ && can_submit(1 - eye, frame)) result = eye;
            refresh_ = false;
        } else {
            refresh_ = true;
            clear_images();
        }
        have_ = true;
        last_frame_ = frame;
        last_result_ = result;
        return result;
    }
    // False when that eye's image is more than 2 frames old (or was never kept, or the index is not an eye).
    bool can_submit(int eye, std::uint64_t now_frame) const noexcept {
        return (eye == 0 || eye == 1) && alternate_image_fresh(frame_[eye], now_frame);
    }
    // An image for `eye` was kept at displayed frame `frame` (frame 0 is "never": frames start at 1).
    void rendered(int eye, std::uint64_t frame) noexcept {
        if (eye == 0 || eye == 1) frame_[eye] = frame;
    }
    void reset() noexcept {
        clear_images();
        have_ = false;
        refresh_ = true;
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
