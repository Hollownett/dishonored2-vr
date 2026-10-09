#include "../src/avatar_fit_events.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
namespace r=d2vr::rig;
int main() {
    r::AvatarFitEvents log;std::ostringstream csv;r::AvatarFitEvents::header(csv);
    r::AvatarFitEventState state{};state.reference_epoch=7;state.pose_epoch=1;
    assert(log.change(csv,state,"initial"));
    state.tick=100;state.present=2;assert(!log.change(csv,state,"same_frame_state"));assert(log.rows()==1);
    state.accepted=state.active=state.gate=true;state.accepted_epoch=7;state.accepts=1;
    state.runtime_status=static_cast<unsigned>(d2vr::AvatarFitRuntimeStatus::active);assert(log.change(csv,state,"accepted"));
    state.gate=state.active=false;state.suspensions=1;state.pending=true;
    state.runtime_status=static_cast<unsigned>(d2vr::AvatarFitRuntimeStatus::verifying);assert(log.change(csv,state,"suspend"));
    state.owners[0].body=0x10000;state.owners[0].body_identity.generation=2;state.owners[0].model=0x20000;
    assert(log.change(csv,state,"owner_replaced"));
    state.resource_sources[0]=0x30000;state.resource_primaries[0]=0x40000;state.surface_counts[0]=5;
    assert(log.change(csv,state,"template_verified"));
    state.gate=state.active=true;state.pending=false;state.reuses=1;
    state.runtime_status=static_cast<unsigned>(d2vr::AvatarFitRuntimeStatus::active);assert(log.change(csv,state,"fit_reused"));
    state.reference_epoch=8;state.accepted=state.active=state.gate=false;state.invalidations=1;
    state.runtime_status=static_cast<unsigned>(d2vr::AvatarFitRuntimeStatus::recalibration_required);assert(log.change(csv,state,"reference_changed"));
    assert(csv.str().find("owner_replaced")<csv.str().find("fit_reused"));
    const auto before=log.rows();for(unsigned i=0;i<300;++i){state.pose_epoch=10+i;log.change(csv,state,"change");}
    assert(log.rows()==r::AvatarFitEvents::limit && log.dropped()==before+300-r::AvatarFitEvents::limit);
    const auto dropped=log.dropped();state.tick++;assert(!log.change(csv,state,"unchanged_after_cap") && log.dropped()==dropped);
    r::AvatarMeshToken mesh{0x10000,0x20000,{{0x30000,1,100,50}}};const auto hash=r::avatar_surface_diagnostic_id(mesh);
    mesh.surfaces[0].triangles++;assert(hash!=r::avatar_surface_diagnostic_id(mesh));
    log.clear();assert(log.rows()==0 && log.dropped()==0);assert(log.change(csv,state,"reset"));
    std::cout<<"PASS: bounded chronological fit changes, owner/resource/gate/reference transitions and stable-frame de-duplication\n";
}
