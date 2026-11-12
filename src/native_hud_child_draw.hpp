#pragma once
#include "native_hud_art_probe.hpp"
#include "interaction_ray.hpp"
#include "native_crosshair.hpp"
#include "native_prompt_entry.hpp"
#include <atomic>
#include <mutex>

namespace d2vr {
enum class NativeHudChildKind:unsigned {none,prompt,health,mana,equipment,objective,tutorial,panel,crosshair};
struct NativeHudChildScope {
    NativeHudChildKind kind{};std::uintptr_t file{},iggy{},node{},owner{};UINT64 epoch{},tick{};
    std::array<float,6> matrix{};std::array<float,4> bounds{};bool matrix_valid{},bounds_valid{};
    NativeInteractionTargetIdentity target{};
    bool talk_offer{},panel_prompt{},prompt_event_pending{},takedown{};std::uint64_t prompt_ticket{}; // takedown: the choke/kill hint (its own capture slot, v698)
    std::optional<native_prompt_entry::Receipt> retained_entry{};
};
inline bool native_hud_talk_glyph_context(const NativeHudChildScope* scope) noexcept {
    return scope && (scope->kind==NativeHudChildKind::prompt ||
        (scope->kind==NativeHudChildKind::panel && scope->panel_prompt)) && scope->target && scope->talk_offer;
}
inline bool native_hud_child_same_identity(const NativeHudChildScope& a,const NativeHudChildScope& b) noexcept {
    return a.kind!=NativeHudChildKind::none && a.kind==b.kind && a.file==b.file && a.iggy==b.iggy &&
        a.node==b.node && a.owner==b.owner && a.epoch && a.epoch==b.epoch &&
        (a.retained_entry && b.retained_entry?native_prompt_entry::same_source(*a.retained_entry,*b.retained_entry):
            !a.retained_entry && !b.retained_entry) &&
        (a.kind!=NativeHudChildKind::prompt || a.retained_entry || a.target==b.target);
}
// A committed image may authorize flat suppression only for this exact target
// and pose generation. Current native observation and artwork age are bounded
// independently: refreshing a target never renews old pixels.
// The same drawn element (one node of one movie for one owner), whichever binding (retained event entry or hand target)
// published it this frame: its pixels are the same, so a held frame may keep showing them across a binding change.
inline bool native_hud_child_same_element(const NativeHudChildScope& a,const NativeHudChildScope& b) noexcept {
    return a.kind!=NativeHudChildKind::none && a.kind==b.kind && a.file==b.file && a.iggy==b.iggy && a.node==b.node && a.owner==b.owner &&
        a.epoch && a.epoch==b.epoch && a.takedown==b.takedown;
}
inline bool native_hud_child_replacement_ready(const NativeHudChildScope& current,const NativeHudChildScope& committed,
        UINT64 image_tick,UINT64 now,UINT64 max_age=250) noexcept {
    return native_hud_child_same_identity(current,committed) && current.tick && image_tick &&
        now>=current.tick && now-current.tick<=max_age && now>=image_tick && now-image_tick<=max_age;
}
// Last good world prompt: lets one failed capture/readback/placement frame keep showing
// the previous world art (republished with a fresh tick) for a short, bounded grace.
struct NativeHudPromptHold {
    // v692 (headset v691: the text grew huge for 1-2 frames when a prompt changed, e.g. a grabbed bottle's): the hold keeps the art's
    // scale (metres per texel), not its width. The held frame can show newer art of another size (the bounds pass landed meanwhile).
    UINT64 tick{};Vec3 point{};double width{.24},per_texel{};
    void good(UINT64 now,Vec3 p,double w,unsigned texels=0) noexcept {tick=now;point=p;width=w;per_texel=texels?w/texels:0;}
    double width_for(unsigned texels) const noexcept {return per_texel>0 && texels?per_texel*texels:width;}
    bool holds(UINT64 now,UINT64 grace=600) const noexcept {return tick && now>=tick && now-tick<=grace;} // 600: rides out takedown sensor flicker
    void reset() noexcept {tick=0;}
};
// How long past its last good placement a prompt slot keeps its last art on a failed frame (0 = clear it now).
// Same element still live: the v692 one-frame hold (600 ms). v1141/v1146 takedown gap (1300 ms): the mod still offers the art's own NPC and no
// fresh capture of a DIFFERENT element owns the slot. v1220 (user, headset v1216: body prompts "not very stable, same problem as the kill/choke"):
// the same gap for the hand/gaze prompt - the game withdraws its retained HUD entry and rebinds the same window to the hand target every
// ~0.75 s on a body, and the new binding's art lands 15-78 ms later (1670 logged gaps with the target still offered: p50 31, p95 78, 98.9 % <= 250 ms)
// while the hand target (its own 500 ms hold) still names the same object. 250 ms rides those out and lets a prompt linger at most that long.
// v1225 handover (sim v1222: the window rebound from one object's prompt to ANOTHER object's, crop 459 -> 372, and stayed blank 62 ms until the
// new art landed): the same window (node) now bound to another object keeps the old art at most 100 ms (p95 art latency 78 ms), never longer.
inline UINT64 native_prompt_keep_grace(bool live,bool live_same_element,bool live_current,bool held_still_bound,bool offer_same_object,bool takedown_slot,
        bool live_same_window=false) noexcept {
    const bool same_live=live && live_same_element && live_current;
    const UINT64 kept=live && live_same_element && (same_live || held_still_bound)?600:0;
    if(offer_same_object && !same_live && (!live || live_same_element || !live_current)) return takedown_slot?1300:std::max<UINT64>(kept,250);
    if(!takedown_slot && !kept && live && !live_same_element && live_current && live_same_window) return 100;
    return kept;
}
// v1228: how well a body's carry prompt follows the hand. An interval is a run of frames whose prompt target is one body (a gap of up to
// gap_ms without it continues the run); runs shorter than min_ms are not counted. Per counted run: never shown, first shown later than
// late_ms, hidden again while the run lasted (flicker); plus the run time and the shown time. One thread (the HUD publish) updates it.
struct NativeBodyPromptStats {
    static constexpr UINT64 gap_ms=300,min_ms=300,late_ms=300;
    std::uintptr_t body{};UINT64 start{},seen{},last{},run_ms{},run_shown{},first{};bool shown{},any{};unsigned hides{};
    UINT64 intervals{},never{},late{},flicker{},target_ms{},shown_ms{},hides_total{};
    void close() noexcept {
        if(body && seen-start>=min_ms) {++intervals;never+=!any;late+=any && first-start>late_ms;flicker+=hides>0;target_ms+=run_ms;shown_ms+=run_shown;hides_total+=hides;}
        body=0;
    }
    void update(UINT64 now,std::uintptr_t target_body,bool prompt_shown) noexcept {
        if(body && (now<last || (target_body && target_body!=body) || (!target_body && now-seen>gap_ms))) close();
        if(!body && target_body) {body=target_body;start=seen=last=now;run_ms=run_shown=0;any=false;hides=0;shown=prompt_shown;if(prompt_shown){any=true;first=now;}return;}
        if(!body) return;
        const UINT64 dt=now-last;last=now;if(target_body) seen=now;
        run_ms+=dt;if(prompt_shown) run_shown+=dt;
        if(prompt_shown && !any) {any=true;first=now;}
        if(!shown && prompt_shown && any && first!=now) ++hides;   // a hide counts once the prompt comes back in the same run (the last fade is no flicker)
        shown=prompt_shown;
    }
};
// The flat prompt is allowed only when no interaction target exists and no world art is
// committed or held. Other kinds keep the committed-image rule.
inline bool native_hud_keep_flat(NativeHudChildKind kind,bool target_exists,bool replacement_ready,bool held) noexcept {
    // User requirement: object prompts are VR-only; the flat HUD copy is never drawn. Vitals (health, mana,
    // ammo) live on the wrist: a flat copy would be a head-locked widget in the corner of the lens.
    (void)target_exists;(void)held;
    // v710 (headset v699: tutorial messages showed as huge white letters past the view, jumping big/small): a tutorial or information
    // message is VR-only too. Its flat copy filled the eye at full-screen scale whenever its world art was not ready.
    if(kind==NativeHudChildKind::prompt || kind==NativeHudChildKind::tutorial || kind==NativeHudChildKind::health || kind==NativeHudChildKind::mana || kind==NativeHudChildKind::equipment)return false;
    return !replacement_ready;
}
// Horizontal shift that centres a child's bounds on the 1920-wide stage (0 = leave as laid out;
// invalid, sentinel (-5e6..5e6) or stage-wide bounds are never moved). Used for tutorial text, which
// natively sits in a corner of its own movie.
inline float native_hud_centre_shift(const std::array<float,6>& m,const std::array<float,4>& b,float stage_w=1920.f) noexcept {
    for(const auto v:m)if(!std::isfinite(v))return 0.f;
    for(const auto v:b)if(!std::isfinite(v) || std::fabs(v)>1e5f)return 0.f;
    if(b[2]<=b[0] || b[3]<=b[1] || (b[2]-b[0])*std::fabs(m[0])>=stage_w*.95f)return 0.f;
    const float cx=m[0]*(b[0]+b[2])*.5f+m[2]*(b[1]+b[3])*.5f+m[4];
    return stage_w*.5f-cx;
}
// Scales a child matrix about the stage centre (k<1 pulls corner widgets inward). False = unchanged.
inline bool native_hud_panel_relayout(std::array<float,6>& m,const std::array<float,2>& centre,float k) noexcept {
    if(!(k>=.3f && k<.999f) || !(centre[0]>0.f) || !(centre[1]>0.f))return false;
    auto out=m;for(unsigned i=0;i<4;++i)out[i]*=k;
    out[4]=centre[0]+k*(m[4]-centre[0]);out[5]=centre[1]+k*(m[5]-centre[1]);
    for(const auto v:out)if(!std::isfinite(v))return false;
    m=out;return true;
}
// Reticle draw matrix maps its local registration origin to the aim point.
// Shrink only the linear part: translation (the aim pivot), caller bytes and siblings stay intact.
inline bool native_crosshair_glyph_scale(std::array<float,6>& matrix,float hud_scale) noexcept {
    if(!std::isfinite(hud_scale) || hud_scale<.3f || hud_scale>2.f)return false;
    for(float v:matrix)if(!std::isfinite(v))return false;
    const float k=.65f*hud_scale;auto out=matrix;
    for(unsigned i=0;i<4;++i)out[i]*=k;
    for(float v:out)if(!std::isfinite(v))return false;
    matrix=out;return true;
}
// The hidden crosshair: zero linear part, translation kept. The original still draws, invisibly, so the engine's own bookkeeping is untouched.
inline bool native_crosshair_hide(std::array<float,6>& m) noexcept {
    for(unsigned i=0;i<4;++i)m[i]=0.f;
    return true;
}
// Undo the known flat-HUD ancestor scale before sizing the eye-space reticle.
// Its native x/y already name the aim point; inherited panel layout must not move it.
inline bool native_crosshair_unlayout(std::array<float,6>& matrix,float k,const std::array<float,2>& centre) noexcept {
    if(!std::isfinite(k) || k<.3f || k>=.999f)return false;
    auto out=matrix;for(unsigned i=0;i<4;++i)out[i]/=k;
    out[4]=centre[0]+(matrix[4]-centre[0])/k;out[5]=centre[1]+(matrix[5]-centre[1])/k;
    for(float v:out)if(!std::isfinite(v))return false;
    matrix=out;return true;
}
// HUD screen: panel children keep their native stage layout. One transform fits the movie's 1920x1080 stage
// (placed by its root matrix: scale root_s, origin root_tx/ty) to the panel capture texture by width and
// centres it vertically, so nothing overflows the texture whatever eye size the HUD target has.
inline std::array<float,6> native_hud_stage_fit(const std::array<float,6>& m,float root_s,float root_tx,float root_ty,float tex_w,float tex_h) noexcept {
    const float s=tex_w/1920.f,k=s/root_s,y0=(tex_h-1080.f*s)*.5f;
    return {k*m[0],k*m[1],k*m[2],k*m[3],k*(m[4]-root_tx),k*(m[5]-root_ty)+y0};
}
// True when pixel bounds b (x0,y0,x1,y1) cover at least 90% of the render target in both axes. The target is
// reconstructed from the movie root (scale s, origin tx/ty): the stage is width-fitted and centred.
inline bool native_hud_full_screen(const std::array<float,4>& b,float s,float tx,float ty) noexcept {
    const float w=1920.f*s+2.f*tx,h=1080.f*s+2.f*ty;
    return s>0.f && w>0.f && h>0.f && b[2]-b[0]>=.9f*w && b[3]-b[1]>=.9f*h;
}
// Exact native display-node render boundary. Observational unless a producer
// explicitly accepts Begin or Transform. No AS3/node metadata is written.
class NativeHudChildDraw {
public:
    using Read=NativeWristHud::Read;
    using Draw=std::uint32_t (__fastcall*)(void*,float*,void*,void*,void*,void*,std::uint32_t,float);
    using Observer=void (*)(const NativeHudChildScope&,bool before,std::uint32_t result) noexcept;
    using Begin=bool (*)(const NativeHudChildScope&) noexcept;
    using End=void (*)(const NativeHudChildScope&) noexcept;
    using Transform=bool (*)(const NativeHudChildScope&,std::array<float,6>& copied_matrix) noexcept;
    inline static std::atomic<Observer> observer{};
    inline static std::atomic<Begin> begin{};inline static std::atomic<End> end{};
    inline static std::atomic<Transform> transform{};
    using CpuProfile=void (*)(unsigned stage,unsigned nesting) noexcept;
    inline static std::atomic<CpuProfile> cpu_profile{};
    inline static std::atomic<UINT64> calls{},identified{},captures{},transforms{},publish_accepts{},publish_rejects{},suppressed{},master_entries{};
    inline static std::atomic<unsigned> panel_width{},panel_height{}; // HUD window capture texture, set by its producer
    inline static thread_local const NativeHudChildScope* current{};
    // Diagnostic (build/hud-node-census): every drawn display node, with hook nesting depth.
    using Census=void (*)(void* iggy,const float* matrix,void* node,unsigned nesting) noexcept;
    inline static std::atomic<Census> census{};
    inline static thread_local unsigned nesting_{};
    inline static thread_local float eye_layout_scale_=1.f;
    inline static thread_local std::array<float,2> eye_layout_centre_{};
    // Nesting of the innermost container kept native only because it holds a wrist widget. Its other children are
    // that widget's frame (ring backdrop, slot diamonds): drawn flat they were head-locked in the lens corner, on the
    // HUD screen they were orphaned decorations. They are not drawn; the wrist shows the widget itself.
    inline static thread_local unsigned holder_level_{};inline static thread_local bool holder_hit_{},holder_hide_{};
    inline static std::atomic<UINT64> holder_hidden{};
    // ui/hud.iggy file object of the movie being drawn (iggy+0x78), for probing its vitals on the UI thread.
    inline static std::atomic<std::uintptr_t> hud_file{};
    // HUD screen exceptions. eye_node: the crosshair, which the mod places on the left hand's aim in EYE coordinates,
    // so it is drawn natively into the eye (on the panel it was offset from the hand). prompt_world: a hand points at
    // the prompt's object, so the prompt is captured as world art beside that hand instead of joining the panel.
    inline static std::atomic<std::uintptr_t> eye_node{}; // Hint only; never ownership proof.
    // The in-game Crosshair switch: the eye node is still drawn (and placed), with a zero linear part. Set every frame by the render thread.
    inline static std::atomic<bool> crosshair_hidden{},crosshair_composed{};
    // Paired stereo draws the UI once per eye (left first): the callback answers the glyph's extra translation for the n-th eye-node draw
    // of the frame (n counts from 0 after begin_frame), nullopt for none.
    using EyeShift=std::optional<std::array<float,2>> (*)(unsigned eye_draw_index) noexcept;
    inline static std::atomic<EyeShift> eye_shift{};
    inline static std::atomic<UINT64> crosshair_hidden_draws{},crosshair_eye_shifts{},crosshair_unlayouts{},crosshair_captures{};
    // v688: while the mod composes the reticle, the game's own crosshair element (dot, red dot, grenade/mine/power/crossbow
    // states) is captured instead of hidden; its anchor (the glyph translation, target pixels) is where the aim point sits.
    inline static std::atomic<float> crosshair_anchor_x{},crosshair_anchor_y{};
    // Observation only: gate/busy, owner/movie mismatch, no callback, native
    // binding, file/recent-Draw, backlinks, tree, exception.
    inline static std::array<std::atomic<UINT64>,8> eye_rejects{};
    inline static std::atomic<UINT64> setup_eye_checks{},setup_eye_passes{};

