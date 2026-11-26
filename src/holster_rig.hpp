#pragma once
// Body holster places (pure policy, no engine code): shoulders, hips, belt pouches, a Heart chest pocket. They follow the body
// (pelvis / chest frames, head fallback). Put-back rule for a released item, and the squeeze-and-pull draw of a remembered item.
// Design: docs/superpowers/specs/2026-11-26-arm-weapon-collisions-holsters-design.md section 7.
#include "tracked_pose.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>

namespace d2vr {
enum class HolsterPlace : unsigned char {none,shoulder_left,shoulder_right,hip_left,hip_right,pouch_left,pouch_right,heart_pocket,count};
enum class HeldKind : unsigned char {empty,sword,gun,gadget,heart,other};
// forward / left are level unit vectors; up is +Z.
struct BodyFrames {
    Vec3 pelvis{},pelvis_forward{},pelvis_left{};bool pelvis_valid{};
    Vec3 chest{},chest_forward{},chest_left{};bool chest_valid{};
    Vec3 head{},head_forward{};bool head_valid{};
};
// frame 0 pelvis, 1 chest; offset = (forward, side, up); mirrored: side points to the place's own side, else to the body's left.
struct PlaceSpec {unsigned frame{};Vec3 offset{};double radius{},exit{};bool mirrored{};};
namespace holster_detail {
inline std::array<PlaceSpec,static_cast<std::size_t>(HolsterPlace::count)> default_places() {
    std::array<PlaceSpec,static_cast<std::size_t>(HolsterPlace::count)> p{};
    const PlaceSpec shoulder{1,{-.02,.17,.17},.16,.03,true},hip{0,{0,.21,-.05},.17,0,true},pouch{0,{.14,.09,0},.13,.03,true},
        pocket{1,{.12,.07,-.02},.13,.03,false};
    p[static_cast<std::size_t>(HolsterPlace::shoulder_left)]=shoulder;p[static_cast<std::size_t>(HolsterPlace::shoulder_right)]=shoulder;
    p[static_cast<std::size_t>(HolsterPlace::hip_left)]=hip;p[static_cast<std::size_t>(HolsterPlace::hip_right)]=hip;
    p[static_cast<std::size_t>(HolsterPlace::pouch_left)]=pouch;p[static_cast<std::size_t>(HolsterPlace::pouch_right)]=pouch;
    p[static_cast<std::size_t>(HolsterPlace::heart_pocket)]=pocket;
    return p;
}
}
struct HolsterConfig {
    std::array<PlaceSpec,static_cast<std::size_t>(HolsterPlace::count)> places=holster_detail::default_places();
    double pull_commit=.07,pull_seconds=1.2,cooldown=.6,stow_max_speed=1.0;
};
// Lines `name fwd side up radius` (names: shoulder hip pouch heart_pocket; both sides of a pair), `#` comments, blank lines. Any bad line -> nullopt.
inline std::optional<HolsterConfig> parse_holster_places(std::string_view text,const HolsterConfig& base) noexcept {
    HolsterConfig cfg=base;
    const auto idx=[](HolsterPlace p){return static_cast<std::size_t>(p);};
    while(!text.empty()) {
        const auto nl=text.find('\n');
        std::string_view line=text.substr(0,nl);
        text=nl==std::string_view::npos?std::string_view{}:text.substr(nl+1);
        if(const auto h=line.find('#');h!=std::string_view::npos) line=line.substr(0,h);
        std::array<std::string_view,6> tok{};std::size_t n=0;
        for(std::size_t i=0;i<line.size();) {
            const auto ws=[](char c){return c==' ' || c=='\t' || c=='\r';};
            if(ws(line[i])) {++i;continue;}
            std::size_t j=i;while(j<line.size() && !ws(line[j])) ++j;
            if(n==tok.size()) return std::nullopt;
            tok[n++]=line.substr(i,j-i);i=j;
        }
        if(n==0) continue;
        if(n!=5) return std::nullopt;
        double v[4]{};
        for(int k=0;k<4;++k) {
            const auto t=tok[static_cast<std::size_t>(k)+1];
            const auto r=std::from_chars(t.data(),t.data()+t.size(),v[k]);
            if(r.ec!=std::errc{} || r.ptr!=t.data()+t.size() || !std::isfinite(v[k])) return std::nullopt;
        }
        HolsterPlace a=HolsterPlace::none,b=HolsterPlace::none;
        if(tok[0]=="shoulder") {a=HolsterPlace::shoulder_left;b=HolsterPlace::shoulder_right;}
        else if(tok[0]=="hip") {a=HolsterPlace::hip_left;b=HolsterPlace::hip_right;}
        else if(tok[0]=="pouch") {a=HolsterPlace::pouch_left;b=HolsterPlace::pouch_right;}
        else if(tok[0]=="heart_pocket") a=b=HolsterPlace::heart_pocket;
        else return std::nullopt;
        for(const auto p:{a,b}) {auto& s=cfg.places[idx(p)];s.offset={v[0],v[1],v[2]};s.radius=v[3];}
    }
    return cfg;
}

class HolsterRig {
public:
    explicit HolsterRig(HolsterConfig cfg={}) : cfg_(cfg) {}
    // Place centre in world metres; nullopt when the place is disabled (radius <= 0) or has no frame (shoulders have no head fallback).
    std::optional<Vec3> centre(HolsterPlace p,const BodyFrames& f) const {
        if(p==HolsterPlace::none || p>=HolsterPlace::count) return std::nullopt;
        const PlaceSpec& s=cfg_.places[static_cast<std::size_t>(p)];
        if(!(s.radius>0)) return std::nullopt;
        const bool left_side=p==HolsterPlace::shoulder_left || p==HolsterPlace::hip_left || p==HolsterPlace::pouch_left;
        const double side=s.mirrored && !left_side?-1.:1.;
        const Vec3 up{0,0,1};
        if(s.frame==0 && f.pelvis_valid) return f.pelvis+f.pelvis_forward*s.offset.x+f.pelvis_left*(side*s.offset.y)+up*s.offset.z;
        if(s.frame==1 && f.chest_valid) return f.chest+f.chest_forward*s.offset.x+f.chest_left*(side*s.offset.y)+up*s.offset.z;
        if(!f.head_valid) return std::nullopt;
        double fwd=0,sd=0,z=0;
        switch(p) {
            case HolsterPlace::hip_left: case HolsterPlace::hip_right: fwd=0;sd=.20;z=-.62;break;
            case HolsterPlace::pouch_left: case HolsterPlace::pouch_right: fwd=.14;sd=.09;z=-.62;break;
            case HolsterPlace::heart_pocket: fwd=.12;sd=.07;z=-.32;break;
            default: return std::nullopt;
        }
        const Vec3 left=cross(up,f.head_forward);
        return f.head+f.head_forward*fwd+left*(side*sd)+up*z;
    }
    struct InPlace {HolsterPlace place{};double distance{};};
    // Nearest place relevant to what the hand holds; the place the hand was in at the last call keeps its `exit` extra radius.
    InPlace classify(unsigned hand,Vec3 hand_pos,HeldKind held,const BodyFrames& f) {
        InPlace best{};
        if(hand>1) return best;
        for(unsigned i=1;i<static_cast<unsigned>(HolsterPlace::count);++i) {
            const auto p=static_cast<HolsterPlace>(i);
            if(!relevant(held,p)) continue;
            const auto c=centre(p,f);if(!c) continue;
            const double d=length(hand_pos-*c);
            const PlaceSpec& s=cfg_.places[i];
            if(!(d<s.radius+(last_[hand]==p?s.exit:0.))) continue;
            if(best.place==HolsterPlace::none || d<best.distance) best={p,d};
        }
        last_[hand]=best.place;
        return best;
    }
    // Released item at a place: does the inventory take it back?
    bool put_back(HeldKind held,HolsterPlace place,bool armed,double speed_mps) const {
        if(place==HolsterPlace::none || place>=HolsterPlace::count) return false;
        if(held==HeldKind::gadget) return !armed && speed_mps<cfg_.stow_max_speed;
        return held==HeldKind::heart;
    }
    struct DrawIn {float grip{};bool empty_hand{},suspended{},have_item{};Vec3 hand{};double now{};HolsterPlace place{};};
    struct DrawOut {bool armed{},committed{},cancelled{},empty{};HolsterPlace place{};};
    DrawOut draw(unsigned hand,const DrawIn& in) {
        DrawOut out{};
        if(hand>1) return out;
        Draw& d=draw_[hand];
        const bool pressed=in.grip>=(d.down?.4f:.6f),edge=pressed && !d.down;d.down=pressed;
        if(d.armed) {
            out.place=d.place;
            if(in.suspended || !pressed) {d.armed=false;out.cancelled=true;}
            else if(length(in.hand-d.origin)>=cfg_.pull_commit && in.now-d.t0<=cfg_.pull_seconds) {
                d.armed=false;out.committed=true;cool_[hand]=in.now+cfg_.cooldown;}
            else if(in.now-d.t0>cfg_.pull_seconds) {d.armed=false;out.cancelled=true;}
            return out;
        }
        const bool draw_place=in.place==HolsterPlace::pouch_left || in.place==HolsterPlace::pouch_right || in.place==HolsterPlace::heart_pocket;
        if(edge && draw_place && in.empty_hand && !in.suspended && !cooling(hand,in.now)) {
            out.place=in.place;
            if(!in.have_item) {out.empty=true;return out;}
            d.armed=true;d.origin=in.hand;d.t0=in.now;d.place=in.place;out.armed=true;
        }
        return out;
    }
    // The caller's put-aways (and any other action) start the hand's cooldown; draw() starts it itself on a commit.
    void note_action(unsigned hand,double now) {if(hand<2) cool_[hand]=now+cfg_.cooldown;}
    bool cooling(unsigned hand,double now) const {return hand<2 && now<cool_[hand];}
    void reset() {last_={};draw_={};cool_={};}
private:
    struct Draw {bool down{},armed{};Vec3 origin{};double t0{};HolsterPlace place{};};
    static bool relevant(HeldKind held,HolsterPlace p) {
        const bool shoulder=p==HolsterPlace::shoulder_left || p==HolsterPlace::shoulder_right;
        const bool hip=p==HolsterPlace::hip_left || p==HolsterPlace::hip_right;
        switch(held) {
            case HeldKind::sword: return shoulder;
            case HeldKind::gun: return hip;
            case HeldKind::gadget: case HeldKind::heart: return true;
            case HeldKind::empty: return !shoulder && !hip;
            default: return false;
        }
    }
    HolsterConfig cfg_;
    std::array<HolsterPlace,2> last_{};
    std::array<Draw,2> draw_{};
    std::array<double,2> cool_{};
};
}
