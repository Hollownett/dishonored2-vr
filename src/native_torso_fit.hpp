#pragma once
#include <optional>
#include <string>
#include "body_renderer.hpp"
#include "avatar_fit_reuse.hpp"

namespace d2vr {
// Pinned native meshes, decoded eight-weight vertices in the bind chest band
// Z=1.35 +/- .0125m, fully weighted to spine01/02/03. These are clothed
// cross-sections, not shoulder spans or a measurement of the player's body.
// Emily SHA256 5a8bbefd7b9124694c59d1f58cacc3c207b15ffdb30c00decb5e9c2ea18a9051
// Corvo SHA256 0cfc0f83868fdc9f224b741cf70b4e723821c3cec17b331eef206acac8805ea3
inline std::optional<double> native_torso_width(const std::string& resource,unsigned vertices) {
    if(vertices==7951 && (resource=="models/characters/player/emily/body.md6" ||
        resource=="models/characters/player/emily/player_body_emily.md6mesh" ||
        resource=="generated/basemodel/models/characters/player/emily/player_body_emily.bmd6model")) return .25142084062099457;
    if(vertices==10391 && (resource=="models/characters/player/corvo/body.md6" ||
        resource=="models/characters/player/corvo/player_body_corvo.md6mesh" ||
        resource=="generated/basemodel/models/characters/player/corvo/player_body_corvo.bmd6model")) return .353106752038002;
    return {};
}
struct NativeTorsoFitToken {
    rig::Arm owner;
    rig::NativeBodyBind body;
    std::string resource;
    rig::AvatarMeshToken mesh;
    double width_metres{};
    std::array<float,3> model_scale{};
};
inline bool operator==(const NativeTorsoFitToken& a,const NativeTorsoFitToken& b) {
    return a.owner==b.owner && a.body==b.body && a.resource==b.resource && a.mesh==b.mesh && a.width_metres==b.width_metres && a.model_scale==b.model_scale;
}
// Fixed-slot callback guard; complete names/inverse/geometry inventory belongs
// to native_torso_fit_token's bounded refresh lane.
// Started identity = {generation, state flags}. Flags change with player states (crouch, mantle,
// combat), the generation only when the object is recreated; compare the generation alone.
inline bool same_generation(const std::optional<rig::Identity>& a,const rig::Identity& b) noexcept {return a && a->generation==b.generation;}
template<class Read> bool native_torso_fit_current(Read& read,std::uintptr_t base,const NativeTorsoFitToken& token) {
    const auto& arm=token.owner;const auto& b=token.body;
    const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !rig::typed(read,base,*game,0x1c940a8) || rig::value<std::uintptr_t>(read,*game+0x3130b0)!=arm.player ||
       !rig::typed(read,base,arm.player,0x1c5e428) || !rig::typed(read,base,arm.body,0x1c5a8d8,0x1c5a9b0) ||
       rig::value<std::uintptr_t>(read,arm.player+0xf0)!=b.model || rig::value<std::uintptr_t>(read,arm.body+0x58)!=b.animation ||
       !same_generation(rig::value<rig::Identity>(read,arm.body+0x18),arm.body_identity) || rig::value<std::uintptr_t>(read,arm.body+0x10)!=arm.player ||
       !rig::typed(read,base,b.model,0x1b0d628) || !rig::typed(read,base,b.animation,0x1b80408,0x1b804f0) ||
       !same_generation(rig::started(read,b.animation),b.animation_identity) || rig::value<std::uintptr_t>(read,b.animation+0x38)!=b.model ||
       rig::value<std::uintptr_t>(read,b.model+0x1fe8)!=b.descriptor || rig::value<std::uintptr_t>(read,b.descriptor+0x50)!=b.skeleton ||
       !rig::typed(read,base,b.skeleton,0x1b0bd08) || rig::value<std::uint16_t>(read,b.skeleton+0x78)!=20 ||
       rig::value<std::uintptr_t>(read,b.skeleton+0x70)!=b.names || rig::value<std::uintptr_t>(read,b.skeleton+0x120)!=b.inverse ||
       rig::value<std::uintptr_t>(read,b.descriptor+8)!=token.mesh.source || rig::value<std::uintptr_t>(read,b.model+0x1bb0)!=token.mesh.source ||
       rig::value<std::array<float,3>>(read,b.model+0x1890)!=token.model_scale) return false;
    return rig::value<std::uintptr_t>(read,base+0x39a5290)==game && rig::value<std::uintptr_t>(read,*game+0x3130b0)==arm.player &&
        rig::value<std::uintptr_t>(read,arm.player+0xf0)==b.model && rig::value<std::uintptr_t>(read,arm.body+0x58)==b.animation &&
        same_generation(rig::value<rig::Identity>(read,arm.body+0x18),arm.body_identity) && same_generation(rig::started(read,b.animation),b.animation_identity) &&
        rig::value<std::uintptr_t>(read,b.model+0x1fe8)==b.descriptor;
}
// Bounded refresh lane only. Both shipped body.md6 wrappers name the shared
// Emily skeleton and their character-specific mesh; no skeleton-name guess.
// Wrapper hashes: Emily 71a340e0...02bd; Corvo 304421c8...a811 (private asset test).
template<class Read> std::optional<NativeTorsoFitToken> native_torso_fit_token(Read& read,std::uintptr_t base,const rig::Arm& owned_arm) {
    const auto arms=rig::player_arms(read,base);
    if(!arms || owned_arm.side>1 || !((*arms)[owned_arm.side]==owned_arm)) return {};
    const auto body=rig::native_body_bind(read,base,owned_arm);if(!body) return {};
    const auto source=rig::value<std::uintptr_t>(read,body->descriptor+8);
    const auto resource=source?rig::body_resource_name(read,*source):std::nullopt;if(!resource) return {};
    if(*resource!="models/characters/player/emily/body.md6" && *resource!="models/characters/player/corvo/body.md6") return {};
    auto model=owned_arm;model.model=body->model;model.skeleton=body->skeleton;
    const auto mesh=rig::avatar_mesh_token(read,base,model);
    if(!mesh || mesh->source!=*source || mesh->surfaces.size()!=1 || mesh->surfaces[0].kind!=0) return {};
    const auto& surface=mesh->surfaces[0];
    const auto native_width=native_torso_width(*resource,surface.vertices);
    if(!native_width || surface.triangles!=(*resource=="models/characters/player/emily/body.md6"?13504u:18259u)) return {};
    const auto scale=rig::value<std::array<float,3>>(read,body->model+0x1890);
    if(!scale || !std::isfinite((*scale)[0]) || (*scale)[0]<1e-4 || (*scale)[0]>16 ||
       (*scale)[1]!=(*scale)[0] || (*scale)[2]!=(*scale)[0]) return {};
    const auto after=rig::player_arms(read,base);const auto body_after=rig::native_body_bind(read,base,owned_arm);
    const auto mesh_after=rig::avatar_mesh_token(read,base,model);
    if(!after || *after!=*arms || !body_after || !(*body_after==*body) || !mesh_after || *mesh_after!=*mesh ||
       rig::value<std::uintptr_t>(read,body->descriptor+8)!=source || rig::body_resource_name(read,*source)!=resource) return {};
    NativeTorsoFitToken result{owned_arm,*body,*resource,*mesh,*native_width*(*scale)[0],*scale};
    if(!native_torso_fit_current(read,base,result)) return {};
    return result;
}
} // namespace d2vr
