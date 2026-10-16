#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The cross-section of a prop along its long axis, measured with a few world casts across it (dxgi_probe.cpp profile_step) while it is the
// hand's target, before anything takes it (a held prop collides with nothing, so it cannot be measured then): one radius per station, kept per
// prop. One mechanism for every prop: a bottle's neck is thin and its body fat, a hammer's handle and head differ, a pipe is one radius, a
// ball has none of it (the profile needs a long, slender box). The fingers close on the profile near where the palm touched
// (finger_ik.hpp seat_held), instead of one cylinder for the whole of it. Pure data and arithmetic: no game memory.
namespace d2vr::rig {
inline constexpr unsigned profile_stations=8;
inline constexpr unsigned profile_min_stations=5;   // fewer measured: no profile (the cylinder of the box stands)
inline constexpr double profile_axial_tolerance=.012; // m: a hit this far off its station's plane is another station's, or a moving prop's
inline constexpr double profile_radius_floor=.004,profile_radius_ceiling=.5;
struct RadiusProfile {
    std::array<float,profile_stations> plus{},minus{};  // the radius the cast from the +side / the -side found at each station (m); 0: not measured
    double half{};                                      // the axial half span the stations cover: the prop's half length along its long axis (m)
    unsigned axis{};                                    // which of the model's axes that is
    unsigned measured() const {unsigned n=0;for(unsigned i=0;i<profile_stations;++i) n+=(plus[i]>0 || minus[i]>0)?1u:0u;return n;}
    bool valid() const {return half>0 && measured()>=profile_min_stations;}
    // The radius at station i: both sides' mean, or the one side that hit; 0 when neither did.
    double at(unsigned i) const {
        if(i>=profile_stations) return 0;
        if(plus[i]>0 && minus[i]>0) return (plus[i]+minus[i])*.5;
        return plus[i]>0?plus[i]:minus[i];
    }
};
// The axial coordinate (from the prop's middle toward its +axis end) of station i.
inline double profile_station(unsigned i,double half) {return (-1+(2.*i+1)/profile_stations)*half;}
// A cast's hit: `s` its axial coordinate (from the middle), `side` +1 / -1 the side the cast came from, `radius` how far from the axis it
// stopped. Taken when it lies on a station's plane (a hit from another pose of the prop does not), and the radius is plausible.
inline bool profile_record(RadiusProfile& p,double s,int side,double radius) {
    if(!(p.half>0) || !std::isfinite(s) || !std::isfinite(radius) || radius<profile_radius_floor || radius>profile_radius_ceiling || (side!=1 && side!=-1)) return false;
    const double spacing=2*p.half/profile_stations;
    const int i=static_cast<int>(std::lround((s/spacing)+(profile_stations-1)*.5));
    if(i<0 || i>=static_cast<int>(profile_stations) || std::abs(s-profile_station(static_cast<unsigned>(i),p.half))>profile_axial_tolerance) return false;
    (side>0?p.plus:p.minus)[static_cast<unsigned>(i)]=static_cast<float>(radius);
    return true;
}
// The radius at axial coordinate s: linear between the measured stations, flat past the outer ones. 0 when nothing is measured.
inline double profile_radius(const RadiusProfile& p,double s) {
    double before_s=0,before_r=0,after_s=0,after_r=0;bool have_before=false,have_after=false;
    for(unsigned i=0;i<profile_stations;++i) {
        const double r=p.at(i);if(!(r>0)) continue;
        const double si=profile_station(i,p.half);
        if(si<=s) {before_s=si;before_r=r;have_before=true;}
        else if(!have_after) {after_s=si;after_r=r;have_after=true;}
    }
    if(have_before && have_after) {const double t=(s-before_s)/(after_s-before_s);return before_r+(after_r-before_r)*t;}
    if(have_before) return before_r;
    if(have_after) return after_r;
    return 0;
}
} // namespace d2vr::rig
