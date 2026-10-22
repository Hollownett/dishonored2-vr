#pragma once
// Which overlay quads (HUD window, wrists, controls card, settings panel, world UI, wheel dot, comfort vignette) have to acquire, draw and
// release a swapchain image this frame. An overlay whose content did not change is submitted again with its last released image, which
// OpenXR allows and which saves the xrAcquire/Wait/Release round trip (and the GPU copy) on a runtime such as wineopenxr where each one
// is expensive. Pure and header-only so it runs on Linux.
//
// An overlay renders when ANY of these is true: it never rendered, or the last attempt did not finish (invalidate); the swapchain is a new
// one (chain generation differs, or is unknown = 0); the pose epoch changed (level load, session restart); its content key changed; or the
// image is older than max_hold_ms (a safety refresh for keys that are coarse clocks, 0 = never refresh).
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>
namespace d2vr {
enum class OverlayKind : unsigned {world_ui,hud_window,wrist_left,wrist_right,hint,settings,dot,vignette};
inline constexpr unsigned overlay_kind_count=8;
inline constexpr const char* overlay_kind_name(OverlayKind kind) noexcept {
    constexpr const char* names[overlay_kind_count]{"world_ui","hud_window","wrist_left","wrist_right","hint","settings","dot","vignette"};
    return names[static_cast<unsigned>(kind)%overlay_kind_count];
}
// A 64-bit mix of the facts that decide an overlay's pixels (order matters; splitmix64 finaliser per value).
inline std::uint64_t overlay_content(std::initializer_list<std::uint64_t> values) noexcept {
    std::uint64_t h=0x9e3779b97f4a7c15ull;
    for(auto v:values) {
        h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
        h^=h>>30;h*=0xbf58476d1ce4e5b9ull;h^=h>>27;h*=0x94d049bb133111ebull;h^=h>>31;
    }
    return h;
}
struct OverlayKey {std::uint64_t chain{},epoch{},content{};};
inline constexpr std::uint64_t overlay_refresh_ms=500; // for keys that are engine draw ticks (a draw inside the same millisecond is invisible to them)
class OverlayDirty {
public:
    bool must_render(const OverlayKey& key,std::uint64_t now_ms,std::uint64_t max_hold_ms) const noexcept {
        if(!valid_ || key.chain==0) return true;
        if(key.chain!=key_.chain || key.epoch!=key_.epoch || key.content!=key_.content) return true;
        if(max_hold_ms && now_ms-rendered_ms_>=max_hold_ms) return true;
        return false;
    }
    // After a successful xrReleaseSwapchainImage of a fully drawn image.
    void rendered(const OverlayKey& key,std::uint64_t now_ms) noexcept {key_=key;rendered_ms_=now_ms;valid_=true;}
    // A failed, timed-out or half-drawn render, a session restart, an epoch change the caller wants to be sure of: the next frame renders.
    void invalidate() noexcept {valid_=false;}
    bool valid() const noexcept {return valid_;}
private:
    OverlayKey key_{};std::uint64_t rendered_ms_{};bool valid_{};
};
// What each kind feeds into its key. The functions are the whole dirty rule of that kind.
// Engine-drawn art (world UI, HUD window, wrist): the texture identity, the capture's draw tick, and the geometry that picks the copied box.
inline std::uint64_t world_ui_overlay_content(std::uint64_t texture,std::uint64_t draw_tick,unsigned kind,bool full_height,unsigned width,unsigned rows) noexcept {
    return overlay_content({texture,draw_tick,kind,full_height?1u:0u,width,rows});
}
inline std::uint64_t hud_window_overlay_content(std::uint64_t texture,std::uint64_t draw_tick,int x,int y,int w,int h) noexcept {
    return overlay_content({texture,draw_tick,static_cast<std::uint64_t>(x),static_cast<std::uint64_t>(y),static_cast<std::uint64_t>(w),static_cast<std::uint64_t>(h)});
}
struct WristArtFacts {std::uint64_t texture{},tick{},owner{};unsigned left{},top{},right{},bottom{};bool premultiplied{};};
inline std::uint64_t wrist_overlay_content(const WristArtFacts& primary,const WristArtFacts* mana,unsigned canvas_width,unsigned canvas_height) noexcept {
    const auto one=[](const WristArtFacts& a) {return overlay_content({a.texture,a.tick,a.owner,a.left,a.top,a.right,a.bottom,a.premultiplied?1u:0u});};
    return overlay_content({one(primary),mana?one(*mana):0,mana?1u:0u,canvas_width,canvas_height});
}
// Controls card and wheel dot: the pixels never change, one fill per runtime image. Constant key.
inline constexpr std::uint64_t static_overlay_content() noexcept {return 1;}
// Settings panel: a counter that moves only when the generated pixels differ from the previous ones.
inline std::uint64_t settings_overlay_content(std::uint64_t pixel_generation) noexcept {return overlay_content({pixel_generation});}
// Comfort vignette: with colour-scale the layer carries the opacity and the texture never changes; without it the texture is re-baked
// (at most 10 times a second) and the bake count is the key.
inline std::uint64_t vignette_overlay_content(bool color_scale,std::uint64_t bakes) noexcept {return color_scale?static_overlay_content():overlay_content({bakes});}
// Per-frame counters for the status: releases per submitted overlay frame (p50 over the last 900 frames), skips, per kind.
class OverlayStats {
public:
    static constexpr std::size_t ring=900;
    void note(OverlayKind kind,bool released) noexcept {
        const auto i=static_cast<unsigned>(kind)%overlay_kind_count;
        if(released) {++released_[i];++frame_releases_;} else {++skipped_[i];++frame_skips_;}
    }
    // One call per frame that considered the overlays (a world frame), after the last overlay was handled.
    void end_frame() noexcept {
        counts_[next_%ring]=static_cast<std::uint8_t>(std::min<unsigned>(frame_releases_,255));
        skip_counts_[next_%ring]=static_cast<std::uint8_t>(std::min<unsigned>(frame_skips_,255));
        ++next_;frame_releases_=frame_skips_=0;
    }
    std::uint64_t skipped() const noexcept {std::uint64_t n=0;for(auto v:skipped_) n+=v;return n;}
    std::uint64_t released() const noexcept {std::uint64_t n=0;for(auto v:released_) n+=v;return n;}
    std::uint64_t skipped(OverlayKind kind) const noexcept {return skipped_[static_cast<unsigned>(kind)%overlay_kind_count];}
    std::uint64_t released(OverlayKind kind) const noexcept {return released_[static_cast<unsigned>(kind)%overlay_kind_count];}
    std::uint64_t frames() const noexcept {return next_;}
    double releases_per_frame_p50() const noexcept {return p50(counts_);}
    double skips_per_frame_p50() const noexcept {return p50(skip_counts_);}
    std::string by_kind_json() const {
        std::string out="{";
        for(unsigned i=0;i<overlay_kind_count;++i) {
            if(i) out+=',';
            out+='"';out+=overlay_kind_name(static_cast<OverlayKind>(i));out+="\":[";
            out+=std::to_string(released_[i]);out+=',';out+=std::to_string(skipped_[i]);out+=']';
        }
        return out+'}';
    }
private:
    double p50(const std::array<std::uint8_t,ring>& values) const {
        const std::size_t n=static_cast<std::size_t>(std::min<std::uint64_t>(next_,ring));
        if(!n) return 0;
        std::vector<std::uint8_t> copy(values.begin(),values.begin()+static_cast<std::ptrdiff_t>(n));
        const auto mid=copy.begin()+static_cast<std::ptrdiff_t>(n/2);
        std::nth_element(copy.begin(),mid,copy.end());
        return *mid;
    }
    std::array<std::uint64_t,overlay_kind_count> released_{},skipped_{};
    std::array<std::uint8_t,ring> counts_{},skip_counts_{};
    unsigned frame_releases_{},frame_skips_{};std::uint64_t next_{};
};
} // namespace d2vr
