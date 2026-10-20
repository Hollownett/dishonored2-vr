#pragma once
// F1: READ-ONLY runtime mass probe. Lists, per ragdoll bone / prop, the raw inverse-mass half the engine holds, its decoded kg and the class kg,
// so the offline class table can be checked against the live layout. It only reads (through the caller's Read); nothing here writes the engine.
#include "grab_mass.hpp"
#include "native_motion.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
struct MassProbeRow {std::uint64_t tick{};std::uint32_t entity_id{},handle{},record_flags{};int bone{-1};
    std::uint16_t raw_inverse_half{};double decoded_kg{},class_kg{};bool rejected{};};
inline std::string mass_probe_csv_header() {return "tick,entity_id,handle,record_flags,bone,bone_name,raw_inverse_half,decoded_kg,class_kg,rejected";}
inline std::string mass_probe_csv_row(const MassProbeRow& r) {
    std::ostringstream o;
    o<<r.tick<<','<<r.entity_id<<','<<r.handle<<','<<r.record_flags<<','<<r.bone<<','<<ragdoll_bone_name(r.bone)<<','<<r.raw_inverse_half<<','
     <<r.decoded_kg<<','<<r.class_kg<<','<<(r.rejected?1:0);
    return o.str();
}
namespace mass_probe_detail {
inline double decoded_kg(std::uint16_t half) {
    const double inverse=grab_mass_detail::decode_inverse(half);
    if(!std::isfinite(inverse) || !(inverse>0)) return 0;
    return 1./inverse;
}
}
template<class Read> std::vector<MassProbeRow> probe_ragdoll_masses(Read& read,std::uintptr_t base,std::uintptr_t entity,std::uint64_t tick) {
    std::vector<MassProbeRow> rows;
    for(int bone=0;bone<17;++bone) {
        const auto body=native_ragdoll_body(read,base,entity,bone);
        if(!body) continue;
        const auto motion=native_motion(read,*body);
        if(!motion) continue;
        MassProbeRow r;r.tick=tick;r.entity_id=body->entity_id;r.handle=body->handle;r.record_flags=body->flags;r.bone=bone;
        r.raw_inverse_half=motion->inverse_mass;r.decoded_kg=mass_probe_detail::decoded_kg(motion->inverse_mass);
        r.class_kg=ragdoll_class_kg[static_cast<std::size_t>(bone)];
        r.rejected=ragdoll_body_mass(bone,motion->inverse_mass,false).rejected;
        rows.push_back(r);
    }
    return rows;
}
template<class Read> std::optional<MassProbeRow> probe_prop_mass(Read& read,const NativeRigidBody& body,std::uint64_t tick) {
    const auto motion=native_motion(read,body);
    if(!motion) return {};
    MassProbeRow r;r.tick=tick;r.entity_id=body.entity_id;r.handle=body.handle;r.record_flags=body.flags;r.bone=body.bone;
    r.raw_inverse_half=motion->inverse_mass;r.decoded_kg=mass_probe_detail::decoded_kg(motion->inverse_mass);
    if(body.bone>=0 && body.bone<17) {r.class_kg=ragdoll_class_kg[static_cast<std::size_t>(body.bone)];r.rejected=ragdoll_body_mass(body.bone,motion->inverse_mass,false).rejected;}
    else {
        const auto m=body.bounds?prop_body_mass(motion->inverse_mass,*body.bounds,false):BodyMass{prop_class_kg,MassSource::cls,false};
        r.class_kg=body.bounds?prop_class_estimate(*body.bounds):prop_class_kg;r.rejected=m.rejected;
        if(!body.bounds) r.rejected=!grab_mass_detail::plausible_kg(motion->inverse_mass,prop_class_kg);
    }
    return r;
}
}