    // The movie root (scale, tx, ty) as last seen at the top of the HUD movie, readable from any thread.
    inline static std::atomic<float> root_scale_pub{},root_tx_pub{},root_ty_pub{};
    inline static void publish_root(const std::array<float,6>& m) noexcept {
        if(!(m[0]>0.f) || !std::isfinite(m[0]) || !std::isfinite(m[4]) || !std::isfinite(m[5]))return;
        root_scale_pub.store(m[0],std::memory_order_relaxed);root_tx_pub.store(m[4],std::memory_order_relaxed);root_ty_pub.store(m[5],std::memory_order_relaxed);
    }
    static float root_scale() noexcept {return root_scale_pub.load(std::memory_order_relaxed);}
    // hud.iggy's own root scale only. root_scale_pub is also written by the tutorial and panel movies (other roots), so a
    // size derived from it jumped between frames (v689 headset: HUD art popping in size).
    inline static std::atomic<float> hud_root_scale_pub{};
    static float hud_root_scale() noexcept {return hud_root_scale_pub.load(std::memory_order_relaxed);}
    // The eye target size in pixels from the movie root (it fits the 1920x1080 stage by width and centres it): {0,0} when unknown.
    static std::array<float,2> target_size() noexcept {
        const float s=root_scale_pub.load(std::memory_order_relaxed),tx=root_tx_pub.load(std::memory_order_relaxed),ty=root_ty_pub.load(std::memory_order_relaxed);
        if(!(s>0.f))return {0.f,0.f};
        return {1920.f*s+2.f*tx,1080.f*s+2.f*ty};
    }
    using EyeLive=bool (*)(const NativeCrosshair::Binding&,UINT64) noexcept;
    inline static std::atomic<EyeLive> eye_live{};
private:
    struct EyeBinding {std::uintptr_t node,file;NativeCrosshair::Binding owner;};
    inline static EyeBinding eye_binding_{};
    inline static std::mutex eye_diagnostics_mutex_;
    // Per reason: tick,frame,owner snapshot tick,element,Iggy,file,node.
    inline static std::array<std::array<UINT64,7>,8> eye_last_{};
    static void reject_eye(unsigned reason) noexcept {const EyeBinding empty{};reject_eye(reason,empty);}
    static void reject_eye(unsigned reason,const EyeBinding& binding) noexcept {
        ++eye_rejects[reason];
        if(!eye_diagnostics_mutex_.try_lock())return;
        eye_last_[reason]={GetTickCount64(),frame_.load(),binding.owner.tick,binding.owner.element,binding.owner.iggy,binding.file,binding.node};
        eye_diagnostics_mutex_.unlock();
    }
public:
    static std::optional<std::array<std::array<UINT64,7>,8>> eye_diagnostics() noexcept {
        if(!eye_diagnostics_mutex_.try_lock())return {};
        const auto out=eye_last_;eye_diagnostics_mutex_.unlock();return out;
    }
private:

