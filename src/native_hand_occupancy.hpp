#pragma once
#include "game_rig.hpp"
#include "native_carry_snapshot.hpp"
#include <algorithm>

namespace d2vr::rig {
struct NativeHandState {
    std::array<Arm,2> arms{};
    std::uintptr_t inventory{},graph{};
    Identity inventory_identity{};
    std::array<std::uintptr_t,2> equip_name_lists{},slots{};
    std::array<std::uintptr_t,2> model_descriptors{},equip_name_texts{};
    std::array<List,2> equip_lists{};
    std::array<std::int8_t,2> equip_indices{};
    unsigned empty_candidates{};
    NativeCarrySnapshot carry{};
    bool carrying{};
};
// Read-only exact-build path. Player locomotion has its own per-arm name lists;
// generic arkDeclLocomotion's +0x88 layout does not describe this player graph.
template<class Read> std::optional<NativeHandState> native_hand_state(Read& read,std::uintptr_t base) {
    const auto arms=player_arms(read,base);if(!arms) return {};
    const auto body=(*arms)[0].body,player=(*arms)[0].player;
    const auto carry=native_carry_snapshot(read,base,player,body);if(!carry)return {};
    const auto inventory=value<std::uintptr_t>(read,body+0x60);
    if(!inventory || !typed(read,base,*inventory,0x1b9a878) || value<std::uintptr_t>(read,*inventory+0x10)!=player) return {};
    const auto inventory_identity=started(read,*inventory);if(!inventory_identity) return {};
    std::array<std::uintptr_t,2> slots{};
    if(!read(*inventory+0x68,slots.data(),sizeof(slots))) return {};
    NativeHandState result{};result.arms=*arms;result.inventory=*inventory;result.inventory_identity=*inventory_identity;
    result.carry=*carry;result.carrying=carry->carrying();
    // Native inventory enum: SLOT_RIGHT_HAND=0, SLOT_LEFT_HAND=1.
    result.slots={slots[1],slots[0]};
    std::array<std::uintptr_t,2> model_descriptors{},texts{};std::array<List,2> lists{};
    for(unsigned side=0;side<2;++side) {
        const auto part=body+(side?0x680:0x370);
        const auto descriptor=value<std::uintptr_t>(read,(*arms)[side].model+0x1fe8);
        if(!descriptor || !pointer(*descriptor)) return {};model_descriptors[side]=*descriptor;
        const auto graph=value<std::uintptr_t>(read,*descriptor+0x138);
        if(!graph || !typed(read,base,*graph,0x1c4d448) || (side && result.graph!=*graph)) return {};
        result.graph=*graph;
        const auto node=value<std::uintptr_t>(read,part+0x18);
        if(!node || *node!=*graph+(side?0x1d0:0x190) || !typed(read,base,*node,0x1c4d438)) return {};
        const auto index=value<std::int8_t>(read,part+0x21);
        const auto names=list(read,*graph+(side?0xb0:0xa0),128);
        if(!index || !names || *index<0 || static_cast<unsigned>(*index)>=names->packed_count) return {};
        lists[side]=*names;result.equip_indices[side]=*index;result.equip_name_lists[side]=names->data;
        const auto text=value<std::uintptr_t>(read,names->data+static_cast<unsigned>(*index)*8);
        if(!text || *text<0x10000 || *text>=0x0000800000000000-6) return {};texts[side]=*text;
        std::array<char,6> name{};
        if(!read(*text,name.data(),name.size())) return {};
        const std::array<char,6> empty{'E','m','p','t','y','\0'};
        if(!result.slots[side] && name==empty) result.empty_candidates|=1u<<side;
    }
    // A slot/tag pair can change between reads during native equip events.
    std::array<std::uintptr_t,2> again{};
    if(!read(*inventory+0x68,again.data(),sizeof(again)) || again!=slots || started(read,*inventory)!=inventory_identity ||
        value<std::uintptr_t>(read,*inventory+0x10)!=player || value<std::uintptr_t>(read,body+0x60)!=inventory || player_arms(read,base)!=arms) return {};
    for(unsigned side=0;side<2;++side) {
        const auto part=body+(side?0x680:0x370);
        const auto names=list(read,result.graph+(side?0xb0:0xa0),128);
        std::array<char,6> name{};
        if(value<std::uintptr_t>(read,(*arms)[side].model+0x1fe8)!=model_descriptors[side] ||
            value<std::uintptr_t>(read,model_descriptors[side]+0x138)!=result.graph ||
            value<std::uintptr_t>(read,part+0x18)!=result.graph+(side?0x1d0:0x190) ||
            value<std::int8_t>(read,part+0x21)!=result.equip_indices[side] || !names ||
            names->data!=lists[side].data || names->packed_count!=lists[side].packed_count || names->packed_capacity!=lists[side].packed_capacity ||
            value<std::uintptr_t>(read,names->data+static_cast<unsigned>(result.equip_indices[side])*8)!=texts[side] ||
            !read(texts[side],name.data(),name.size())) return {};
        const std::array<char,6> empty{'E','m','p','t','y','\0'};
        if((result.empty_candidates&(1u<<side)) && name!=empty) return {};
    }
    result.model_descriptors=model_descriptors;result.equip_name_texts=texts;result.equip_lists=lists;
    const auto carry_after=native_carry_current(read,base,*carry);if(!carry_after || *carry_after!=*carry)return {};
    if(result.carrying)result.empty_candidates=0;
    return result;
}
// Callback-time revocation only: never creates eligibility. The caller supplies
// its freshly verified upload Arm and a recently published sustained snapshot.
// No heap traversal or name scanning; only this side's pinned Empty entry.
template<class Read> unsigned native_empty_side(Read& read,std::uintptr_t base,const NativeHandState& state,
                                               unsigned sustained_bits,const Arm& verified_arm) {
    const auto side=verified_arm.side;if(side>1 || !(sustained_bits&state.empty_candidates&(1u<<side)) ||
        !(verified_arm==state.arms[side]) || state.carrying || state.carry.carrying()) return 0;
    const auto carry=native_carry_current(read,base,state.carry);if(!carry || *carry!=state.carry || carry->carrying())return 0;
    const auto& arm=state.arms[side];const auto part=arm.body+(side?0x680:0x370);
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8) || value<std::uintptr_t>(read,*game+0x3130b0)!=arm.player ||
        value<std::uintptr_t>(read,arm.body+0x10)!=arm.player || started(read,arm.body)!=arm.body_identity ||
        value<std::uintptr_t>(read,arm.body+0x60)!=state.inventory || !typed(read,base,state.inventory,0x1b9a878) ||
        value<std::uintptr_t>(read,state.inventory+0x10)!=arm.player || started(read,state.inventory)!=state.inventory_identity) return 0;
    const auto valid=[&]() {
        const auto names=list(read,state.graph+(side?0xb0:0xa0),128);
        if(value<std::uintptr_t>(read,part+8)!=arm.animation || started(read,arm.animation)!=arm.animation_identity ||
            value<std::uintptr_t>(read,arm.animation+0x38)!=arm.model ||
            value<std::uintptr_t>(read,arm.model+0x1fe8)!=state.model_descriptors[side] ||
            value<std::uintptr_t>(read,state.model_descriptors[side]+0x138)!=state.graph ||
            !typed(read,base,state.graph,0x1c4d448) || value<std::uintptr_t>(read,part+0x18)!=state.graph+(side?0x1d0:0x190) ||
            !typed(read,base,state.graph+(side?0x1d0:0x190),0x1c4d438) ||
            value<std::int8_t>(read,part+0x21)!=state.equip_indices[side] || state.equip_indices[side]<0 ||
            !names || names->data!=state.equip_lists[side].data || names->packed_count!=state.equip_lists[side].packed_count ||
            names->packed_capacity!=state.equip_lists[side].packed_capacity ||
            static_cast<unsigned>(state.equip_indices[side])>=names->packed_count ||
            value<std::uintptr_t>(read,names->data+static_cast<unsigned>(state.equip_indices[side])*8)!=state.equip_name_texts[side] ||
            value<std::uintptr_t>(read,state.inventory+(side?0x68:0x70))!=std::uintptr_t{0}) return false;
        std::array<char,6> text{};const std::array<char,6> empty{'E','m','p','t','y','\0'};
        return read(state.equip_name_texts[side],text.data(),text.size()) && text==empty;
    };
    if(!valid() || !valid() || started(read,state.inventory)!=state.inventory_identity ||
        value<std::uintptr_t>(read,arm.body+0x60)!=state.inventory || value<std::uintptr_t>(read,*game+0x3130b0)!=arm.player) return 0;
    const auto carry_after=native_carry_current(read,base,state.carry);
    return carry_after && *carry_after==state.carry && !carry_after->carrying()?1u<<side:0;
}
// Occupancy can lead the visible equip animation. Require a sustained owned
// Empty graph plus null native slot, then clear immediately on any mismatch.
class NativeHandOccupancy {
    std::optional<NativeHandState> owner_;
    std::uint64_t epoch_{},last_tick_{};
    std::array<std::uint64_t,2> since_{};
    std::array<unsigned,2> samples_{};
    unsigned eligible_{};
public:
    void clear() noexcept {owner_.reset();epoch_=last_tick_=0;since_={};samples_={};eligible_=0;}
    unsigned observe(const std::optional<NativeHandState>& state,std::uint64_t tick,std::uint64_t epoch,bool gameplay_focused) {
        if(!state || !epoch || !gameplay_focused || state->empty_candidates>3 || !state->carry.valid_owner() ||
           state->carrying || state->carry.carrying()) {clear();return 0;}
        const bool same=owner_ && owner_->arms==state->arms && owner_->inventory==state->inventory &&
            owner_->inventory_identity==state->inventory_identity && owner_->graph==state->graph && owner_->equip_name_lists==state->equip_name_lists &&
            owner_->carry==state->carry;
        if(!same || epoch_!=epoch || tick<last_tick_ || tick-last_tick_>200) {
            clear();owner_=state;epoch_=epoch;
        } else if(tick==last_tick_) {
            for(unsigned side=0;side<2;++side) if(!(state->empty_candidates&(1u<<side))) {
                samples_[side]=0;since_[side]=0;eligible_&=~(1u<<side);
            }
            return eligible_;
        }
        last_tick_=tick;
        for(unsigned side=0;side<2;++side) {
            if(!(state->empty_candidates&(1u<<side))) {samples_[side]=0;since_[side]=0;eligible_&=~(1u<<side);continue;}
            if(!samples_[side]) since_[side]=tick;
            if(samples_[side]<20) ++samples_[side];
            if(samples_[side]>=20 && tick-since_[side]>=1000) eligible_|=1u<<side;
        }
        return eligible_;
    }
};
} // namespace d2vr::rig
