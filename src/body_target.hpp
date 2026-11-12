#pragma once
// v1250 (user, headset v1246: "body UI (carry and loot) should be rendered when user points arm at a body, now it is not always rendered" and
// "even when arm is near body the UI prompt was unstable"). The v1246 session: 12 of 13 body prompt hides were not_targeted, and the blink log
// shows ~1200 one-to-five-frame gaps before a body prompt came back: the target was a per-hand, per-part, per-cache-tick thing (the routed
// hand's live Use target <= 250 ms, else one hand's grab focus part <= 150 ms, and any other live Use target next to the body cancelled it).
// Here the target is ONE body entity, entered by either hand's palm near any of its ragdoll parts or by a hand pointing at it, and held until
// both hands have left a wider gate and stopped pointing at it for hold_ms. Pure (no engine reads): dxgi_probe.cpp feeds the observations.
#include "arm_ik.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace d2vr::body_target {
// Palm near: the grip (controller) point within enter_palm_m of a part's surface enters, leave_palm_m (1.6x) keeps it.
// Pointing: the hand's aim ray (the controller's pointing pose, the same pose the game's Use query is routed along) passes within
// point_tolerance_m of a part's surface, the part's closest approach at most reach_m along the ray (a body on the floor 2.5 m ahead of a
// standing player is ~2.9 m from the hand), and a world LOS cast to that part is clear; it keeps with the wider tolerance and reach while the
// LOS is not seen blocked. Release: hold_ms after the last frame any hand kept it. A different body takes over only after it ranked better
// for takeover_ms. The prompt stands prompt_up_m above the body origin's height over the part, eased with anchor_tau_s.
inline constexpr double enter_palm_m=.25,leave_palm_m=.40,point_tolerance_m=.15,point_leave_tolerance_m=.25,reach_m=3.0,reach_leave_m=3.3;
inline constexpr double prompt_up_m=.35,anchor_tau_s=.12,palm_margin_m=.05,point_margin_m=.20;
inline constexpr std::uint64_t hold_ms=350,takeover_ms=150,los_ttl_ms=300;
inline constexpr double none=std::numeric_limits<double>::infinity();

struct Part {Vec3 at{};double radius{};};
struct RayFit {bool ok{};double along{none},gap{none};int part{-1};};
// The part whose surface the ray passes nearest (closest approach in front of the origin, at most reach along it).
inline RayFit ray_fit(Vec3 origin,Vec3 direction,const Part* parts,std::size_t n,double reach) noexcept {
    RayFit best{};const double l=length(direction);
    if(!(l>1e-6) || !finite(origin) || !finite(direction)) return best;
    direction=direction*(1/l);
    for(std::size_t i=0;i<n;++i) {
        if(!finite(parts[i].at)) continue;
        const Vec3 d=parts[i].at-origin;const double t=dot(d,direction);
        if(!(t>0) || t>reach) continue;
        const double gap=length(d-direction*t)-parts[i].radius;
        if(!best.ok || gap<best.gap) best={true,t,gap,static_cast<int>(i)};
    }
    return best;
}
// The palm point's distance to the nearest part surface (negative inside).
inline double palm_gap(Vec3 palm,const Part* parts,std::size_t n,int* part=nullptr) noexcept {
    double best=none;
    if(!finite(palm)) return best;
    for(std::size_t i=0;i<n;++i) {
        if(!finite(parts[i].at)) continue;
        const double g=length(parts[i].at-palm)-parts[i].radius;
        if(g<best) {best=g;if(part) *part=static_cast<int>(i);}
    }
    return best;
}

enum class Source : unsigned {none,palm,point,count};
enum class Los : int {blocked=-1,unknown=0,clear=1};
// One body this frame. palm/point fields are the best over both hands; anchor = the prompt point for that body (caller: part x/z, origin
// height + prompt_up_m) when anchor_valid.
struct Obs {
    std::uintptr_t body{};
    double palm{none};int palm_side{-1},palm_part{-1};
    double point_gap{none},along{none};int point_side{-1},point_part{-1};
    Los los{Los::unknown};
    Vec3 anchor{};bool anchor_valid{};
};
inline bool enters_palm(const Obs& o) noexcept {return o.palm<=enter_palm_m;}
inline bool enters_point(const Obs& o) noexcept {return o.point_gap<=point_tolerance_m && o.along<=reach_m && o.los==Los::clear;}
inline bool keeps_palm(const Obs& o) noexcept {return o.palm<=leave_palm_m;}
inline bool keeps_point(const Obs& o) noexcept {return o.point_gap<=point_leave_tolerance_m && o.along<=reach_leave_m && o.los!=Los::blocked;}
inline bool keeps(const Obs& o) noexcept {return keeps_palm(o) || keeps_point(o);}
// Palm-near ranks before any pointing (palm gap 0..; pointing 10 + distance along the ray: the nearest body along the ray wins).
inline double rank(const Obs& o) noexcept {return enters_palm(o)?o.palm:enters_point(o)?10+o.along:none;}

