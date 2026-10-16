#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_grab.hpp"
#include "native_hand_occupancy.hpp"
#include <limits>

namespace d2vr {
class NativeDoorEmptyGate {
public:
    struct Lease {
        NativePlayerCamera owner{};
        rig::NativeHandState hands{};
        UINT64 epoch{},picked{},observed_tick{},lifecycle_revision{};
        unsigned admitted_hands{3};
    };
private:
    template<class Read> struct Reader {
        Read& input;
        static bool cpp(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
            try {return read(address,output,size);}catch(...) {return false;}
        }
        static bool safe(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
            __try {return cpp(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        }
        bool operator()(std::uintptr_t address,void* output,std::size_t size) noexcept {
            return safe(input,address,output,size);
        }
    };
    static bool owner_equal(const NativePlayerCamera& first,const NativePlayerCamera& second) noexcept {
        return first.player && first.camera && first.player==second.player && first.camera==second.camera &&
            first.body==second.body && first.controller==second.controller && first.physics==second.physics &&
            first.heading_delegate==second.heading_delegate && first.camera_identity==second.camera_identity &&
            first.physics_identity==second.physics_identity && !first.authored_view() && !second.authored_view();
    }
    static bool hands_equal(const rig::NativeHandState& first,const rig::NativeHandState& second) noexcept {
        return first.arms==second.arms && first.inventory==second.inventory && first.inventory_identity==second.inventory_identity &&
            first.graph==second.graph && first.equip_name_lists==second.equip_name_lists && first.slots==second.slots &&
            first.model_descriptors==second.model_descriptors && first.equip_name_texts==second.equip_name_texts &&
            first.equip_indices==second.equip_indices && first.carry==second.carry &&
            first.equip_lists[0].data==second.equip_lists[0].data && first.equip_lists[1].data==second.equip_lists[1].data &&
            first.equip_lists[0].packed_count==second.equip_lists[0].packed_count &&
            first.equip_lists[1].packed_count==second.equip_lists[1].packed_count &&
            first.equip_lists[0].packed_capacity==second.equip_lists[0].packed_capacity &&
            first.equip_lists[1].packed_capacity==second.equip_lists[1].packed_capacity;
    }
    static bool valid_mask(unsigned mask) noexcept {return mask && mask<=3;}
    static bool empty(const rig::NativeHandState& hands,const NativePlayerCamera& owner,unsigned mask) noexcept {
        return valid_mask(mask) && hands.empty_candidates<=3 && (hands.empty_candidates&mask)==mask &&
            (!(mask&1u) || !hands.slots[0]) && (!(mask&2u) || !hands.slots[1]) && !hands.carrying &&
            hands.carry.valid_owner() && !hands.carry.carrying() && hands.carry.player==owner.player &&
            hands.carry.body==owner.body && hands.arms[0].player==owner.player && hands.arms[1].player==owner.player &&
            hands.arms[0].body==owner.body && hands.arms[1].body==owner.body;
    }
    static bool lanes_valid(const std::array<NativeGrab::DiagnosticLane,2>& lanes,const NativePlayerCamera& owner,UINT64 now,unsigned mask) noexcept {
        if(!valid_mask(mask) || !lanes[0].epoch || lanes[0].epoch!=lanes[1].epoch || !lanes[0].lifecycle_revision ||
           lanes[0].lifecycle_revision==std::numeric_limits<UINT64>::max() || lanes[0].lifecycle_revision!=lanes[1].lifecycle_revision)return false;
        for(unsigned side=0;side<2;++side) {
            const auto& lane=lanes[side];
            if(lane.player!=owner.player || lane.camera!=owner.camera || !owner_equal(lane.publication_owner,owner) ||
                !lane.serial || !lane.publication_tick || !lane.source_tick ||
                now<lane.publication_tick || now-lane.publication_tick>grab_pose_freshness_ms ||
                now<lane.source_tick || now-lane.source_tick>grab_pose_freshness_ms || !lane.source_age_valid)return false;
            if((mask&(1u<<side)) && (!lane.usable || lane.occupied || lane.held || lane.pending_release || lane.deferred ||
                (lane.role!=RightGripOwner::Role::release && lane.role!=RightGripOwner::Role::ready)))return false;
        }
        return true;
    }
    static bool same_publications(const std::array<NativeGrab::DiagnosticLane,2>& first,
                                  const std::array<NativeGrab::DiagnosticLane,2>& second) noexcept {
        for(unsigned side=0;side<2;++side) {
            if(first[side].epoch!=second[side].epoch || first[side].serial!=second[side].serial ||
                first[side].publication_tick!=second[side].publication_tick || first[side].source_tick!=second[side].source_tick ||
                first[side].acquisition_tick!=second[side].acquisition_tick ||
                first[side].lifecycle_revision!=second[side].lifecycle_revision)return false;
        }
        return true;
    }
public:
    template<class Read,class Diagnostics,class Picked>
    static std::optional<Lease> capture_with(std::uintptr_t base,Read& input,const NativePlayerCamera& owner,
                                            Diagnostics&& diagnostics,Picked&& picked,UINT64 now,unsigned admitted_hands=3) {
        try {
            Reader<Read> read{input};const auto before=picked();const auto lanes=diagnostics(now);
            if(!now || !lanes || !lanes_valid(*lanes,owner,now,admitted_hands))return {};
            const auto camera=native_player_camera(read,base,owner.camera);
            if(!camera || !owner_equal(owner,*camera))return {};
            const auto hands=rig::native_hand_state(read,base);
            if(!hands || !empty(*hands,*camera,admitted_hands))return {};
            const auto carry=rig::native_carry_current(read,base,hands->carry);
            if(!carry || *carry!=hands->carry || carry->carrying())return {};
            const auto again=rig::native_hand_state(read,base);
            const auto after_camera=native_player_camera(read,base,owner.camera);
            const auto after_carry=rig::native_carry_current(read,base,hands->carry);
            const auto after_lanes=diagnostics(now);const auto after=picked();
            if(before!=after || !again || !empty(*again,*camera,admitted_hands) || !hands_equal(*hands,*again) ||
                !after_camera || !owner_equal(*camera,*after_camera) || !after_carry || *after_carry!=hands->carry ||
                !after_lanes || !lanes_valid(*after_lanes,*after_camera,now,admitted_hands) || !same_publications(*lanes,*after_lanes))return {};
            return Lease{*after_camera,*again,(*lanes)[0].epoch,after,now,(*lanes)[0].lifecycle_revision,admitted_hands};
        }catch(...) {return {};}
    }
    template<class Read> static std::optional<Lease> capture(std::uintptr_t base,Read& read,const NativePlayerCamera& owner,
                                                           NativeGrab& grab,UINT64 now,unsigned admitted_hands=3) {
        return capture_with(base,read,owner,[&](UINT64 tick){return grab.diagnostics(tick);},[&]{return grab.picked();},now,admitted_hands);
    }
    template<class Diagnostics,class Picked>
    static bool publications_current_with(const Lease& lease,Diagnostics&& diagnostics,Picked&& picked,UINT64 now) {
        if(!now || now<lease.observed_tick || !lease.epoch || !lease.lifecycle_revision ||
           lease.lifecycle_revision==std::numeric_limits<UINT64>::max())return false;
        try {
            const auto before=picked();const auto first=diagnostics(now);
            if(!first || !lanes_valid(*first,lease.owner,now,lease.admitted_hands) || (*first)[0].epoch!=lease.epoch ||
               (*first)[0].lifecycle_revision!=lease.lifecycle_revision)return false;
            const auto second=diagnostics(now);const auto after=picked();
            return before==after && before==lease.picked && second && lanes_valid(*second,lease.owner,now,lease.admitted_hands) &&
                (*second)[0].epoch==lease.epoch && (*second)[0].lifecycle_revision==lease.lifecycle_revision &&
                same_publications(*first,*second);
        }catch(...) {return false;}
    }
    static bool publications_current(NativeGrab& grab,const Lease& lease,UINT64 now) {
        return publications_current_with(lease,[&](UINT64 tick){return grab.diagnostics(tick);},[&]{return grab.picked();},now);
    }
    template<class Read,class Diagnostics,class Picked>
    static bool current_with(std::uintptr_t base,Read& read,const NativePlayerCamera& owner,const Lease& lease,
                             Diagnostics&& diagnostics,Picked&& picked,UINT64 now) {
        if(now<lease.observed_tick || !owner_equal(owner,lease.owner))return false;
        const auto fresh=capture_with(base,read,owner,diagnostics,picked,now,lease.admitted_hands);
        return fresh && fresh->epoch==lease.epoch && fresh->picked==lease.picked &&
            fresh->lifecycle_revision==lease.lifecycle_revision && hands_equal(fresh->hands,lease.hands);
    }
    template<class Read> static bool current(std::uintptr_t base,Read& read,const NativePlayerCamera& owner,NativeGrab& grab,
                                            const Lease& lease,UINT64 now) {
        return current_with(base,read,owner,lease,[&](UINT64 tick){return grab.diagnostics(tick);},[&]{return grab.picked();},now);
    }
};
}
