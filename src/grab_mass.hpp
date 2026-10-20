#pragma once
// F1: mass class table and plausibility gate for grabbed bodies. The engine's inverse-mass half (hknpMotion +0x26) is UNVERIFIED live,
// so the class value is what the mod uses; a read is only trusted when it is plausible AND the tuning enables measured masses.
// mass_verified stays false regardless (it needs a runtime probe to prove the layout).
#include "grab_force_cap.hpp"
#include "native_rigid_body.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
// pelvis, spine, chest, neck, head, l arm (upper, fore, hand), r arm, l leg (thigh, calf, foot), r leg. Sums to 75 kg.
inline constexpr std::array<double,17> ragdoll_class_kg{12,10,18,1.5,4.5, 2.0,1.6,.6, 2.0,1.6,.6, 7,3.4,1.2, 7,3.4,1.2};
inline constexpr double prop_class_kg=12.,body_class_total_kg=75.;
inline const char* ragdoll_bone_name(int bone) {
    static constexpr const char* names[17]={"pelvis","spine","chest","neck","head","l_upper_arm","l_forearm","l_hand","r_upper_arm","r_forearm","r_hand",
        "l_thigh","l_calf","l_foot","r_thigh","r_calf","r_foot"};
    return bone>=0 && bone<17?names[bone]:"?";
}
enum class MassSource {cls,measured};
struct BodyMass {double kg{};MassSource source{MassSource::cls};bool rejected{};};

namespace grab_mass_detail {
inline double decode_inverse(std::uint16_t half) {
    const std::uint32_t bits=static_cast<std::uint32_t>(half)<<16;float f;std::memcpy(&f,&bits,4);return f;
}
// Decoded mass in kg when the read is plausible against the class value (finite inverse > 1e-4, within a factor 12), else nothing.
inline std::optional<double> plausible_kg(std::uint16_t half,double class_kg) {
    const double inverse=decode_inverse(half);
    if(!std::isfinite(inverse) || !(inverse>1e-4)) return {};
    const double kg=1./inverse;
    if(std::isfinite(kg) && kg>=class_kg/12 && kg<=class_kg*12) return kg;
    return {};
}
inline BodyMass gate(std::optional<std::uint16_t> half,double class_kg,bool use_measured) {
    BodyMass out{class_kg,MassSource::cls,false};
    if(!half) return out;
    const auto kg=plausible_kg(*half,class_kg);
    if(!kg) {out.rejected=true;return out;}
    if(use_measured) {out.kg=*kg;out.source=MassSource::measured;}
    return out;
}
}

inline BodyMass ragdoll_body_mass(int bone,std::optional<std::uint16_t> inverse_half,bool use_measured) {
    const double cls=bone>=0 && bone<17?ragdoll_class_kg[static_cast<std::size_t>(bone)]:prop_class_kg;
    return grab_mass_detail::gate(inverse_half,cls,use_measured);
}
// A prop's class value is what its volume suggests (350 kg/m3, as prop_mass does).
inline double prop_class_estimate(const NativeGrabBounds& bounds) {
    const auto e=bounds.maximum-bounds.minimum;
    return std::clamp(std::max(e.x,.02)*std::max(e.y,.02)*std::max(e.z,.02)*350.,.05,2000.);
}
inline BodyMass prop_body_mass(std::optional<std::uint16_t> inverse_half,const NativeGrabBounds& bounds,bool use_measured) {
    return grab_mass_detail::gate(inverse_half,prop_class_estimate(bounds),use_measured);
}
// Mass of the rest of the body that hangs from a lifted bone: grows to lift_fraction of the remainder over the first 0.25 m of lift.
inline double hanging_mass(int bone,double lift_m,const GrabTuning& t) {
    if(bone<0 || bone>=17 || !std::isfinite(lift_m) || !(lift_m>0)) return 0;
    return t.lift_fraction*(body_class_total_kg-ragdoll_class_kg[static_cast<std::size_t>(bone)])*std::clamp(lift_m/.25,0.,1.);
}
}
