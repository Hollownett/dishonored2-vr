#include "../src/native_door_request.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
using Owner=d2vr::NativeDoorOwner;
using Factory=d2vr::NativeDoorRequest;

Owner::Snapshot owner_fixture() {
    Owner::Snapshot owner{};
    owner.base=0x140000000;
    owner.game=0x20000000;
    owner.world=0x21000000;
    owner.player=0x22000000;
    owner.door=0x23000000;
    owner.part=0x24000000;
    owner.component=0x25000000;
    owner.animation=0x26000000;
    owner.container=owner.animation+0x338;
    owner.schema=0x27000000;
    owner.binding=0x28000000;
    owner.player_spawn=0x12;
    owner.door_spawn=0x10013;
    owner.part_spawn=0x20014;
    owner.component_identity.generation=17;
    owner.animation_identity.generation=31;
    owner.clips={3,4,7,8};
    return owner;
}

Factory::Tokens token_fixture() {
    return {101,202,303,404,505};
}

void check_orientation_and_fields() {
    const auto owner=owner_fixture();
    const auto tokens=token_fixture();
    const auto ccw=Factory::make(owner,tokens,true);
    const auto cw=Factory::make(owner,tokens,false);
    assert(ccw && cw);
    assert(ccw->operation.opening_clip==4 && ccw->operation.closing_clip==7);
    assert(cw->operation.opening_clip==3 && cw->operation.closing_clip==8);
    for(const auto* request:{&*ccw,&*cw}) {
        const auto& operation=request->operation;
        assert(operation.ownership==tokens.ownership && operation.world==tokens.world);
        assert(operation.player_generation==tokens.player && operation.part_generation==tokens.part);
        assert(operation.player==owner.player && operation.part==owner.part);
        assert(operation.animation==owner.animation && operation.container==owner.container && operation.binding==owner.binding);
        assert(operation.desired_seconds==0.f && !operation.close_authorized && !operation.linked);
        assert(request->game==owner.game && request->manager==owner.game+0x1e8968);
        assert(request->component==owner.component && request->door==owner.door);
        assert(request->component_generation==tokens.component);
        assert(request->door_spawn==owner.door_spawn && request->player_spawn==owner.player_spawn && request->part_spawn==owner.part_spawn);
        assert(operation.player_generation!=owner.component_identity.generation);
        assert(operation.part_generation!=owner.animation_identity.generation);
    }
    auto maximum_tokens=token_fixture();
    maximum_tokens={UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
    assert(Factory::make(owner,maximum_tokens,false));
}

void check_invalid_tokens_and_owner() {
    const auto owner=owner_fixture();
    const auto tokens=token_fixture();
    for(unsigned index=0;index<5;++index) {
        auto invalid=tokens;
        std::uint64_t* fields[]{&invalid.ownership,&invalid.world,&invalid.player,&invalid.part,&invalid.component};
        *fields[index]=0;
        assert(!Factory::make(owner,invalid,false));
    }
    auto invalid_owner=owner;
    invalid_owner.container+=8;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.animation=0x0000800000000000ull-0x100;
    invalid_owner.container=invalid_owner.animation+0x338;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.game=0x0000800000000000ull-0x1000;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.binding=0x28000001;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.clips[2]=0x80;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.player_spawn=0;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.door_spawn=0x3fff0010u;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.part_spawn=0x3fff0001u;
    assert(!Factory::make(invalid_owner,tokens,false));
    invalid_owner=owner;
    invalid_owner.door_spawn=0x3ffe0001u;
    assert(Factory::make(invalid_owner,tokens,false));
}
}

int main() {
    check_orientation_and_fields();
    check_invalid_tokens_and_owner();
    std::cout<<"native_door_request_check PASS\n";
}