    // Per displayed frame the exact-eye node is validated against the live engine once (the owner binding, the file/iggy back-links and the
    // tree walk are many engine reads), and every further child draw of that frame is answered from eye_ok_. begin_frame(0) (the default) turns
    // the cache off. Publishing or clearing the binding, or an answer older than eye_ok_max_age_ms, drops it.
    inline static std::atomic<unsigned> eye_draws_{};
    inline static std::atomic<UINT64> frame_{},eye_gen_{},eye_ok_frame_{},eye_ok_gen_{},eye_ok_tick_{},eye_validations_{},eye_cache_hits_{};
    inline static std::atomic<std::uintptr_t> eye_ok_iggy_{},eye_ok_node_{};
    inline static constexpr UINT64 eye_ok_max_age_ms=50;
    static std::uintptr_t live_eye_node(void* iggy) noexcept {
        if(!active_.load() || !read_ || !mutex_.try_lock()) {reject_eye(0);return 0;}
        const auto b=eye_binding_;mutex_.unlock();
        if(!b.node || b.owner.iggy!=reinterpret_cast<std::uintptr_t>(iggy)) {reject_eye(1,b);return 0;}
        const auto frame=frame_.load(std::memory_order_relaxed);const auto tick=GetTickCount64();
        if(frame && eye_ok_frame_.load(std::memory_order_acquire)==frame && eye_ok_gen_.load(std::memory_order_relaxed)==eye_gen_.load(std::memory_order_acquire) &&
           eye_ok_iggy_.load(std::memory_order_relaxed)==b.owner.iggy && tick>=eye_ok_tick_.load(std::memory_order_relaxed) &&
           tick-eye_ok_tick_.load(std::memory_order_relaxed)<=eye_ok_max_age_ms) {
            ++eye_cache_hits_;return eye_ok_node_.load(std::memory_order_relaxed);
        }
        const auto generation=eye_gen_.load(std::memory_order_acquire);
        ++eye_validations_;
        const auto node=validated_eye_node(b,tick);
        if(frame) { // publish the answer, frame last
            eye_ok_node_.store(node,std::memory_order_relaxed);eye_ok_iggy_.store(b.owner.iggy,std::memory_order_relaxed);
            eye_ok_gen_.store(generation,std::memory_order_relaxed);eye_ok_tick_.store(tick,std::memory_order_relaxed);
            eye_ok_frame_.store(frame,std::memory_order_release);
        }
        return node;
    }
    static std::uintptr_t validated_eye_node(const EyeBinding& b,UINT64 tick) noexcept {
        const auto live=eye_live.load();
        if(!live) {reject_eye(2,b);return 0;}
        if(!live(b.owner,tick)) {reject_eye(3,b);return 0;}
        try {
            if(!wrist_hud_file_owned(read_,base_,b.file)) {reject_eye(4,b);return 0;}
            if(rig::value<std::uintptr_t>(read_,b.file+0x1608)!=b.owner.iggy ||
               rig::value<std::uintptr_t>(read_,b.owner.iggy+0x78)!=b.file) {reject_eye(5,b);return 0;}
            if(!NativeHudArtProbe::in_tree(read_,b.owner.iggy,b.node)) {reject_eye(6,b);return 0;}
            return b.node;
        } catch(...) {reject_eye(7,b);return 0;}
    }
public:
    static bool publish_eye(std::uintptr_t node,const NativeCrosshair::Binding& owner) noexcept {
        std::uintptr_t file{};
        if(!node || !read_ || !owner.iggy || !read_(owner.iggy+0x78,&file,8) || !file)return false;
        {std::lock_guard<std::mutex> lock(mutex_);eye_binding_={node,file,owner};eye_node=node;eye_gen_.fetch_add(1,std::memory_order_release);}
        if(live_eye_node(reinterpret_cast<void*>(owner.iggy))==node)return true;
        clear_eye();return false;
    }
    static bool has_live_eye() noexcept {
        ++setup_eye_checks;
        if(!mutex_.try_lock()) {reject_eye(0);return false;}
        const auto owner=eye_binding_.owner;mutex_.unlock();
        const bool owned=owner.iggy && live_eye_node(reinterpret_cast<void*>(owner.iggy))!=0;
        if(owned)++setup_eye_passes;
        return owned;
    }
    static void clear_eye() noexcept {std::lock_guard<std::mutex> lock(mutex_);eye_binding_={};eye_node=0;eye_gen_.fetch_add(1,std::memory_order_release);}
    // Render thread, once per displayed frame (0 = no caching).
    static void begin_frame(UINT64 frame) noexcept {frame_.store(frame,std::memory_order_relaxed);eye_draws_.store(0,std::memory_order_relaxed);}
    static UINT64 eye_validations() noexcept {return eye_validations_.load();}
    static UINT64 eye_cache_hits() noexcept {return eye_cache_hits_.load();}
    inline static std::atomic<UINT64> overlay_draws{}; // full-screen overlays (damage vignette) not drawn
    inline static std::atomic<bool> prompt_world{};
    inline static thread_local bool holder_eye_{},holder_eye_hit_{};
    // VR layout of the gameplay HUD movie: every child of its root container is
    // scaled about the eye centre so corner widgets (health, loot log,
    // notifications) land inside the lens view. The separate root (damage
    // vignette) keeps its full-screen matrix. The movie stage is 1920x1080.
    using HudMovie=bool (*)(void* iggy) noexcept;
    inline static std::atomic<HudMovie> hud_movie{};
    // Only positively identified corner widgets may be relaid. Nesting depth
    // alone does not establish that a child is not the full-screen damage mask.
    using HudLayoutNode=bool (*)(void* iggy,void* node) noexcept;
    inline static std::atomic<HudLayoutNode> hud_layout_node{};
    // ui/tutorials.iggy is its own movie, never relaid with MasterHUD, so its
    // panel sat at flat 1920x1080 coordinates outside the lens view. Every
    // child of that movie's root is scaled about the stage centre like hud.iggy widgets.
    inline static std::atomic<HudMovie> tutorial_movie{};
    // Pose epoch the exact tutorial child scopes are stamped with (set by the producer each frame).
    inline static std::atomic<UINT64> tutorial_epoch{};
    inline static std::atomic<float> hud_scale{1.f};
    // HUD window (Dishonored 1 style): when set, prompts, tutorials and (with panel_generic) every
    // other unidentified direct child of the qualified MasterHUD node are scoped as kind panel.
    // The producer redirects them into ONE offscreen surface shown as a VR panel; they are never
    // drawn flat. Wrist vitals and markers keep their own identified kinds. The VR relayout
    // scale is not applied while active so the panel shows the full 1920x1080 stage.
    inline static std::atomic<bool> panel_active{},panel_generic{};
    inline static std::atomic<UINT64> panel_children{};
    // Other gameplay HUD movies (e.g. ui/save_notification.iggy): every nesting-2 child becomes panel content.
    inline static std::atomic<HudMovie> panel_movie{};
    inline static std::atomic<ULONGLONG> master_tick_{};
    inline static thread_local bool master_{};
    inline static std::atomic<UINT64> hud_relayouts{};
    inline static thread_local std::array<float,2> stage_centre_{};
    inline static thread_local std::array<float,3> root_{}; // current movie root: scale, tx, ty
private:
    inline static constexpr std::uintptr_t rva=0x1037930;
    inline static constexpr std::array<unsigned char,29> prologue{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xa8,0xfc,0xff,0xff,0x48,0x81,0xec,0x18,0x04,0x00,0x00};
    inline static std::uintptr_t base_{};inline static Read read_{};inline static Draw original_{};
    inline static bool prompt_entry_abi_{};
    inline static void* target_{};inline static std::atomic<bool> active_{};inline static std::atomic<unsigned> depth_{};
    inline static std::mutex mutex_;inline static std::array<NativeHudChildScope,40> bindings_{};
    inline static NativeInteractionTargetIdentity prompt_target_{};inline static UINT64 prompt_target_tick_{};inline static bool prompt_talk_{};
    // Sticky prompt display roots: any node ever published for a prompt stays identified as a prompt
    // subtree (no target/freshness needed) so it can never fall through to a flat draw.
    inline static std::array<NativeHudChildScope,4> prompt_roots_{};inline static unsigned prompt_root_next_{};
    // Sticky record of every node ever published as a wrist/marker widget (bindings_ entries get recycled).
    inline static std::array<std::uintptr_t,16> widget_nodes_{},widget_iggy_{};inline static unsigned widget_count_{},widget_next_{};
    static NativeHudChildScope identify(void* iggy,void* node,float* matrix) noexcept {
        NativeHudChildScope out{};if(current || !active_.load() || !mutex_.try_lock())return out;
        const auto now=GetTickCount64();
        for(auto& binding:bindings_) if(binding.kind!=NativeHudChildKind::none && binding.iggy==reinterpret_cast<std::uintptr_t>(iggy) && binding.node==reinterpret_cast<std::uintptr_t>(node)) {
            if(binding.kind==NativeHudChildKind::prompt && binding.retained_entry) {
                // Add/Gauge owns an entity source independently of whichever
                // hand is hovered now. Only this exact child Draw renews its
                // scope; it never changes the timestamp of an older image.
                const auto retained=prompt_entry_abi_?native_prompt_entry::refresh(read_,base_,*binding.retained_entry):std::nullopt;
                if(retained) {
                    binding.retained_entry=retained;out=binding;out.tick=now;
                    out.talk_offer=binding.target && binding.target==prompt_target_ && prompt_talk_ &&
                        prompt_target_tick_ && now>=prompt_target_tick_ && now-prompt_target_tick_<=250;
                    break;
                }
                // A pending Add cannot authorize a Draw before the native
                // code has changed pending1 to active4. Missing/removing or
                // reused entries retire immediately, without rebinding.
                if(!prompt_entry_abi_ || !native_prompt_entry::refresh(read_,base_,*binding.retained_entry,false) ||
                   !binding.tick || now<binding.tick || now-binding.tick>250)binding={};
                continue;
            }
            // v714 (headset v713: the counter-serum message never showed): a script message is published every 250 ms at best, on the
            // next UI call, and expired after 250 ms. Its own Draw now renews it, so it lives exactly as long as the game draws it.
            if(binding.kind==NativeHudChildKind::tutorial && now>=binding.tick && now-binding.tick<=1000) {binding.tick=now;out=binding;break;}
            const bool live_prompt=binding.kind==NativeHudChildKind::prompt && binding.target && binding.target==prompt_target_ &&
                prompt_target_tick_ && now>=prompt_target_tick_ && now-prompt_target_tick_<=250;
            if(live_prompt || (now>=binding.tick && now-binding.tick<=250)){out=binding;if(live_prompt)out.tick=now;out.talk_offer=live_prompt && prompt_talk_;break;}
        }
        if(out.kind==NativeHudChildKind::none)for(const auto& root:prompt_roots_)
            if(root.kind==NativeHudChildKind::prompt && root.iggy==reinterpret_cast<std::uintptr_t>(iggy) && root.node==reinterpret_cast<std::uintptr_t>(node)){out=root;out.target={};out.tick=now;break;}
        mutex_.unlock();if(out.kind==NativeHudChildKind::none)return out;
        try {
            if(!wrist_hud_file_owned(read_,base_,out.file) || rig::value<std::uintptr_t>(read_,out.file+0x1608)!=out.iggy || !NativeHudArtProbe::in_tree(read_,out.iggy,out.node))return {};
            out.matrix_valid=matrix && read_(reinterpret_cast<std::uintptr_t>(matrix),out.matrix.data(),sizeof(out.matrix));
            if(out.matrix_valid)for(const auto value:out.matrix)if(!std::isfinite(value))out.matrix_valid=false;
            out.bounds_valid=read_(out.node+0xc8,out.bounds.data(),sizeof(out.bounds));
            if(out.bounds_valid)for(const auto value:out.bounds)if(!std::isfinite(value))out.bounds_valid=false;
        }catch(...){return {};}
        return out;
    }
    // The tutorial movie is not hud.iggy and needs no published binding: each direct child of
    // its root (nesting 2) is identified by the movie itself. Owner = node.
    static NativeHudChildScope tutorial_child(void* iggy,void* node,float* matrix) noexcept {
        NativeHudChildScope out{};const auto epoch=tutorial_epoch.load(std::memory_order_relaxed);
        if(current || !active_.load() || !epoch || nesting_!=2 || !node || !matrix)return out;
        const auto is_tutorial=tutorial_movie.load(std::memory_order_relaxed);
        if(!is_tutorial || !is_tutorial(iggy))return out;
        try {
            std::uintptr_t file{};
            if(!read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) || !file)return {};
            out={NativeHudChildKind::tutorial,file,reinterpret_cast<std::uintptr_t>(iggy),reinterpret_cast<std::uintptr_t>(node),reinterpret_cast<std::uintptr_t>(node),epoch,GetTickCount64()};
            out.matrix_valid=read_(reinterpret_cast<std::uintptr_t>(matrix),out.matrix.data(),sizeof(out.matrix));
            if(out.matrix_valid)for(const auto value:out.matrix)if(!std::isfinite(value))out.matrix_valid=false;
            out.bounds_valid=read_(out.node+0xc8,out.bounds.data(),sizeof(out.bounds));
            if(out.bounds_valid)for(const auto value:out.bounds)if(!std::isfinite(value))out.bounds_valid=false;
        }catch(...){return {};}
        return out;
    }
    // Unidentified direct child of the qualified MasterHUD node (nesting 3) -> panel content.
    // Also: with the window active, siblings of the MasterHUD node in hud.iggy (nesting 2: takedown/combat/
    // stealth hints, loot log, notifications) and nesting-2 children of panel_movie movies. Without this
    // they were drawn flat and, with the VR relayout off, landed outside the lens view.
    // A container holding a node ever bound to the wrist/marker kinds (health, mana, ammo, objectives) keeps
    // its native draw path: the panel must never swallow those widgets, even while their binding is stale.
    static bool holds_bound_widget(void* iggy,void* node) noexcept {
        std::array<std::uintptr_t,40> nodes{};unsigned count=0;
        if(!mutex_.try_lock())return true;
        for(const auto& b:bindings_)if(b.iggy==reinterpret_cast<std::uintptr_t>(iggy) && (b.kind==NativeHudChildKind::health || b.kind==NativeHudChildKind::mana ||
            b.kind==NativeHudChildKind::equipment || b.kind==NativeHudChildKind::objective))nodes[count++]=b.node;
        for(unsigned i=0;i<widget_count_;++i)if(widget_iggy_[i]==reinterpret_cast<std::uintptr_t>(iggy) && count<nodes.size())nodes[count++]=widget_nodes_[i];
        mutex_.unlock();
        for(unsigned i=0;i<count;++i){std::uintptr_t at=nodes[i];
            for(unsigned hop=0;hop<12 && at;++hop){if(at==reinterpret_cast<std::uintptr_t>(node))return true;if(!read_(at+0x80,&at,sizeof(at)))break;}}
        return false;
    }
    static NativeHudChildScope panel_child(void* iggy,void* node,float* matrix,bool sibling) noexcept {
        NativeHudChildScope out{};const auto epoch=tutorial_epoch.load(std::memory_order_relaxed);
        if(current || !active_.load() || !epoch || !node || !matrix ||
           !((nesting_==3 && master_) || (nesting_==2 && sibling) || (nesting_==holder_level_+1 && ((master_ && holder_level_>=3) || (holder_eye_ && holder_level_>=2)))))return out;
        if(const auto eye=live_eye_node(iggy)) {
            if(reinterpret_cast<std::uintptr_t>(node)==eye)return out;
            std::uintptr_t at=eye;for(unsigned hop=0;hop<12 && at;++hop){if(!read_(at+0x80,&at,sizeof(at)))break;
                if(at==reinterpret_cast<std::uintptr_t>(node)){holder_hit_=true;holder_eye_hit_=true;return out;}}
        }
        if(holds_bound_widget(iggy,node)){holder_hit_=true;return out;}
        if(master_ && holder_level_>=3 && nesting_==holder_level_+1 && !holder_eye_){holder_hide_=true;return out;}
        if(nesting_==2 && full_screen_overlay(node))return out;
        // The damage layer (edge stripes, hit arcs) is a full-screen MasterHUD child: fitted into the panel it was a
        // hard red square in the headset (census 2026-10-12). Not drawn; the game's full-frame damage tint remains.
        if(nesting_==3 && master_ && full_screen_overlay(node)){holder_hide_=true;++overlay_draws;return out;}
        // Takedown hints are only ever the world indicator over the NPC (takedown_child): on the HUD screen they were a
        // second copy whenever the native sensor flickered (headset v346).
        {std::array<float,6> m{};if(takedown_hint_node(matrix,m)){holder_hide_=true;return out;}}
        try {
            std::uintptr_t file{};
            if(!read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) || !file)return {};
            out={NativeHudChildKind::panel,file,reinterpret_cast<std::uintptr_t>(iggy),reinterpret_cast<std::uintptr_t>(node),reinterpret_cast<std::uintptr_t>(node),epoch,GetTickCount64()};
            out.matrix_valid=read_(reinterpret_cast<std::uintptr_t>(matrix),out.matrix.data(),sizeof(out.matrix));
            if(out.matrix_valid)for(const auto value:out.matrix)if(!std::isfinite(value))out.matrix_valid=false;
            out.bounds_valid=read_(out.node+0xc8,out.bounds.data(),sizeof(out.bounds));
            if(out.bounds_valid)for(const auto value:out.bounds)if(!std::isfinite(value))out.bounds_valid=false;
        }catch(...){return {};}
        return out;
    }
    // Takedown hints (kill/choke glyphs) are the MasterHUD child laid out below the crosshair (stage ~960,711;
    // census 2026-10-12: its two glyph groups drew exactly while the kill/choke sensors qualified). While the
    // producer reports a qualified takedown NPC they are prompt art over that NPC instead of panel content.
    static bool takedown_hint_node(float* matrix,std::array<float,6>& m) noexcept {
        if(!(nesting_==3 && master_) || !matrix || root_[0]<=0.f || !read_(reinterpret_cast<std::uintptr_t>(matrix),m.data(),sizeof(m)))return false;
        const float sx=(m[4]-root_[1])/root_[0],sy=(m[5]-root_[2])/root_[0];
        return std::fabs(sx-960.f)<=60.f && sy>=600.f && sy<=820.f;
    }
    static NativeHudChildScope takedown_child(void* iggy,void* node,float* matrix) noexcept {
        NativeHudChildScope out{};const auto epoch=tutorial_epoch.load(std::memory_order_relaxed);
        if(!node || !epoch)return out;
        try {
            std::array<float,6> m{};if(!takedown_hint_node(matrix,m))return out;
            NativeInteractionTargetIdentity target{};UINT64 tick{};bool message=false;
            if(!mutex_.try_lock())return out;target=takedown_;tick=takedown_tick_;
            // v720 (headset v717: Dr. Hypatia's "Press [Grip] to inject counter-serum." appeared only once the choke had begun): the
            // game lays its script messages out in this same stage region (y 650-690). While a takedown was offered, this claimed the
            // message node as the takedown hint, so the message the game raises with the offer was never seen until the offer ended.
            for(const auto& b:bindings_) if(b.kind==NativeHudChildKind::tutorial && b.iggy==reinterpret_cast<std::uintptr_t>(iggy) && b.node==reinterpret_cast<std::uintptr_t>(node)) {message=true;break;}
            mutex_.unlock();if(message)return out;
            const auto now=GetTickCount64();if(!target || !tick || now<tick || now-tick>250)return out;
            std::uintptr_t file{};if(!read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) || !file)return {};
            out={NativeHudChildKind::prompt,file,reinterpret_cast<std::uintptr_t>(iggy),reinterpret_cast<std::uintptr_t>(node),reinterpret_cast<std::uintptr_t>(node),epoch,now};
            out.target=target;out.takedown=true;out.matrix=m;out.matrix_valid=true;for(const auto value:m)if(!std::isfinite(value))out.matrix_valid=false;
            out.bounds_valid=read_(out.node+0xc8,out.bounds.data(),sizeof(out.bounds));
            if(out.bounds_valid)for(const auto value:out.bounds)if(!std::isfinite(value))out.bounds_valid=false;
            ++takedown_hints;
        }catch(...){return {};}
        return out;
    }
    inline static NativeInteractionTargetIdentity takedown_{};inline static UINT64 takedown_tick_{};
    // Full-screen overlay sibling (DamageVignette: its stripes are laid out by the game to the real render
    // target, measured bounds 0..2957 px tall). In the panel its soft stripes were cut off at the panel's edges;
    // drawn natively they end at the eye frame edge. The target size follows from the movie root, which fits
    // the 1920x1080 stage by width and centres it.
    static bool full_screen_overlay(void* node) noexcept {
        std::array<float,4> b{};
        if(!node || !read_ || root_[0]<=0.f || !read_(reinterpret_cast<std::uintptr_t>(node)+0xc8,b.data(),sizeof(b)))return false;
        for(const auto v:b)if(!std::isfinite(v) || std::fabs(v)>1e6f)return false;
        return native_hud_full_screen(b,root_[0],root_[1],root_[2]);
    }
    static bool unbounded_container(void* node) noexcept {
        std::array<float,4> b{};
        return node && read_ && read_(reinterpret_cast<std::uintptr_t>(node)+0xc8,b.data(),sizeof(b)) && b[0]<-1e6f && b[1]<-1e6f && b[2]>1e6f && b[3]>1e6f;
    }
    static std::uint32_t __fastcall hook(void* iggy,float* matrix,void* node,void* aux,void* p5,void* p6,std::uint32_t clip,float depth) {
        ++depth_;++calls;++nesting_;
        const auto profile=cpu_profile.load(std::memory_order_relaxed);
        if(profile)profile(0,nesting_);
        if(const auto observe=census.load(std::memory_order_relaxed)) observe(iggy,matrix,node,nesting_);
        if(node && reinterpret_cast<std::uintptr_t>(node)==eye_node.load(std::memory_order_relaxed) &&
           reinterpret_cast<std::uintptr_t>(node)==live_eye_node(iggy)) {
            alignas(16) std::array<float,6> glyph{};
            bool scaled=matrix && read_(reinterpret_cast<std::uintptr_t>(matrix),glyph.data(),sizeof glyph);
            if(scaled && eye_layout_scale_!=1.f) {scaled=native_crosshair_unlayout(glyph,eye_layout_scale_,eye_layout_centre_);if(scaled)++crosshair_unlayouts;}
            if(scaled)scaled=native_crosshair_glyph_scale(glyph,hud_scale.load(std::memory_order_relaxed));
            const unsigned eye_draw=eye_draws_.fetch_add(1,std::memory_order_relaxed);
            NativeHudChildScope crosshair{};bool captured=false;const auto previous=current;const auto stop=end.load(std::memory_order_acquire);
            if(scaled && !crosshair_hidden.load(std::memory_order_relaxed) && crosshair_composed.load(std::memory_order_relaxed))
                if(const auto start=begin.load(std::memory_order_acquire);start && stop) {
                    std::uintptr_t file{};
                    if(read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) && file) {
                        crosshair={NativeHudChildKind::crosshair,file,reinterpret_cast<std::uintptr_t>(iggy),reinterpret_cast<std::uintptr_t>(node),
                            reinterpret_cast<std::uintptr_t>(node),tutorial_epoch.load(std::memory_order_relaxed),GetTickCount64()};
                        crosshair.matrix=glyph;crosshair.matrix_valid=true;
                        current=&crosshair;captured=start(crosshair);
                        if(captured) {crosshair_anchor_x.store(glyph[4],std::memory_order_relaxed);crosshair_anchor_y.store(glyph[5],std::memory_order_relaxed);++crosshair_captures;}
                        else current=previous;
                    }
                }
            if(!captured && (crosshair_hidden.load(std::memory_order_relaxed) || crosshair_composed.load(std::memory_order_relaxed))) {native_crosshair_hide(glyph);scaled=true;++crosshair_hidden_draws;} // read failed: a zero matrix is invisible too
            else if(!captured && scaled)
                if(const auto shift=eye_shift.load(std::memory_order_relaxed))
                    if(const auto extra=shift(eye_draw)) {glyph[4]+=(*extra)[0];glyph[5]+=(*extra)[1];++crosshair_eye_shifts;}
            std::uint32_t result{};
            if(profile)profile(1,nesting_);
            __try {result=original_(iggy,scaled?glyph.data():matrix,node,aux,p5,p6,clip,depth);}
            __finally {if(captured){stop(crosshair);current=previous;}if(profile){profile(2,nesting_);profile(3,nesting_);}--nesting_;--depth_;}
            return result;
        }
        // Full-screen overlays (DamageVignette) are laid out for the flat screen; in the eye they showed as a sharp-edged
        // square (headset). Not drawn: the game's own full-frame damage tint post-process remains.
        const bool hide_overlay=panel_active.load(std::memory_order_relaxed) && nesting_==2 && root_[0]>0.f && read_ && full_screen_overlay(node);
        if(hide_overlay) ++overlay_draws;
        alignas(16) std::array<float,6> laid{};bool relaid=false;float inherited_layout=1.f;
        const bool window=panel_active.load(std::memory_order_relaxed);bool entered_master=false,sibling=false,holder_entered=false,hide_child=false,eye_saved=false;unsigned holder_saved=0;
        if(window && nesting_==2 && !master_) {
            const auto is_hud=hud_movie.load(std::memory_order_relaxed);const auto qualifies=hud_layout_node.load(std::memory_order_relaxed);
            // MasterHUD: the published layout node, or (when that probe has not fired) the one unbounded
            // nesting-2 container of hud.iggy - measured: every HUD widget, takedown hints included, lives under it.
            if(is_hud && is_hud(iggy) && ((qualifies && qualifies(iggy,node)) || unbounded_container(node))) {master_=true;entered_master=true;master_tick_=GetTickCount64();++master_entries;}
            else if(is_hud && is_hud(iggy)) sibling=GetTickCount64()-master_tick_.load()<1000; // only once the master node is known
            else if(const auto extra=panel_movie.load(std::memory_order_relaxed);extra && extra(iggy)) sibling=true;
        }
        if(nesting_==1 && iggy)
            if(const auto is_hud=hud_movie.load(std::memory_order_relaxed);is_hud && is_hud(iggy)) {
                std::uintptr_t file{};if(read_ && read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) && file)hud_file.store(file,std::memory_order_relaxed);
                std::array<float,6> root{};if(matrix && read_ && read_(reinterpret_cast<std::uintptr_t>(matrix),root.data(),sizeof(root)) && root[0]>0.f && std::isfinite(root[0]))
                    hud_root_scale_pub.store(root[0],std::memory_order_relaxed);}
        if(window && matrix && nesting_==1)
            if(const auto is_hud=hud_movie.load(std::memory_order_relaxed),is_tutorial=tutorial_movie.load(std::memory_order_relaxed),extra=panel_movie.load(std::memory_order_relaxed);
               ((is_hud && is_hud(iggy)) || (is_tutorial && is_tutorial(iggy)) || (extra && extra(iggy))) && read_(reinterpret_cast<std::uintptr_t>(matrix),laid.data(),sizeof(laid)))
                {stage_centre_={laid[4]+laid[0]*960.f,laid[5]+laid[3]*540.f};root_={laid[0],laid[4],laid[5]};publish_root(laid);} // panel children are fitted from this root
        if(const float k=hud_scale.load(std::memory_order_relaxed);!window && k<.999f && matrix && nesting_<=2)
            if(const auto is_hud=hud_movie.load(std::memory_order_relaxed),is_tutorial=tutorial_movie.load(std::memory_order_relaxed);
               ((is_hud && is_hud(iggy)) || (is_tutorial && is_tutorial(iggy))) && read_(reinterpret_cast<std::uintptr_t>(matrix),laid.data(),sizeof(laid))) {
                if(nesting_==1) {stage_centre_={laid[4]+laid[0]*960.f,laid[5]+laid[3]*540.f};publish_root(laid);}
                else {
                    const auto qualifies=hud_layout_node.load(std::memory_order_relaxed);const auto tutorial=tutorial_movie.load(std::memory_order_relaxed);
                    if(((qualifies && qualifies(iggy,node)) || (tutorial && tutorial(iggy))) && stage_centre_[0]>0.f && stage_centre_[1]>0.f) {
                        for(unsigned i=0;i<4;++i) laid[i]*=k;
                        laid[4]=stage_centre_[0]+k*(laid[4]-stage_centre_[0]);laid[5]=stage_centre_[1]+k*(laid[5]-stage_centre_[1]);
                        relaid=true;for(const auto value:laid) relaid=relaid && std::isfinite(value);
                        if(relaid) {++hud_relayouts;inherited_layout=k;}
                    }
                }
            }
        if(!window && matrix && nesting_==1 && hud_scale.load(std::memory_order_relaxed)>=.999f) // the crosshair needs the eye target size at any HUD size
            if(const auto is_hud=hud_movie.load(std::memory_order_relaxed);is_hud && is_hud(iggy) && read_(reinterpret_cast<std::uintptr_t>(matrix),laid.data(),sizeof(laid)))publish_root(laid);
        // v700 (headset v699: an NPC's Talk prompt still blinked): the choke/kill hint is recognised before the general prompt
        // identification, which could claim it as an ordinary prompt (slot 0) while the Talk window held slot 0 too.
        auto scope=tutorial_child(iggy,node,matrix);
        if(window && scope.kind==NativeHudChildKind::none)scope=takedown_child(iggy,node,matrix);
        if(scope.kind==NativeHudChildKind::none)scope=identify(iggy,node,matrix);
        if(window) {
            // v698: prompts are world art only (no HUD screen frames). v710: so are all tutorial and information messages, the
            // tutorial movie's cards included: they are laid out head-locked at the game's own stage positions (dxgi_probe), none on the panel.
            constexpr bool tutorial=false;
            if(scope.kind==NativeHudChildKind::none && panel_generic.load(std::memory_order_relaxed)) {
                holder_hit_=holder_hide_=holder_eye_hit_=false;scope=panel_child(iggy,node,matrix,sibling);hide_child=holder_hide_;holder_hide_=false;
                if(holder_hit_){holder_saved=holder_level_;eye_saved=holder_eye_;holder_level_=nesting_;holder_eye_=holder_eye_hit_;holder_entered=true;}
            }
            // Native layout, fitted whole into the panel texture (tutorials keep their centring shift).
            if(scope.kind==NativeHudChildKind::panel && scope.matrix_valid && root_[0]>0.f)
                if(const float w=static_cast<float>(panel_width.load(std::memory_order_relaxed)),h=static_cast<float>(panel_height.load(std::memory_order_relaxed));w>0.f && h>0.f) {
                    laid=native_hud_stage_fit(tutorial && relaid?laid:scope.matrix,root_[0],root_[1],root_[2],w,h);
                    relaid=true;for(const auto value:laid) relaid=relaid && std::isfinite(value);if(relaid)++hud_relayouts;
                }
            if(scope.kind==NativeHudChildKind::panel)++panel_children;
        }
        const auto previous=current;
        const auto watch=observer.load(std::memory_order_acquire);const auto start=begin.load(std::memory_order_acquire);const auto stop=end.load(std::memory_order_acquire);
        bool capture=false,changed=false;alignas(16) std::array<float,6> copied=scope.matrix;
        if(scope.kind!=NativeHudChildKind::none){
            ++identified;current=&scope;if(watch)watch(scope,true,0);
            if(start && stop)capture=start(scope);
            // Capture and in-place relocation are mutually exclusive producers.
            if(!capture && scope.matrix_valid)if(const auto adjust=transform.load(std::memory_order_acquire)) {
                changed=adjust(scope,copied);for(const auto value:copied)if(!std::isfinite(value))changed=false;
            }
            if(capture)++captures;if(changed)++transforms;
        }
        std::uint32_t result{};
        // VR-only prompt: an identified prompt subtree that is not being captured is never drawn flat. v814 (headset v812: "I should not
        // see this huge white tutorial notification, there is a VR copy"): nor a tutorial or information message (v710 made them VR-only,
        // but a draw whose capture was refused, its slot busy or the frame not capturing, still went flat across the eye).
        const bool hide=hide_child && scope.kind==NativeHudChildKind::none && start;
        if(hide)++holder_hidden;
        const bool suppress=hide || hide_overlay || ((scope.kind==NativeHudChildKind::prompt || scope.kind==NativeHudChildKind::panel ||
                                                     scope.kind==NativeHudChildKind::tutorial) && !capture && start);
        if(suppress)++suppressed;
        if(profile)profile(1,nesting_);
        const auto saved_eye_scale=eye_layout_scale_;const auto saved_eye_centre=eye_layout_centre_;
        if(inherited_layout!=1.f && relaid && !changed && !suppress) {eye_layout_scale_=inherited_layout;eye_layout_centre_=stage_centre_;}
        __try {if(!suppress)result=original_(iggy,changed?copied.data():relaid?laid.data():matrix,node,aux,p5,p6,clip,depth);}
        __finally {eye_layout_scale_=saved_eye_scale;eye_layout_centre_=saved_eye_centre;if(profile)profile(2,nesting_);if(scope.kind!=NativeHudChildKind::none){if(capture)stop(scope);if(watch)watch(scope,false,result);current=previous;}if(entered_master)master_=false;if(holder_entered){holder_level_=holder_saved;holder_eye_=eye_saved;}if(profile)profile(3,nesting_);--nesting_;--depth_;}
        return result;
    }
