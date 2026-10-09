#pragma once
#include "avatar_fit_reuse.hpp"
#include <ostream>
#include <tuple>

namespace d2vr::rig {
struct AvatarFitEventState {
    std::uint64_t tick{},present{},reference_epoch{},pose_epoch{},accepted_epoch{};
    unsigned logic_mask{},runtime_status{},wizard_state{};
    bool gate{},pending{},accepted{},active{},template_verified{};
    std::array<Arm,2> owners{};
    std::array<std::uintptr_t,2> resource_sources{},resource_primaries{};
    std::array<std::uint64_t,2> surface_identity{};
    std::array<unsigned,2> surface_counts{};
    std::uint64_t accepts{},reuses{},invalidations{},suspensions{};
    double native_palm_span{},native_shoulder_span{},arm_scale{},shoulder_drop{};
};
inline bool same_avatar_fit_event(const AvatarFitEventState& a,const AvatarFitEventState& b) {
    return std::tie(a.reference_epoch,a.pose_epoch,a.accepted_epoch,a.logic_mask,a.runtime_status,a.wizard_state,a.gate,a.pending,a.accepted,a.active,a.template_verified,
        a.owners,a.resource_sources,a.resource_primaries,a.surface_identity,a.surface_counts,a.accepts,a.reuses,a.invalidations,a.suspensions,a.native_palm_span,a.native_shoulder_span,a.arm_scale,a.shoulder_drop)==
        std::tie(b.reference_epoch,b.pose_epoch,b.accepted_epoch,b.logic_mask,b.runtime_status,b.wizard_state,b.gate,b.pending,b.accepted,b.active,b.template_verified,
        b.owners,b.resource_sources,b.resource_primaries,b.surface_identity,b.surface_counts,b.accepts,b.reuses,b.invalidations,b.suspensions,b.native_palm_span,b.native_shoulder_span,b.arm_scale,b.shoulder_drop);
}
inline std::uint64_t avatar_surface_diagnostic_id(const AvatarMeshToken& mesh) {
    std::uint64_t result=14695981039346656037ull;
    const auto add=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){result^=(value>>(i*8))&255;result*=1099511628211ull;}};
    add(mesh.source);add(mesh.primary);
    for(const auto& surface:mesh.surfaces){add(surface.resource);add(surface.kind);add(surface.vertices);add(surface.triangles);}
    return result;
}
class AvatarFitEvents {
    std::optional<AvatarFitEventState> last_;unsigned rows_{},dropped_{};
public:
    static constexpr unsigned limit=256;
    void clear(){last_.reset();rows_=dropped_=0;}
    unsigned rows() const{return rows_;}unsigned dropped()const{return dropped_;}
    static void header(std::ostream& out) {
        out<<"event,tick,present,reason,reference_epoch,pose_epoch,accepted_epoch,logic_mask,runtime_status,wizard_state,pose_gate,resource_pending,accepted,active,template_verified,accepts,reuses,invalidations,suspensions,native_palm_span,native_shoulder_span,arm_scale,eye_to_shoulder,resource_check_bound_ms";
        for(const auto side:{"left","right"})out<<','<<side<<"_player,"<<side<<"_body,"<<side<<"_body_generation,"<<side<<"_body_flags,"<<side<<"_animation,"<<side<<"_animation_generation,"<<side<<"_animation_flags,"<<side<<"_model,"<<side<<"_skeleton,"<<side<<"_resource_source,"<<side<<"_resource_primary,"<<side<<"_surface_identity,"<<side<<"_surface_count";
        out<<'\n';
    }
    bool change(std::ostream& out,const AvatarFitEventState& state,const char* reason) {
        if(last_ && same_avatar_fit_event(*last_,state))return false;
        last_=state;
        if(rows_>=limit){++dropped_;return false;}
        out<<++rows_<<','<<state.tick<<','<<state.present<<','<<reason<<','<<state.reference_epoch<<','<<state.pose_epoch<<','<<state.accepted_epoch<<','<<state.logic_mask<<','<<state.runtime_status<<','<<state.wizard_state<<','<<state.gate<<','<<state.pending<<','<<state.accepted<<','<<state.active<<','<<state.template_verified<<','<<state.accepts<<','<<state.reuses<<','<<state.invalidations<<','<<state.suspensions<<','<<state.native_palm_span<<','<<state.native_shoulder_span<<','<<state.arm_scale<<','<<state.shoulder_drop<<",500";
        for(unsigned side=0;side<2;++side){const auto& arm=state.owners[side];out<<','<<arm.player<<','<<arm.body<<','<<arm.body_identity.generation<<','<<arm.body_identity.flags<<','<<arm.animation<<','<<arm.animation_identity.generation<<','<<arm.animation_identity.flags<<','<<arm.model<<','<<arm.skeleton<<','<<state.resource_sources[side]<<','<<state.resource_primaries[side]<<','<<state.surface_identity[side]<<','<<state.surface_counts[side];}
        out<<'\n';out.flush();return true;
    }
};
} // namespace d2vr::rig
