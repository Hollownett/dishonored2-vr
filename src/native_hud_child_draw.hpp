#pragma once
#include "native_hud_art_probe.hpp"
#include "interaction_ray.hpp"
#include <atomic>
#include <mutex>

namespace d2vr {
enum class NativeHudChildKind:unsigned {none,prompt,health,mana,equipment,objective,tutorial,panel};
struct NativeHudChildScope {
    NativeHudChildKind kind{};std::uintptr_t file{},iggy{},node{},owner{};UINT64 epoch{},tick{};
    std::array<float,6> matrix{};std::array<float,4> bounds{};bool matrix_valid{},bounds_valid{};
    NativeInteractionTargetIdentity target{};
};
inline bool native_hud_child_same_identity(const NativeHudChildScope& a,const NativeHudChildScope& b) noexcept {
    return a.kind!=NativeHudChildKind::none && a.kind==b.kind && a.file==b.file && a.iggy==b.iggy &&
        a.node==b.node && a.owner==b.owner && a.epoch && a.epoch==b.epoch &&
        (a.kind!=NativeHudChildKind::prompt || a.target==b.target); // a sticky prompt root has no target on either side
}
// A committed image may authorize flat suppression only for this exact target
// and pose generation. Current native observation and artwork age are bounded
// independently: refreshing a target never renews old pixels.
inline bool native_hud_child_replacement_ready(const NativeHudChildScope& current,const NativeHudChildScope& committed,
        UINT64 image_tick,UINT64 now,UINT64 max_age=250) noexcept {
    return native_hud_child_same_identity(current,committed) && current.tick && image_tick &&
        now>=current.tick && now-current.tick<=max_age && now>=image_tick && now-image_tick<=max_age;
}
// Last good world prompt: lets one failed capture/readback/placement frame keep showing
// the previous world art (republished with a fresh tick) for a short, bounded grace.
struct NativeHudPromptHold {
    UINT64 tick{};Vec3 point{};double width{.24};
    void good(UINT64 now,Vec3 p,double w) noexcept {tick=now;point=p;width=w;}
    bool holds(UINT64 now,UINT64 grace=600) const noexcept {return tick && now>=tick && now-tick<=grace;} // 600: rides out takedown sensor flicker
    void reset() noexcept {tick=0;}
};
// The flat prompt is allowed only when no interaction target exists and no world art is
// committed or held. Other kinds keep the committed-image rule.
inline bool native_hud_keep_flat(NativeHudChildKind kind,bool target_exists,bool replacement_ready,bool held) noexcept {
    // User requirement: object prompts are VR-only; the flat HUD copy is never drawn. Vitals (health, mana,
    // ammo) live on the wrist: a flat copy would be a head-locked widget in the corner of the lens.
    (void)target_exists;(void)held;
    if(kind==NativeHudChildKind::prompt || kind==NativeHudChildKind::health || kind==NativeHudChildKind::mana || kind==NativeHudChildKind::equipment)return false;
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
    inline static std::atomic<UINT64> calls{},identified{},captures{},transforms{},publish_accepts{},publish_rejects{},suppressed{},master_entries{};
    inline static std::atomic<unsigned> panel_width{},panel_height{}; // HUD window capture texture, set by its producer
    inline static thread_local const NativeHudChildScope* current{};
    // Diagnostic (build/hud-node-census): every drawn display node, with hook nesting depth.
    using Census=void (*)(void* iggy,const float* matrix,void* node,unsigned nesting) noexcept;
    inline static std::atomic<Census> census{};
    inline static thread_local unsigned nesting_{};
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
    inline static std::atomic<std::uintptr_t> eye_node{};
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
    inline static void* target_{};inline static std::atomic<bool> active_{};inline static std::atomic<unsigned> depth_{};
    inline static std::mutex mutex_;inline static std::array<NativeHudChildScope,40> bindings_{};
    inline static NativeInteractionTargetIdentity prompt_target_{};inline static UINT64 prompt_target_tick_{};
    // Sticky prompt display roots: any node ever published for a prompt stays identified as a prompt
    // subtree (no target/freshness needed) so it can never fall through to a flat draw.
    inline static std::array<NativeHudChildScope,4> prompt_roots_{};inline static unsigned prompt_root_next_{};
    // Sticky record of every node ever published as a wrist/marker widget (bindings_ entries get recycled).
    inline static std::array<std::uintptr_t,16> widget_nodes_{},widget_iggy_{};inline static unsigned widget_count_{},widget_next_{};
    static NativeHudChildScope identify(void* iggy,void* node,float* matrix) noexcept {
        NativeHudChildScope out{};if(current || !active_.load() || !mutex_.try_lock())return out;
        const auto now=GetTickCount64();
        for(auto& binding:bindings_) if(binding.kind!=NativeHudChildKind::none && binding.iggy==reinterpret_cast<std::uintptr_t>(iggy) && binding.node==reinterpret_cast<std::uintptr_t>(node)) {
            const bool live_prompt=binding.kind==NativeHudChildKind::prompt && binding.target && binding.target==prompt_target_ &&
                prompt_target_tick_ && now>=prompt_target_tick_ && now-prompt_target_tick_<=250;
            if(live_prompt || (now>=binding.tick && now-binding.tick<=250)){out=binding;if(live_prompt)out.tick=now;break;}
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
        if(const auto eye=eye_node.load(std::memory_order_relaxed)) {
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
            NativeInteractionTargetIdentity target{};UINT64 tick{};
            if(!mutex_.try_lock())return out;target=takedown_;tick=takedown_tick_;mutex_.unlock();
            const auto now=GetTickCount64();if(!target || !tick || now<tick || now-tick>250)return out;
            std::uintptr_t file{};if(!read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) || !file)return {};
            out={NativeHudChildKind::prompt,file,reinterpret_cast<std::uintptr_t>(iggy),reinterpret_cast<std::uintptr_t>(node),reinterpret_cast<std::uintptr_t>(node),epoch,now};
            out.target=target;out.matrix=m;out.matrix_valid=true;for(const auto value:m)if(!std::isfinite(value))out.matrix_valid=false;
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
        if(const auto observe=census.load(std::memory_order_relaxed)) observe(iggy,matrix,node,nesting_);
        // Full-screen overlays (DamageVignette) are laid out for the flat screen; in the eye they showed as a sharp-edged
        // square (headset). Not drawn: the game's own full-frame damage tint post-process remains.
        const bool hide_overlay=panel_active.load(std::memory_order_relaxed) && nesting_==2 && root_[0]>0.f && read_ && full_screen_overlay(node);
        if(hide_overlay) ++overlay_draws;
        alignas(16) std::array<float,6> laid{};bool relaid=false;
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
                std::uintptr_t file{};if(read_ && read_(reinterpret_cast<std::uintptr_t>(iggy)+0x78,&file,sizeof(file)) && file)hud_file.store(file,std::memory_order_relaxed);}
        if(window && matrix && nesting_==1)
            if(const auto is_hud=hud_movie.load(std::memory_order_relaxed),is_tutorial=tutorial_movie.load(std::memory_order_relaxed),extra=panel_movie.load(std::memory_order_relaxed);
               ((is_hud && is_hud(iggy)) || (is_tutorial && is_tutorial(iggy)) || (extra && extra(iggy))) && read_(reinterpret_cast<std::uintptr_t>(matrix),laid.data(),sizeof(laid)))
                {stage_centre_={laid[4]+laid[0]*960.f,laid[5]+laid[3]*540.f};root_={laid[0],laid[4],laid[5]};} // panel children are fitted from this root
        if(const float k=hud_scale.load(std::memory_order_relaxed);!window && k<.999f && matrix && nesting_<=2)
            if(const auto is_hud=hud_movie.load(std::memory_order_relaxed),is_tutorial=tutorial_movie.load(std::memory_order_relaxed);
               ((is_hud && is_hud(iggy)) || (is_tutorial && is_tutorial(iggy))) && read_(reinterpret_cast<std::uintptr_t>(matrix),laid.data(),sizeof(laid))) {
                if(nesting_==1) stage_centre_={laid[4]+laid[0]*960.f,laid[5]+laid[3]*540.f};
                else {
                    const auto qualifies=hud_layout_node.load(std::memory_order_relaxed);const auto tutorial=tutorial_movie.load(std::memory_order_relaxed);
                    if(((qualifies && qualifies(iggy,node)) || (tutorial && tutorial(iggy))) && stage_centre_[0]>0.f && stage_centre_[1]>0.f) {
                        for(unsigned i=0;i<4;++i) laid[i]*=k;
                        laid[4]=stage_centre_[0]+k*(laid[4]-stage_centre_[0]);laid[5]=stage_centre_[1]+k*(laid[5]-stage_centre_[1]);
                        relaid=true;for(const auto value:laid) relaid=relaid && std::isfinite(value);
                        if(relaid) ++hud_relayouts;
                    }
                }
            }
        auto scope=tutorial_child(iggy,node,matrix);if(scope.kind==NativeHudChildKind::none)scope=identify(iggy,node,matrix);
        if(window && scope.kind==NativeHudChildKind::none)scope=takedown_child(iggy,node,matrix);
        if(window) {
            const bool tutorial=scope.kind==NativeHudChildKind::tutorial;
            if(tutorial && scope.matrix_valid && scope.bounds_valid)
                if(const float dx=native_hud_centre_shift(scope.matrix,scope.bounds);dx!=0.f) {laid=scope.matrix;laid[4]+=dx;relaid=true;}
            if((scope.kind==NativeHudChildKind::prompt && !prompt_world.load(std::memory_order_relaxed)) || tutorial) scope.kind=NativeHudChildKind::panel;
            else if(scope.kind==NativeHudChildKind::none && panel_generic.load(std::memory_order_relaxed)) {
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
        // VR-only prompt: an identified prompt subtree that is not being captured is never drawn flat.
        const bool hide=hide_child && scope.kind==NativeHudChildKind::none && start;
        if(hide)++holder_hidden;
        const bool suppress=hide || hide_overlay || ((scope.kind==NativeHudChildKind::prompt || scope.kind==NativeHudChildKind::panel) && !capture && start);
        if(suppress)++suppressed;
        __try {if(!suppress)result=original_(iggy,changed?copied.data():relaid?laid.data():matrix,node,aux,p5,p6,clip,depth);}
        __finally {if(scope.kind!=NativeHudChildKind::none){if(capture)stop(scope);if(watch)watch(scope,false,result);current=previous;}if(entered_master)master_=false;if(holder_entered){holder_level_=holder_saved;holder_eye_=eye_saved;}--nesting_;--depth_;}
        return result;
    }
public:
    static bool publish(NativeHudChildKind kind,const NativeHudArtGeometry& geometry,std::uintptr_t owner,UINT64 epoch,UINT64 tick,NativeInteractionTargetIdentity target={}) {
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
           !wrist_hud_file_owned(read_,base_,geometry.file) || rig::value<std::uintptr_t>(read_,geometry.file+0x1608)!=geometry.iggy || !NativeHudArtProbe::in_tree(read_,geometry.iggy,geometry.display_node))return reject();
        std::lock_guard<std::mutex> lock(mutex_);NativeHudChildScope* free{};
        for(auto& binding:bindings_){if(binding.kind==kind && binding.owner==owner){free=&binding;break;}
            if(!free && (binding.kind==NativeHudChildKind::none || binding.epoch!=epoch || (tick>=binding.tick && tick-binding.tick>250)))free=&binding;}
        if(kind==NativeHudChildKind::health || kind==NativeHudChildKind::mana || kind==NativeHudChildKind::equipment || kind==NativeHudChildKind::objective) {
            bool known=false;for(unsigned i=0;i<widget_count_;++i)known=known || (widget_nodes_[i]==geometry.display_node && widget_iggy_[i]==geometry.iggy);
            if(!known){const unsigned at=widget_next_++%static_cast<unsigned>(widget_nodes_.size());widget_nodes_[at]=geometry.display_node;widget_iggy_[at]=geometry.iggy;widget_count_=std::min<unsigned>(widget_count_+1,static_cast<unsigned>(widget_nodes_.size()));}
        }
        if(!free)return reject();*free={kind,geometry.file,geometry.iggy,geometry.display_node,owner,epoch,tick};free->target=target;++publish_accepts;return true;
    }
    static void clear() {std::lock_guard<std::mutex> lock(mutex_);bindings_={};prompt_roots_={};prompt_target_={};prompt_target_tick_=0;takedown_={};takedown_tick_=0;widget_nodes_={};widget_iggy_={};widget_count_=widget_next_=0;}
    // Producer: the NPC the native takedown sensor qualified this frame (empty = none).
    static void set_takedown(NativeInteractionTargetIdentity target,UINT64 tick) {std::lock_guard<std::mutex> lock(mutex_);takedown_=target;takedown_tick_=target?tick:0;}
    inline static std::atomic<UINT64> takedown_hints{};
    static void clear_kind(NativeHudChildKind kind) {std::lock_guard<std::mutex> lock(mutex_);for(auto& binding:bindings_)if(binding.kind==kind)binding={};if(kind==NativeHudChildKind::prompt){prompt_target_={};prompt_target_tick_=0;}}
    // Stat changes are sparse. Refresh their already-owned child identities
    // only when that same native HUD movie is being drawn again.
    static void refresh_wrist(std::uintptr_t file,std::uintptr_t iggy,UINT64 epoch,UINT64 tick) {
        if(!epoch || !tick || !read_ || !wrist_hud_file_owned(read_,base_,file) || rig::value<std::uintptr_t>(read_,file+0x1608)!=iggy) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& binding:bindings_) if(binding.file==file && binding.iggy==iggy &&
            (binding.kind==NativeHudChildKind::health || binding.kind==NativeHudChildKind::mana || binding.kind==NativeHudChildKind::equipment) &&
            NativeHudArtProbe::in_tree(read_,iggy,binding.node)) {binding.epoch=epoch;binding.tick=tick;}
    }
    // Authorize only the current same target; the exact child Draw renews its
    // observed scope. A movie Draw alone does not prove fresh prompt artwork.
    static void refresh_prompt(std::uintptr_t file,std::uintptr_t iggy,UINT64 epoch,UINT64 tick,NativeInteractionTargetIdentity target) {
        if(!epoch || !tick || !read_ || !wrist_hud_file_owned(read_,base_,file) || rig::value<std::uintptr_t>(read_,file+0x1608)!=iggy)return;
        std::lock_guard<std::mutex> lock(mutex_);
        prompt_target_=target && target.epoch==epoch?target:NativeInteractionTargetIdentity{};prompt_target_tick_=tick;
        for(auto& binding:bindings_) if(binding.kind==NativeHudChildKind::prompt &&
            (binding.file!=file || binding.iggy!=iggy || binding.epoch!=epoch || (binding.target && binding.target!=prompt_target_)))binding={};
    }
    static bool install(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;base_=base;read_=read;
        if(!target_){std::array<unsigned char,prologue.size()> live{};if(!read(base+rva,live.data(),live.size()) || live!=prologue)return false;
            target_=reinterpret_cast<void*>(base+rva);if(MH_CreateHook(target_,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK){target_=nullptr;return false;}}
        const auto status=MH_EnableHook(target_);active_=status==MH_OK || status==MH_ERROR_ENABLED;return active_.load();
    }
    static bool disable() noexcept {
        active_=false;transform=nullptr;begin=nullptr;end=nullptr;observer=nullptr;hud_movie=nullptr;tutorial_movie=nullptr;panel_movie=nullptr;tutorial_epoch=0;hud_layout_node=nullptr;hud_scale=1.f;panel_active=false;panel_generic=false;hud_file=0;eye_node=0;prompt_world=false;
        if(!target_)return true;const auto result=MH_DisableHook(target_);if(result!=MH_OK && result!=MH_ERROR_DISABLED)return false;
        const auto until=GetTickCount64()+2000;while(depth_ && GetTickCount64()<until)Sleep(1);if(depth_)return false;clear();return true;
    }
    // Fixtures execute the exact pass-through hook against an injected original.
    static void fixture(Read read,std::uintptr_t base,Draw original) {read_=read;base_=base;original_=original;active_=true;clear();}
    static std::uint32_t invoke_fixture(void* iggy,float* matrix,void* node,void* aux,void* p5,void* p6,std::uint32_t clip,float depth){return hook(iggy,matrix,node,aux,p5,p6,clip,depth);}
};
} // namespace d2vr
