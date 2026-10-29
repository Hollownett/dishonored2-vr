#pragma once
// G2 VR cutscenes: which Cinematic32 frames render as an immersive (stereo projection) scene. Only the exact game_logic_mask()==32
// (the Cinematic state) is eligible; mixed masks (2|32, 4|32, ...), loading, pause and menus never are. A cutscene frame is immersive
// only with a complete native eye pair whose two eyes are distinct, a readable player camera and no fault. Once the pair has formed, up to
// two consecutive incomplete-pair frames are held (the previous immersive layer of this cutscene is shown again); the third, any other
// failure, or a pair that never forms within the entry grace sends the rest of that cutscene to the theater screen. The fallback is
// sticky until end(), so the view never flickers between modes. Pure state; the probe calls immersive() once per Present and end()
// when the cinematic bit leaves the mask.
#include "cutscene_mode.hpp"
#include <cstdint>

namespace d2vr {
enum class CutsceneFallback {none,mode_theater,knob_off,incomplete_pair,duplicate_eyes,camera_unreadable,fault,bink_video};

inline const char* to_string(CutsceneFallback reason) noexcept {
    switch(reason) {
    case CutsceneFallback::none: return "none";
    case CutsceneFallback::mode_theater: return "mode_theater";
    case CutsceneFallback::knob_off: return "knob_off";
    case CutsceneFallback::incomplete_pair: return "incomplete_pair";
    case CutsceneFallback::duplicate_eyes: return "duplicate_eyes";
    case CutsceneFallback::camera_unreadable: return "camera_unreadable";
    case CutsceneFallback::fault: return "fault";
    case CutsceneFallback::bink_video: return "bink_video";
    }
    return "none";
}

inline constexpr unsigned cinematic_logic_mask=32;

// The cutscene avatar (v676). A cutscene locks the controls, so its own animation owns the player's arms and the mod must not upload
// tracked hands there. The game also draws the player's body and arms; the mod leaves that render copy alone, so the player sees the
// cutscene's arms and body. Only build/hide-cutscene-avatar moves it out of view again (v672's hiding); the tracked knob
// (build/cutscene-avatar, native_animation=false) is the v643 mode, where the player moves the hands and the body follows them.
struct CutsceneAvatarPolicy {bool native_animation;bool hidden;};
inline CutsceneAvatarPolicy cutscene_avatar_policy(bool cutscene,bool native_animation,bool hide_knob) noexcept {
    if(!cutscene || !native_animation) return {false,false};
    return {true,hide_knob};
}

struct CutsceneFrame {
    unsigned logic;        // game_logic_mask() at this Present
    bool paired_complete;  // a complete native eye pair (both eyes' Begins matched, the left image copied, both layer views recorded)
    bool eyes_distinct;    // two different eye images rendered from two different eye poses (never one texture or one pose twice)
    bool camera_readable;  // the player camera the cinematic renders from was read this frame
    bool fault;            // a recoverable fault or callback failure since the cutscene began
    std::uint64_t now_ms{};// Present time; 0 = unknown (a mixed mask is then never held)
};

class CutscenePolicy {
    bool active_{},paired_seen_{},mixed_{},resume_{};
    std::uint64_t mixed_start_{};
    unsigned pending_frames_{},misses_{};
    CutsceneFallback reason_{CutsceneFallback::none},last_fallback_{CutsceneFallback::none};
    std::uint64_t cutscenes_{};
    bool latch(CutsceneFallback why) noexcept {reason_=why;last_fallback_=why;return false;}
    void begin() noexcept {if(!active_) {active_=true;++cutscenes_;}}
public:
    // Entering a cutscene (from gameplay or from a load) the engine needs a few frames to prepare and render the first native pair
    // (G2 spike: the entry frame always misses; after a load, stereo is re-enabled first). Before the first complete pair, an
    // incomplete frame is pending (not immersive, no fallback) for at most this many exact-32 frames.
    static constexpr unsigned entry_grace_frames=30;
    // Once paired, a short gap in the pair (Ruling 9) is held rather than ending the cutscene's VR view: at most this many in a row.
    static constexpr unsigned tolerated_misses=2;
    // Track T C4: inside an ACTIVE immersive cutscene a transient mixed mask (cinematic bit plus another, e.g. a dialog choice 32|x or the
    // skip transition) holds the immersive state (the previous layer is shown again) for at most this long instead of dropping to the
    // theater and back; the pause bit (4) never holds. After the hold the pair gets the entry grace to form again.
    static constexpr std::uint64_t mixed_hold_ms=500;
    static constexpr unsigned pause_logic_bit=4;