// Neighbouring objects never steal the body's kept prompt unless the game itself draws THEIR prompt now (the art it draws for a downed body,
// this one or another, is no reason to hide this body's).
inline bool other_prompt_drawn(bool drawn_now,std::uintptr_t drawn_owner,bool drawn_owner_downed,std::uintptr_t target,bool same_element) noexcept {
    if(!drawn_now) return false;
    if(drawn_owner) return drawn_owner!=target && !drawn_owner_downed;
    return !same_element;   // untagged art: as before (another element than the kept one)
}

enum class Drop : unsigned {gone,away,within_leave,count};
class Tracker {
public:
    std::uintptr_t body{};Source source{};int side{-1},part{-1};
    Vec3 anchor{};bool anchored{};
    // counters
    std::uint64_t acquired[static_cast<unsigned>(Source::count)]{},time_ms[static_cast<unsigned>(Source::count)]{},drops[static_cast<unsigned>(Drop::count)]{};
    std::uint64_t takeovers{},part_switches{},hand_switches{};
    void step(std::uint64_t now,const Obs* obs,std::size_t n) noexcept {
        const std::uint64_t dt=last_step_ && now>=last_step_?now-last_step_:0;last_step_=now;
        if(body) time_ms[static_cast<unsigned>(source)]+=dt;
        const Obs* cur=nullptr;const Obs* best=nullptr;double best_rank=none;
        for(std::size_t i=0;i<n;++i) {
            if(obs[i].body==body && body) cur=&obs[i];
            const double r=rank(obs[i]);
            if(r<best_rank) {best_rank=r;best=&obs[i];}
        }
        if(body) {
            if(cur && keeps(*cur)) last_in_=now;
            if(now<last_in_ || now-last_in_>hold_ms) {
                ++drops[static_cast<unsigned>(!cur?Drop::gone:keeps_palm(*cur)?Drop::within_leave:Drop::away)];
                body=0;source=Source::none;side=part=-1;anchored=false;cur=nullptr;challenger_=0;
            }
        }
        if(body && best && best->body!=body) {
            const double cr=cur?rank(*cur):none;const double margin=best_rank<10 && cr<10?palm_margin_m:point_margin_m;
            if(best_rank+margin<cr || cr==none) {
                if(challenger_!=best->body) {challenger_=best->body;challenger_since_=now;}
                if(now-challenger_since_>=takeover_ms) {take(*best,now);cur=best;++takeovers;}
            } else challenger_=0;
        } else challenger_=0;
        if(!body && best) {take(*best,now);cur=best;}
        if(!body || !cur) return;
        // the current body's source, hand and part (counted when they change; the target does not)
        Source s=source;int h=side,p=part;
        if(keeps_palm(*cur)) {s=Source::palm;h=cur->palm_side;p=cur->palm_part;}
        else if(keeps_point(*cur)) {s=Source::point;h=cur->point_side;p=cur->point_part;}
        if(p!=part && part>=0 && p>=0) ++part_switches;
        if(h!=side && side>=0 && h>=0) ++hand_switches;
        source=s;side=h;part=p;
        if(cur->anchor_valid && finite(cur->anchor)) {
            if(!anchored) {anchor=cur->anchor;anchored=true;}
            else {const double a=1-std::exp(-static_cast<double>(dt)/1000./anchor_tau_s);anchor=anchor+(cur->anchor-anchor)*a;}
        }
    }
private:
    std::uint64_t last_in_{},last_step_{},challenger_since_{};std::uintptr_t challenger_{};
    void take(const Obs& o,std::uint64_t now) noexcept {
        body=o.body;last_in_=now;challenger_=0;anchored=false;
        source=enters_palm(o)?Source::palm:Source::point;side=enters_palm(o)?o.palm_side:o.point_side;part=enters_palm(o)?o.palm_part:o.point_part;
        ++acquired[static_cast<unsigned>(source)];
    }
};
} // namespace d2vr::body_target
