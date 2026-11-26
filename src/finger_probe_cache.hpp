#pragma once
#include "finger_ik.hpp"
#include <array>
#include <cstdint>
#include <optional>

// G5 (plan Task 12): the free hand's probe answers, per probe slot (the palm cast and one per finger), as the shapes the fingers rest on. A slot's
// shape lives while its probe keeps answering: a hit is live for finger_probe_hit_life_ms after the slot's latest answer, and it stays through one
// missed probe (stickiness, ruling H10: it survives >= 2 probe cycles after the last hit, counted in probe cycles, not frames, so a frame hitch
// does not drop a surface); the second miss in a row drops it (a hand that lifts off is free within two cycles, never a stuck open hand). Bonelab
// disables its finger box on overlap (CheckFingerEnable, study 355); here the probes find the surface instead. Pure; one per hand.
namespace d2vr::rig {
inline constexpr std::uint64_t finger_probe_hit_life_ms=150;
inline constexpr unsigned finger_probe_cache_slots=8,finger_probe_sticky_misses=2;
// The palm slot (0) is probed every frame, a fingertip slot every ~5th (~70 ms at 72 Hz): its misses count only once the last hit is this old (two fingertip cycles).
inline constexpr std::uint64_t finger_palm_sticky_ms=140;
class FingerProbeCache {
public:
    // A probe of this slot answered: its hit as a shape, or nothing (a miss, or a hit that makes no shape).
    void probed(unsigned slot,std::optional<HeldShape> hit,std::uint64_t now,bool surface=false) { // surface (G6): the shape is a world surface slab (not a prop)
        if(slot>=finger_probe_cache_slots) return;
        auto& s=slots_[slot];s.answered=now;
        if(hit) {s.has=true;s.shape=*hit;s.surface=surface;s.misses=0;s.hit_at=now;}
        else if(s.has) {
            if(slot==0) {if(now>=s.hit_at && now-s.hit_at>=finger_palm_sticky_ms) s.has=false;} // time-based: the per-frame palm probe must not drop in 2 frames
            else if(++s.misses>=finger_probe_sticky_misses) s.has=false;
        }
    }
    // The live shapes (in slot order), how many.
    unsigned shapes(std::uint64_t now,std::array<HeldShape,8>& out) const {
        unsigned n=0;
        for(const auto& s:slots_) if(s.has && (now<=s.answered || now-s.answered<=finger_probe_hit_life_ms) && n<out.size()) out[n++]=s.shape;
        return n;
    }
    // G6: shapes() with the live fingertip slots' surface slabs re-placed onto the live palm slab's plane when they nearly agree (normal within 3 degrees,
    // face within 5 mm of the palm face plane): the palm's basis, the slab's own face point projected onto the plane along the normal, its own half, centre
    // .02 (a slab's thickness) behind the new face. Everything else (a prop, no palm surface, other slots, disagreeing slabs) is returned unchanged.
    unsigned shapes_on_palm_plane(std::uint64_t now,std::array<HeldShape,8>& out) const {
        const auto live=[&](const Slot& s) {return s.has && (now<=s.answered || now-s.answered<=finger_probe_hit_life_ms);};
        const Slot& palm=slots_[0];const bool palm_ok=live(palm) && palm.surface;
        const Vec3 n=palm_ok?palm.shape.frame.basis[2]:Vec3{};
        const Vec3 pface=palm_ok?palm.shape.frame.position+n*palm.shape.half.z:Vec3{};
        constexpr double cos3=0.99862953475457383; // cos(3 degrees)
        unsigned count=0;
        for(unsigned i=0;i<slots_.size();++i) {
            const Slot& s=slots_[i];
            if(!live(s) || count>=out.size()) continue;
            HeldShape h=s.shape;
            if(palm_ok && i>0 && s.surface) {
                const Vec3 m=s.shape.frame.basis[2];
                const Vec3 face=s.shape.frame.position+m*s.shape.half.z;
                const double offset=dot(face-pface,n);
                if(dot(m,n)>=cos3 && std::abs(offset)<=.005) {
                    const Vec3 on=face-n*offset;
                    h.frame.basis=palm.shape.frame.basis;h.frame.position=on-n*h.half.z;
                }
            }
            out[count++]=h;
        }
        return count;
    }
    void clear() {slots_={};}
private:
    struct Slot {bool has{};HeldShape shape{};std::uint64_t answered{},hit_at{};unsigned misses{};bool surface{};};
    std::array<Slot,finger_probe_cache_slots> slots_{};
};
// G5: which fingertip (0..4) the k-th of `casts` fingertip casts of probe frame `probe_frame` aims at. No priority finger (priority_bits&0x1f==0): the plain
// round-robin s%5 with s=probe_frame*casts+k. Otherwise even s cycles over the priority fingers (ascending), odd s round-robins all five (a free finger is
// still probed at least every 10 casts).
inline unsigned pick_probe_finger(unsigned priority_bits,std::uint64_t probe_frame,unsigned k,unsigned casts) {
    const std::uint64_t s=probe_frame*casts+k;
    const unsigned bits=priority_bits&0x1fu;
    if(!bits) return static_cast<unsigned>(s%5);
    if(s&1) return static_cast<unsigned>((s/2)%5);
    unsigned pop=0;for(unsigned f=0;f<5;++f) if(bits&(1u<<f)) ++pop;
    unsigned want=static_cast<unsigned>((s/2)%pop);
    for(unsigned f=0;f<5;++f) if(bits&(1u<<f)) {if(want==0) return f;--want;}
    return 0;
}
}
