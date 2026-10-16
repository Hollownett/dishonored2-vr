#include "../src/combat_target_memory.hpp"
#include <cassert>
#include <iostream>

using namespace d2vr;

NativePlayerStateLease owner() {return {0x140000000,0x2000000,0x2100000,0x2200000,0x10001,{17,10}};}
const CombatTarget ground{CombatTarget::Kind::ground,0x20001};
const CombatTarget drop{CombatTarget::Kind::drop,0x30002};

void expiry_and_current() {
    CombatTargetMemory memory;
    auto result=memory.update(owner(),1,{},100,true);assert(result.context_changed && !result.remembered);
    result=memory.update(owner(),1,ground,101,true);assert(!result.context_changed && result.remembered==ground);
    result=memory.update(owner(),1,{},1101,true);assert(!result.context_changed && result.remembered==ground);
    result=memory.update(owner(),1,{},1102,true);assert(!result.context_changed && !result.remembered);
    assert(memory.update(owner(),1,ground,1200,true).remembered==ground);
    assert(memory.update(owner(),1,ground,1500,true).remembered==ground);
    assert(memory.update(owner(),1,{},2500,true).remembered==ground);
    assert(!memory.update(owner(),1,{},2501,true).remembered);
    result=memory.update(owner(),1,drop,2600,true);assert(!result.context_changed && result.remembered==drop);
    auto changed=drop;++changed.spawn;
    result=memory.update(owner(),1,changed,2601,true);assert(!result.context_changed && result.remembered==changed);
    changed.kind=CombatTarget::Kind::ground;
    result=memory.update(owner(),1,changed,2602,true);assert(!result.context_changed && result.remembered==changed);
}

void full_owner_changes() {
    for(unsigned field=0;field<8;++field) {
        CombatTargetMemory memory;assert(memory.update(owner(),1,ground,100,true).remembered==ground);
        auto changed=owner();
        if(field==0)++changed.base;
        if(field==1)++changed.game;
        if(field==2)++changed.player;
        if(field==3)++changed.machine;
        if(field==4)++changed.spawn;
        if(field==5)++changed.identity.generation;
        if(field==6)++changed.identity.flags;
        const auto epoch=field==7?2u:1u;
        const auto result=memory.update(changed,epoch,{},101,true);
        assert(result.context_changed && !result.remembered);
        const auto returned=memory.update(owner(),1,{},102,true);
        assert(returned.context_changed && !returned.remembered);
        const auto fresh=memory.update(changed,epoch,drop,103,true);
        assert(fresh.context_changed && fresh.remembered==drop);
    }
}

void invalid_context() {
    for(unsigned loss=0;loss<5;++loss) {
        CombatTargetMemory memory;memory.update(owner(),1,ground,100,true);
        const auto result=memory.update(loss==0?std::nullopt:std::optional<NativePlayerStateLease>{owner()},
            loss==1?0u:1u,drop,loss==2?0u:loss==3?99u:101u,loss!=4);
        assert(result.context_changed && !result.remembered);
        assert(memory.update(owner(),1,{},102,true).context_changed);
        assert(!memory.update(owner(),1,{},103,true).remembered);
    }
    CombatTargetMemory empty;
    assert(!empty.update({},0,ground,100,false).context_changed);
    assert(empty.update(owner(),1,ground,100,true).remembered==ground);
    assert(!empty.update(owner(),1,{},100,true).context_changed);
    assert(empty.update(owner(),1,{},100,true).remembered==ground);
}

int main() {expiry_and_current();full_owner_changes();invalid_context();std::cout<<"combat target memory check passed\n";}
