#pragma once
// Frame-submission hardening for SteamVR on Linux (headset 2026-10-22: SteamVR/vrlink died twice, once at a stealth kill).
//
// Evidence (docs/ and STEALTH-HARDENING-REPORT.md): every runtime-side 16x16 swap-set pair in vrserver/vrlink lined up with a state-5
// (FOCUSED) xrEndFrame that carried ZERO layers, and the authored-scene onset of the kill followed 33 ms later. The runtime treats an empty
// frame as "nothing to show" and rebuilds its direct-mode sets, which the Quest stream does not always survive. So: while the session
// shows frames, never submit zero layers. Header-only and pure (OpenXR types only) so the decisions run on Linux.
#include "xr_quad_validation.hpp"
#include <openxr/openxr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace d2vr {
inline bool xr_session_shows_frames(XrSessionState state) noexcept {return state==XR_SESSION_STATE_VISIBLE || state==XR_SESSION_STATE_FOCUSED;}

// Why a frame had no layer. The first failed gate of GameXr::present wins, in the order the gates are evaluated.
enum class EmptyFrameReason : unsigned {none,not_rendering,continuity_unusable,no_eye_sources,tracked_pair_missing,alternate_eye_rejected,menu_band_not_ready,layers_rejected,count};
inline const char* empty_frame_reason_name(EmptyFrameReason reason) noexcept {
    static constexpr const char* names[]{"none","not_rendering","continuity_unusable","no_eye_sources","tracked_pair_missing","alternate_eye_rejected","menu_band_not_ready","layers_rejected"};
    return static_cast<unsigned>(reason)<static_cast<unsigned>(EmptyFrameReason::count)?names[static_cast<unsigned>(reason)]:"unknown";
}
struct EmptyFrameFacts {
    bool should_render{true},continuity_usable{true},have_sources{true},tracked_ok{true},alternate_ok{true},band_ready{true};
    unsigned layer_count{};  // after the pose guard
};
inline EmptyFrameReason classify_empty_frame(const EmptyFrameFacts& f) noexcept {
    if(f.layer_count) return EmptyFrameReason::none;
    if(!f.should_render) return EmptyFrameReason::not_rendering;
    if(!f.continuity_usable) return EmptyFrameReason::continuity_unusable;
    if(!f.have_sources) return EmptyFrameReason::no_eye_sources;
    if(!f.tracked_ok) return EmptyFrameReason::tracked_pair_missing;
    if(!f.alternate_ok) return EmptyFrameReason::alternate_eye_rejected;
    if(!f.band_ready) return EmptyFrameReason::menu_band_not_ready;
    return EmptyFrameReason::layers_rejected;
}

enum class FrameFallback : unsigned {none,last_projection,black_quad};
inline const char* frame_fallback_name(FrameFallback f) noexcept {return f==FrameFallback::last_projection?"resubmit_last_projection":f==FrameFallback::black_quad?"black_quad":"none";}
struct FallbackFacts {
    unsigned layer_count{};XrSessionState state{XR_SESSION_STATE_UNKNOWN};bool should_render{true};
    EmptyFrameReason reason{};bool last_good_usable{},blank_ready{};
};
// Never an empty frame while the session shows frames. A frame the runtime asked us not to render (shouldRender false) may stay empty.
// After a reference-space change the last projection's poses belong to the old space: black for that frame instead of a misplaced world.
inline FrameFallback choose_frame_fallback(const FallbackFacts& f) noexcept {
    if(f.layer_count || !xr_session_shows_frames(f.state) || !f.should_render) return FrameFallback::none;
    if(f.reason!=EmptyFrameReason::continuity_unusable && f.last_good_usable) return FrameFallback::last_projection;
    return f.blank_ready?FrameFallback::black_quad:FrameFallback::none;
}

// G2 cutscene hold (hold 0 none, 1 black, 2 replay): may an empty frame re-send the last good projection? Not on a black hold; on a replay
// hold only when that projection was this cutscene's own immersive layer (scene ids match, nonzero). Without a hold: as before.
inline bool scene_hold_replays(unsigned hold,std::uint64_t hold_scene,std::uint64_t last_good_scene) noexcept {
    return hold==0 || (hold==2 && hold_scene!=0 && last_good_scene==hold_scene);
}