public:
    // A new native event supersedes the artwork of this exact node. Unknown
    // event ownership returns its pixels to the established HUD panel path.
    static void retire_prompt_node(std::uintptr_t iggy,std::uintptr_t node) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& binding:bindings_)if(binding.iggy==iggy && binding.node==node &&
            (binding.kind==NativeHudChildKind::prompt || binding.kind==NativeHudChildKind::panel))binding={};
        for(auto& root:prompt_roots_)if(root.iggy==iggy && root.node==node)root={};
    }
    // Native Remove may name another label in the same InteractionWindow.
    // Retire only the exact removed source; the compound window (including
    // its native hold gauge) remains captured through the existing node.
    static void clear_prompt_entry(std::uintptr_t element,std::uint32_t key) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& binding:bindings_)if(binding.kind==NativeHudChildKind::prompt && binding.retained_entry &&
            binding.retained_entry->element==element && binding.retained_entry->key==key)binding={};
    }
    static bool has_retained_prompt(UINT64 epoch,UINT64 tick) {
        if(!active_.load() || !epoch || !tick || !read_ || !prompt_entry_abi_)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& binding:bindings_)if(binding.kind==NativeHudChildKind::prompt && binding.retained_entry &&
            binding.epoch==epoch && binding.tick && tick>=binding.tick &&
            wrist_hud_file_owned(read_,base_,binding.file) && rig::value<std::uintptr_t>(read_,binding.file+0x1608)==binding.iggy &&
            NativeHudArtProbe::in_tree(read_,binding.iggy,binding.node)) {
            if(const auto retained=native_prompt_entry::refresh(read_,base_,*binding.retained_entry)) {
                binding.retained_entry=retained;return true;
            }
        }
        return false;
    }
    // Last-good artwork may be held only while its exact native source is
    // still bound. A same-node event for another key supersedes that source.
    // This check observes ownership; it never renews an image or scope tick.
    static bool scope_current(const NativeHudChildScope& scope,UINT64 epoch,UINT64 tick) {
        if(!active_.load() || !epoch || !tick || !read_ || scope.epoch!=epoch)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& binding:bindings_)if(native_hud_child_same_identity(binding,scope) && binding.tick && tick>=binding.tick &&
            wrist_hud_file_owned(read_,base_,binding.file) && rig::value<std::uintptr_t>(read_,binding.file+0x1608)==binding.iggy &&
            NativeHudArtProbe::in_tree(read_,binding.iggy,binding.node)) {
            if(binding.retained_entry)return prompt_entry_abi_ && native_prompt_entry::refresh(read_,base_,*binding.retained_entry).has_value();
            const bool live_prompt=binding.kind==NativeHudChildKind::prompt && binding.target && binding.target==prompt_target_ &&
                prompt_target_tick_ && tick>=prompt_target_tick_ && tick-prompt_target_tick_<=250;
            return live_prompt || tick-binding.tick<=250;
        }
        return false;
    }
    static bool publish(NativeHudChildKind kind,const NativeHudArtGeometry& geometry,std::uintptr_t owner,UINT64 epoch,UINT64 tick,NativeInteractionTargetIdentity target={},bool prompt_event_pending=false,std::uint64_t prompt_ticket=0,std::optional<native_prompt_entry::Receipt> retained_entry={}) {
        auto reject=[] {++publish_rejects;return false;};
        if(kind==NativeHudChildKind::prompt && read_ && owner && geometry.display_node && wrist_hud_file_owned(read_,base_,geometry.file) &&
           rig::value<std::uintptr_t>(read_,geometry.file+0x1608)==geometry.iggy && NativeHudArtProbe::in_tree(read_,geometry.iggy,geometry.display_node)) {
            std::lock_guard<std::mutex> lock(mutex_);NativeHudChildScope* slot{};
            for(auto& root:prompt_roots_)if(root.iggy==geometry.iggy && root.node==geometry.display_node)slot=&root;
            if(!slot)slot=&prompt_roots_[prompt_root_next_++%prompt_roots_.size()];
            *slot={kind,geometry.file,geometry.iggy,geometry.display_node,owner,epoch,tick};
        }
        if(!read_ || kind==NativeHudChildKind::none || !owner || !geometry.display_node || (geometry.fields&31)!=31 || !geometry.visible ||
           (kind==NativeHudChildKind::prompt && target && target.epoch!=epoch) ||
           (retained_entry && (kind!=NativeHudChildKind::prompt || owner!=retained_entry->element || !prompt_entry_abi_ ||
               !native_prompt_entry::refresh(read_,base_,*retained_entry,false))) ||
           !wrist_hud_file_owned(read_,base_,geometry.file) || rig::value<std::uintptr_t>(read_,geometry.file+0x1608)!=geometry.iggy || !NativeHudArtProbe::in_tree(read_,geometry.iggy,geometry.display_node))return reject();
        std::lock_guard<std::mutex> lock(mutex_);NativeHudChildScope* free{};
        for(auto& binding:bindings_){if(binding.kind==kind && binding.owner==owner){free=&binding;break;}
            if(!free && (binding.kind==NativeHudChildKind::none || binding.epoch!=epoch || (tick>=binding.tick && tick-binding.tick>250 &&
                !(binding.kind==NativeHudChildKind::prompt && binding.retained_entry && prompt_entry_abi_ &&
                    native_prompt_entry::refresh(read_,base_,*binding.retained_entry)))))free=&binding;}
        if(kind==NativeHudChildKind::health || kind==NativeHudChildKind::mana || kind==NativeHudChildKind::equipment || kind==NativeHudChildKind::objective) {
            bool known=false;for(unsigned i=0;i<widget_count_;++i)known=known || (widget_nodes_[i]==geometry.display_node && widget_iggy_[i]==geometry.iggy);
            if(!known){const unsigned at=widget_next_++%static_cast<unsigned>(widget_nodes_.size());widget_nodes_[at]=geometry.display_node;widget_iggy_[at]=geometry.iggy;widget_count_=std::min<unsigned>(widget_count_+1,static_cast<unsigned>(widget_nodes_.size()));}
        }
        if(!free)return reject();*free={kind,geometry.file,geometry.iggy,geometry.display_node,owner,epoch,tick};free->target=target;free->prompt_event_pending=kind==NativeHudChildKind::prompt && prompt_event_pending;free->prompt_ticket=prompt_ticket;free->retained_entry=retained_entry;++publish_accepts;return true;
    }
    static void clear() {std::lock_guard<std::mutex> lock(mutex_);eye_binding_={};eye_node=0;eye_gen_.fetch_add(1,std::memory_order_release);bindings_={};prompt_roots_={};prompt_target_={};prompt_target_tick_=0;prompt_talk_=false;takedown_={};takedown_tick_=0;widget_nodes_={};widget_iggy_={};widget_count_=widget_next_=0;}
    // Producer: the NPC the native takedown sensor qualified this frame (empty = none).
    static void set_takedown(NativeInteractionTargetIdentity target,UINT64 tick) {std::lock_guard<std::mutex> lock(mutex_);takedown_=target;takedown_tick_=target?tick:0;}
    inline static std::atomic<UINT64> takedown_hints{};
    static void clear_kind(NativeHudChildKind kind) {std::lock_guard<std::mutex> lock(mutex_);for(auto& binding:bindings_)if(binding.kind==kind)binding={};if(kind==NativeHudChildKind::prompt){prompt_target_={};prompt_target_tick_=0;prompt_talk_=false;}}
    // Stat changes are sparse. Refresh their already-owned child identities
    // only when that same native HUD movie is being drawn again.
    static void refresh_wrist(std::uintptr_t file,std::uintptr_t iggy,UINT64 epoch,UINT64 tick) {
        if(!epoch || !tick || !read_ || !wrist_hud_file_owned(read_,base_,file) || rig::value<std::uintptr_t>(read_,file+0x1608)!=iggy) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& binding:bindings_) if(binding.file==file && binding.iggy==iggy &&
            (binding.kind==NativeHudChildKind::health || binding.kind==NativeHudChildKind::mana || binding.kind==NativeHudChildKind::equipment) &&
            NativeHudArtProbe::in_tree(read_,iggy,binding.node)) {binding.epoch=epoch;binding.tick=tick;}
    }
    // Component-owned events require their current same target. Retained-entry
    // events follow that native source instead. Only the exact child Draw
    // renews the observed scope; a movie Draw cannot renew captured pixels.
    static void refresh_prompt(std::uintptr_t file,std::uintptr_t iggy,UINT64 epoch,UINT64 tick,NativeInteractionTargetIdentity target,bool exact_talk_offer=false,NativePromptCompletions completions={}) {
        if(!epoch || !tick || !read_ || !wrist_hud_file_owned(read_,base_,file) || rig::value<std::uintptr_t>(read_,file+0x1608)!=iggy)return;
        std::lock_guard<std::mutex> lock(mutex_);
        prompt_target_=target && target.epoch==epoch?target:NativeInteractionTargetIdentity{};prompt_target_tick_=tick;prompt_talk_=static_cast<bool>(prompt_target_) && exact_talk_offer;
        for(auto& binding:bindings_) if(binding.kind==NativeHudChildKind::prompt) {
            if(binding.file!=file || binding.iggy!=iggy || binding.epoch!=epoch) {binding={};continue;}
            if(binding.retained_entry) {
                const auto retained=prompt_entry_abi_?native_prompt_entry::refresh(read_,base_,*binding.retained_entry,false):std::nullopt;
                if(retained)binding.retained_entry=retained;else binding={};
                continue;
            }
            // The event records its exact in-flight stage0 ticket. That
            // invocation's immutable result owns the art, even if other
            // queries complete before Draw. Fresh child capture is still required.
            if(binding.prompt_event_pending) {
                if(!binding.tick || tick<binding.tick || tick-binding.tick>250) {binding={};continue;}
                const auto completed=completions.get(binding.prompt_ticket);
                if(!completed.sequence) {binding={};continue;} // exact invocation was evicted/unknown
                if(completed.completed) {
                    if(completed.target && completed.target.epoch==epoch && completed.target==prompt_target_) {
                        binding.target=completed.target;binding.prompt_event_pending=false;
                    } else binding={}; // rejected/empty completion consumes its own event
                }
            } else if(binding.target && binding.target!=prompt_target_)binding={};
        }
    }
    static bool install(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;base_=base;read_=read;prompt_entry_abi_=native_prompt_entry::verified_abi(read_,base_);
        if(!target_){std::array<unsigned char,prologue.size()> live{};if(!read(base+rva,live.data(),live.size()) || live!=prologue)return false;
            target_=reinterpret_cast<void*>(base+rva);if(MH_CreateHook(target_,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK){target_=nullptr;return false;}}
        const auto status=MH_EnableHook(target_);active_=status==MH_OK || status==MH_ERROR_ENABLED;return active_.load();
    }
    static bool disable() noexcept {
        cpu_profile=nullptr;eye_live=nullptr;eye_shift=nullptr;crosshair_hidden=false;crosshair_composed=false;root_scale_pub=0.f;hud_root_scale_pub=0.f;clear_eye();active_=false;transform=nullptr;begin=nullptr;end=nullptr;observer=nullptr;hud_movie=nullptr;tutorial_movie=nullptr;panel_movie=nullptr;tutorial_epoch=0;hud_layout_node=nullptr;hud_scale=1.f;panel_active=false;panel_generic=false;hud_file=0;eye_node=0;prompt_world=false;
        if(!target_)return true;const auto result=MH_DisableHook(target_);if(result!=MH_OK && result!=MH_ERROR_DISABLED)return false;
        const auto until=GetTickCount64()+2000;while(depth_ && GetTickCount64()<until)Sleep(1);if(depth_)return false;clear();return true;
    }
    // Fixtures execute the exact pass-through hook against an injected original.
    static void fixture(Read read,std::uintptr_t base,Draw original,bool trusted_prompt_entry_abi=false) {read_=read;base_=base;original_=original;prompt_entry_abi_=trusted_prompt_entry_abi;active_=true;clear();}
    static std::uint32_t invoke_fixture(void* iggy,float* matrix,void* node,void* aux,void* p5,void* p6,std::uint32_t clip,float depth){return hook(iggy,matrix,node,aux,p5,p6,clip,depth);}
};
} // namespace d2vr
