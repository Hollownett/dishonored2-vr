#pragma once
#include "native_door_event_admission.hpp"
#include "native_door_owner.hpp"
#include <cstdint>
#include <optional>

namespace d2vr {
class NativeDoorRequest {
public:
    struct Tokens {
        std::uint64_t ownership{},world{},player{},part{},component{};
    };
    using Request=NativeDoorEventAdmission::Request;
    static std::optional<Request> make(const NativeDoorOwner::Snapshot& owner,const Tokens& tokens,bool opening_ccw) noexcept {
        if(!tokens.ownership || !tokens.world || !tokens.player || !tokens.part || !tokens.component ||
           !valid_owner(owner))return {};
        if(owner.game>0x0000800000000000ull-0x1e8968)return {};
        const auto manager=owner.game+0x1e8968;
        if(!rig::pointer(manager))return {};
        Request request{};
        auto& operation=request.operation;
        operation.ownership=tokens.ownership;
        operation.world=tokens.world;
        operation.player_generation=tokens.player;
        operation.part_generation=tokens.part;
        operation.player=owner.player;
        operation.part=owner.part;
        operation.animation=owner.animation;
        operation.container=owner.container;
        operation.binding=owner.binding;
        operation.desired_seconds=0.f;
        operation.opening_clip=owner.clips[opening_ccw?1:0];
        operation.closing_clip=owner.clips[opening_ccw?2:3];
        operation.close_authorized=false;
        operation.linked=false;
        request.game=owner.game;
        request.manager=manager;
        request.component=owner.component;
        request.door=owner.door;
        request.component_generation=tokens.component;
        request.door_spawn=owner.door_spawn;
        request.player_spawn=owner.player_spawn;
        request.part_spawn=owner.part_spawn;
        return request;
    }
private:
    static bool valid_spawn(std::uint32_t spawn) noexcept {
        return spawn && spawn!=0x3fff0010u && (spawn>>16)<0x3fffu;
    }
    static bool valid_owner(const NativeDoorOwner::Snapshot& owner) noexcept {
        if(!rig::pointer(owner.base) || !rig::pointer(owner.game) || !rig::pointer(owner.world) || !rig::pointer(owner.player) ||
           !rig::pointer(owner.door) || !rig::pointer(owner.part) || !rig::pointer(owner.component) || !rig::pointer(owner.animation) ||
           !rig::pointer(owner.container) || !rig::pointer(owner.schema) || !rig::pointer(owner.binding) ||
           owner.animation>0x0000800000000000ull-0x338 || owner.container!=owner.animation+0x338 ||
           !valid_spawn(owner.player_spawn) || !valid_spawn(owner.door_spawn) || !valid_spawn(owner.part_spawn))return false;
        for(const auto clip:owner.clips)if(clip>=0x80)return false;
        return true;
    }
};
}