// The last projection layer that went out with real content: its own poses, fovs and eye chains, copied (the originals are frame locals).
struct LastGoodProjection {
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView,2> views{};
    std::array<XrSwapchain,2> chains{};
    std::uint64_t tick_ms{},epoch{};bool valid{};
    inline static constexpr std::uint64_t max_age_ms=3000;
    static bool well_formed(const XrCompositionLayerProjection& p) noexcept {
        if(p.type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION || p.viewCount!=2 || !p.views || !p.space) return false;
        for(unsigned eye=0;eye<2;++eye) {
            const auto& v=p.views[eye];const auto& q=v.pose.orientation;const auto& t=v.pose.position;
            if(!v.subImage.swapchain || v.subImage.imageRect.extent.width<=0 || v.subImage.imageRect.extent.height<=0) return false;
            if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!std::isfinite(q.w)||!std::isfinite(t.x)||!std::isfinite(t.y)||!std::isfinite(t.z)) return false;
            if(std::fabs(std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w)-1.0)>1e-3) return false;
            if(!(v.fov.angleRight>v.fov.angleLeft) || !(v.fov.angleUp>v.fov.angleDown)) return false;
        }
        return true;
    }
    bool capture(const XrCompositionLayerProjection& p,std::uint64_t now_ms,std::uint64_t pose_epoch) noexcept {
        if(!well_formed(p)) return false;
        views[0]=p.views[0];views[1]=p.views[1];views[0].next=views[1].next=nullptr;
        layer=p;layer.next=nullptr;layer.views=views.data();
        chains={p.views[0].subImage.swapchain,p.views[1].subImage.swapchain};tick_ms=now_ms;epoch=pose_epoch;valid=true;return true;
    }
    // Usable only while the eye chains are the very ones it references (a resize replaces them) and in the same reference epoch.
    bool usable(const std::array<XrSwapchain,2>& current,std::uint64_t now_ms,std::uint64_t pose_epoch) const noexcept {
        return valid && chains==current && chains[0] && chains[1] && epoch==pose_epoch && now_ms>=tick_ms && now_ms-tick_ms<=max_age_ms;
    }
    const XrCompositionLayerBaseHeader* header() noexcept {layer.views=views.data();return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);}
    void reset() noexcept {valid=false;chains={};}
};

// A large black quad, 1 m in front of the head (or of the origin when no head pose is known), showing a whole opaque-black swapchain.
inline XrCompositionLayerQuad make_black_quad(XrSpace space,XrSwapchain chain,std::uint32_t extent,const XrPosef* head) noexcept {
    XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.space=space;q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;q.layerFlags=0;q.size={6.f,6.f};
    q.subImage.swapchain=chain;q.subImage.imageRect={{0,0},{static_cast<int32_t>(extent),static_cast<int32_t>(extent)}};
    q.pose.orientation={0,0,0,1};q.pose.position={0,0,-1.f};
    if(head) {
        const auto& o=head->orientation;const double n=std::sqrt(double(o.x)*o.x+double(o.y)*o.y+double(o.z)*o.z+double(o.w)*o.w);
        const auto& p=head->position;
        if(n>1e-3 && n<1e3 && std::isfinite(n) && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) {
            const float x=static_cast<float>(o.x/n),y=static_cast<float>(o.y/n),z=static_cast<float>(o.z/n),w=static_cast<float>(o.w/n);
            // forward = rotate (0,0,-1) by the unit quaternion
            const float fx=-2.f*(x*z+w*y),fy=-2.f*(y*z-w*x),fz=-(1.f-2.f*(x*x+y*y));
            q.pose.orientation={x,y,z,w};q.pose.position={p.x+fx,p.y+fy,p.z+fz};
        }
    }
    return q;
}
// True when the layer would pass the runtime-style validation (xr_quad_validation.hpp) against a chain of `extent` x `extent`.
inline bool black_quad_valid(const XrCompositionLayerQuad& q,std::uint32_t extent) noexcept {
    return validate_quad_layer(q,true,QuadChainFacts{true,extent,extent},false)==XR_SUCCESS;
}

