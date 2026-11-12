#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace d2vr {
// v1213: what the image skin does with a rig upload (dxgi_probe image_skin_commit). kind = the tick upload's image kind: 0 the engine's or an
// intended palette (wheel / cutscene avatar, contact receipt, VR arms not active), 1 tracked, 2 contact raw, 3 (v1213) VR arms in use but the
// tick fell back to the engine pose (track aged after a hitch, solve or publish refused). fresh: this Present's image exists; held: an earlier
// image of the same fit exists. The engine's arm pose is never kept for kind 3 while any image exists (the v1212 1-3 frame arm vanish).
enum class ImageSkinPlan : unsigned {keep_upload,write_fresh,write_held,gap_unheld};
inline ImageSkinPlan image_skin_plan(int kind,bool fresh,bool held) noexcept {
    if(kind<=0 || kind>3) return ImageSkinPlan::keep_upload;               // intended (or unknown): the upload stays
    if(fresh) return ImageSkinPlan::write_fresh;
    if(kind==3) return held?ImageSkinPlan::write_held:ImageSkinPlan::gap_unheld;
    return ImageSkinPlan::keep_upload;                                     // kind 1/2 without an image: the tick's own tracked palette (a VR pose)
}
// Counts how often a hand (and the sword in it) stops being the tracked pose and falls back to the native arm, per side.
// Pure counting, no behaviour: note() is called once per arm upload with whether the tracked palette was submitted and,
// when it was not, why. gate_flips are tracked<->fallback transitions; fallback_runs_max_frames is the longest run of
// consecutive fallback uploads (a flicker is a run of 1-2).
class ArmFlicker {
public:
    enum Why : unsigned {tracked, no_track, callback_before_track, track_aged, no_solution, stale_revision, publish_refused, other, count};
    static constexpr unsigned reasons=Why::count;
    struct Side {
        std::uint64_t uploads{},fallbacks{},gate_flips{},run{},run_max{};
        std::array<std::uint64_t,reasons> why{};
        bool seen{},was_tracked{};
    };
    void note(unsigned side,bool tracked_palette,unsigned reason) {
        if(side>1) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& s=sides_[side];
        ++s.uploads;
        if(reason>=reasons) reason=other;
        if(tracked_palette) {s.run=0;++s.why[tracked];}
        else {++s.fallbacks;++s.why[reason==tracked?other:reason];if(++s.run>s.run_max)s.run_max=s.run;}
        if(s.seen && s.was_tracked!=tracked_palette)++s.gate_flips;
        s.seen=true;s.was_tracked=tracked_palette;
    }
    void weapon_hidden_frame() {std::lock_guard<std::mutex> lock(mutex_);++weapon_hidden_;}
    void reset() {std::lock_guard<std::mutex> lock(mutex_);sides_={};weapon_hidden_=0;}
    Side of(unsigned i) const {std::lock_guard<std::mutex> lock(mutex_);return sides_[i&1u];}
    std::uint64_t weapon_hidden_frames() const {std::lock_guard<std::mutex> lock(mutex_);return weapon_hidden_;}
    // {"gate_flips":[l,r],"fallback_runs_max_frames":[l,r],"fallbacks":[l,r],"uploads":[l,r],"why":[[...],[...]],"weapon_hidden_frames":n}
    // why order: tracked, no_track, callback_before_track, track_aged, no_solution, stale_revision, publish_refused, other.
    std::string json(std::uint64_t attach_clears) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream out;
        const auto pair=[&](auto get) {out<<'['<<get(sides_[0])<<','<<get(sides_[1])<<']';};
        out<<"{\"gate_flips\":";pair([](const Side& s){return s.gate_flips;});
        out<<",\"fallback_runs_max_frames\":";pair([](const Side& s){return s.run_max;});
        out<<",\"fallbacks\":";pair([](const Side& s){return s.fallbacks;});
        out<<",\"uploads\":";pair([](const Side& s){return s.uploads;});
        out<<",\"why\":[";
        for(unsigned i=0;i<2;++i) {out<<(i?",[":"[");for(unsigned r=0;r<reasons;++r)out<<(r?",":"")<<sides_[i].why[r];out<<']';}
        out<<"],\"attach_clears\":"<<attach_clears<<",\"weapon_hidden_frames\":"<<weapon_hidden_<<'}';
        return out.str();
    }
private:
    mutable std::mutex mutex_;
    std::array<Side,2> sides_{};
    std::uint64_t weapon_hidden_{};
};
} // namespace d2vr