    // Sticky: false for the rest of the cutscene once any frame fails.
    bool immersive(const CutsceneFrame& frame,CutsceneMode mode,bool knob_off) noexcept {
        if(frame.logic!=cinematic_logic_mask) {
            // Never immersive. While the cinematic bit stays set the cutscene goes on and its fallback sticks; the scene gates were shut
            // for this frame, though, so the pair has to form again: the entry grace re-arms.
            const bool mixed=(frame.logic&cinematic_logic_mask)!=0 && (frame.logic&pause_logic_bit)==0;
            if(mixed && active_ && reason_==CutsceneFallback::none && paired_seen_ && frame.now_ms) {
                if(!mixed_) {mixed_=true;mixed_start_=frame.now_ms;}
                if(frame.now_ms>=mixed_start_ && frame.now_ms-mixed_start_<=mixed_hold_ms) return false; // held (holding() is true)
            }
            if(active_) {paired_seen_=false;pending_frames_=0;misses_=0;resume_=false;}
            mixed_=false;
            return false;
        }
        if(mixed_) {mixed_=false;resume_=true;misses_=0;} // back to the exact cinematic after a held mixed run
        begin();
        if(reason_!=CutsceneFallback::none) return false;
        if(knob_off) return latch(CutsceneFallback::knob_off);
        if(mode==CutsceneMode::theater) return latch(CutsceneFallback::mode_theater);
        if(frame.fault) return latch(CutsceneFallback::fault);
        if(!frame.camera_readable) return latch(CutsceneFallback::camera_unreadable);
        if(!frame.paired_complete) {
            if(!paired_seen_) {if(++pending_frames_<=entry_grace_frames) return false;}
            else if(++misses_<=(resume_?entry_grace_frames:tolerated_misses)) return false;
            misses_=0;return latch(CutsceneFallback::incomplete_pair);
        }
        if(!frame.eyes_distinct) return latch(CutsceneFallback::duplicate_eyes);
        paired_seen_=true;misses_=0;resume_=false;
        return true;
    }
    // This cutscene can never be immersive (e.g. a one-view eye mode): the theater screen at once, no grace.
    void reject(CutsceneFallback why) noexcept {begin();if(reason_==CutsceneFallback::none && why!=CutsceneFallback::none) latch(why);}
    CutsceneFallback reason() const noexcept {return reason_;}
    CutsceneFallback last_fallback() const noexcept {return last_fallback_;} // kept across end(), for the status
    bool active() const noexcept {return active_;}
    // Waiting for the first pair: nothing new is shown (no theater flash, no unpaired image as stereo).
    bool pending() const noexcept {return active_ && reason_==CutsceneFallback::none && !paired_seen_;}
    // A tolerated miss after the pair formed: show the previous immersive stereo layer of this cutscene again (never a newer image).
    bool holding() const noexcept {return active_ && reason_==CutsceneFallback::none && paired_seen_ && (misses_>0 || mixed_);}
    std::uint64_t cutscenes() const noexcept {return cutscenes_;}
    // When the cinematic bit leaves the mask: the next cutscene starts afresh.
    void end() noexcept {active_=false;paired_seen_=false;mixed_=resume_=false;pending_frames_=0;misses_=0;reason_=CutsceneFallback::none;}
};

// May exact-32 frames keep the scene gates open (native stereo, eye matching, the head-view write)? Never in theater, never with the
// knob, never after this cutscene fell back, and only on the paired native-eye path (a one-view eye mode never makes a pair).
inline bool cutscene_gates_open(CutsceneMode mode,bool knob_off,CutsceneFallback reason,bool paired_native_eyes) noexcept {
    return mode!=CutsceneMode::theater && !knob_off && reason==CutsceneFallback::none && paired_native_eyes;
}
} // namespace d2vr