// Rows of the menu/cutscene band chain: the 16:9 middle band of the eye image, a pure function of the eye size so the chain can be built
// at its final size as soon as the eye chains exist (before any frame), not on first use.
inline std::int32_t menu_band_rows(std::uint32_t width,std::uint32_t height,double band=9.0/16.0) noexcept {
    if(!width || !height) return 1;
    const long rows=std::lround(width*band);
    return static_cast<std::int32_t>(std::clamp(rows,1l,static_cast<long>(height)));
}

// At most `per_window` lines per `window_ms` and `total_cap` lines in all; the first line after a suppressed run says how many were skipped.
class EventLogLimiter {
public:
    struct Decision {bool emit{};unsigned suppressed_before{};bool final_notice{};};
    EventLogLimiter(unsigned total_cap,unsigned per_window,std::uint64_t window_ms) noexcept : total_cap_(total_cap),per_window_(per_window),window_ms_(window_ms) {}
    Decision admit(std::uint64_t now_ms) noexcept {
        ++seen_;
        if(emitted_>=total_cap_) {if(!final_sent_) {final_sent_=true;return {true,0,true};}++dropped_;return {};}
        if(!window_open_ || (now_ms>=window_start_ && now_ms-window_start_>=window_ms_)) {window_start_=now_ms;in_window_=0;window_open_=true;}
        if(in_window_>=per_window_) {++suppressed_;++dropped_;return {};}
        ++in_window_;++emitted_;const auto skipped=suppressed_;suppressed_=0;return {true,skipped,false};
    }
    unsigned emitted() const noexcept {return emitted_;}
    std::uint64_t seen() const noexcept {return seen_;}
    std::uint64_t dropped() const noexcept {return dropped_;}
private:
    unsigned total_cap_,per_window_;std::uint64_t window_ms_;
    unsigned emitted_{},in_window_{},suppressed_{};std::uint64_t window_start_{},seen_{},dropped_{};bool window_open_{},final_sent_{};
};

inline std::string format_clock(unsigned hour,unsigned minute,unsigned second,unsigned millisecond) {
    char text[16];std::snprintf(text,sizeof(text),"%02u:%02u:%02u.%03u",hour%24,minute%60,second%60,millisecond%1000);return text;
}
// "swapchain create" / "swapchain destroy" lines for xr-recoveries.log. `utc` and `local` are format_clock strings.
inline std::string format_chain_event(bool create,const char* name,std::uint32_t width,std::uint32_t height,bool frame_open,std::uint64_t handle,
                                      const std::string& utc,const std::string& local,unsigned suppressed_before) {
    char text[320];
    std::snprintf(text,sizeof(text),"xr-event swapchain %s name=\"%s\" size=%ux%u in_frame=%d handle=0x%llx utc=%s local=%s",
        create?"create":"destroy",name?name:"",width,height,frame_open?1:0,static_cast<unsigned long long>(handle),utc.c_str(),local.c_str());
    std::string out=text;
    if(suppressed_before) out+=" suppressed_before="+std::to_string(suppressed_before);
    return out;
}
inline std::string format_empty_frame_event(std::uint64_t frame,XrSessionState state,EmptyFrameReason reason,FrameFallback fallback,bool should_render,bool gameplay,
                                            bool authored,bool world_scene,bool band_ready,const std::string& utc,const std::string& local,unsigned suppressed_before) {
    char text[360];
    std::snprintf(text,sizeof(text),"xr-event empty-frame frame=%llu session_state=%d reason=%s action=%s should_render=%d gameplay=%d authored=%d world_scene=%d menu_band_ready=%d utc=%s local=%s",
        static_cast<unsigned long long>(frame),static_cast<int>(state),empty_frame_reason_name(reason),frame_fallback_name(fallback),should_render?1:0,gameplay?1:0,
        authored?1:0,world_scene?1:0,band_ready?1:0,utc.c_str(),local.c_str());
    std::string out=text;
    if(suppressed_before) out+=" suppressed_before="+std::to_string(suppressed_before);
    return out;
}
}
