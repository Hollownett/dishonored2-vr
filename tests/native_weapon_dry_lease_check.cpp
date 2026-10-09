#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_weapon_dry_lease.hpp"
#include "native_rig_fixture.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;using namespace d2vr::rig;
    auto memory=fixture();const auto arms=player_arms(memory,base_address);assert(arms);
    NativeWeaponSourceContent source{};source.arm=arms->at(1);source.game=game_address;source.frame=42;source.source=poses[1]+137*64;
    for(unsigned i=0;i<69;++i) {source.remap[i]=static_cast<short>(137+i);source.joints[i].f[0]=source.joints[i].f[5]=source.joints[i].f[10]=1;source.joints[i].f[14]=-.5f;}
    source.joints[2].f[14]=0;source.joints[3].f[14]=-.3f;
    ArmBinding binding{};binding.parents.fill(4);binding.parents[0]=-1;binding.parents[1]=0;binding.parents[2]=1;binding.parents[3]=2;binding.parents[4]=3;
    binding.chain={2,3,4};binding.helpers={64,65,66,67};for(auto helper:binding.helpers) binding.parents[helper]=3;
    binding.bind_wrist=binding.bind_grip=source.joints[4];
    auto rig=std::make_shared<const NativeArmBindingSnapshot>(NativeArmBindingSnapshot{*arms,{binding,binding},{}});
    NativeWeaponModelBase model{source.arm.model,game_address,42,{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1},true};
    ArmTracking tracking;ArmTrackingFrame frame{};frame.tick=100;frame.epoch=7;frame.hands=2;frame.reference_epoch=5;frame.has_head_anchor=true;
    frame.head_anchor.position={1,2.16,3.22};frame.head_anchor.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
    frame.grips[1].position={1,2,2.5};frame.grips[1].basis=frame.head_anchor.basis;
    tracking.configure_calibration({5,1.6,.32,.22,1,{}});tracking.publish(frame);const auto frozen=tracking.snapshot();
    Upload input{source.joints,model.values,137};const auto original=input;
    const auto a=tracking.pose(input,binding,1,100),b=ArmTracking::pose_frozen(input,binding,1,100,frozen);
    assert(a && b && !std::memcmp(a->joints.data(),b->joints.data(),sizeof(a->joints)) && !std::memcmp(&input,&original,sizeof(input)));
    NativeAttachmentCandidate candidate{};candidate.arm=source.arm;candidate.binding=0x900000;
    candidate.binding_identity={7,10};candidate.joint=59;candidate.entity=0x910000;candidate.parent=0x920000;
    candidate.inventory=0x930000;candidate.inventory_identity={3,10};candidate.declaration=0x940000;candidate.declaration_table=0x140123456;
    candidate.slots[1]=0x950000;candidate.slot_tables[1]=0x140567890;candidate.slot_owner_fields[1]=candidate.entity;
    candidate.slot_identity_fields[1]=0x100000003;
    NativeWeaponDryLease oracle;
    auto equipment=std::make_shared<NativeEquipmentOwnership>();equipment->attachment=candidate;
    equipment->equipment=0x960000;equipment->equipment_identity={5,10};equipment->guards.push_back({0x960000,{1}});
    const auto prepare=[&](UINT64 receipt) {return oracle.prepare(rig,source,model,tracking.snapshot(),receipt,7,candidate,100,true,&ArmTracking::pose_frozen,equipment);};
    const auto observe=[&](unsigned site,const NativeWeaponModelBase& current,bool eligible=true) {
        return oracle.observe(1,site,source,current,tracking.stamp(),rig,1,7,candidate,eligible,100,equipment);
    };
    auto raw=oracle.prepare(rig,source,model,tracking.snapshot(),1,7,candidate,100,true,&ArmTracking::pose_frozen);
    assert(!raw.accepted && raw.reason==NativeWeaponDryLease::equipment_absent);
    auto p=prepare(1);assert(p.accepted);
    assert(p.typed_equipment_verified && p.equipment==equipment->equipment);
    auto first=observe(NativeWeaponDryLease::primary,model);assert(first.accepted && first.strict_revision);
    tracking.publish(frame); // Immutable entered snapshot survives an XR-only publication; strict policy is reported false.
    auto secondary=observe(NativeWeaponDryLease::secondary,model);assert(secondary.accepted && !secondary.strict_revision && secondary.token==p.token);
    assert(observe(NativeWeaponDryLease::outer_exit,model).accepted);
    auto gpu=observe(NativeWeaponDryLease::gpu,model);assert(gpu.complete && gpu.token==p.token && oracle.completed()==1);
    auto moved=model;moved.values[0]+=.01f;
    oracle.clear();assert(prepare(1).accepted);
    auto before=observe(NativeWeaponDryLease::primary,moved);assert(!before.accepted && !before.after_primary && before.reason==NativeWeaponDryLease::model_changed);
    oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
    auto after=observe(NativeWeaponDryLease::outer_exit,moved);assert(!after.accepted && after.after_primary && after.reason==NativeWeaponDryLease::model_changed);
    oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
    tracking.clear_calibration();auto revoked=observe(NativeWeaponDryLease::secondary,model);assert(!revoked.accepted && revoked.after_primary);
    tracking.configure_calibration({5,1.6,.32,.22,1,{}});
    oracle.clear();assert(prepare(1).accepted);
    assert(!prepare(1).accepted); // A second binding/candidate cannot replace a same-frame lease.
    oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
    oracle.clear();assert(oracle.revoked()>0 && oracle.before_failures()>0 && oracle.after_failures()>0);
    assert(prepare(1).accepted);
    assert(!observe(NativeWeaponDryLease::gpu,model).accepted); // No GPU-only implicit commit.
    oracle.clear();assert(prepare(1).accepted);
    auto wrong=source;wrong.remap[59]++;
    auto remap=oracle.observe(1,NativeWeaponDryLease::primary,wrong,model,tracking.stamp(),rig,1,7,candidate,true,100,equipment);
    assert(!remap.accepted && !remap.after_primary && remap.reason==NativeWeaponDryLease::source_changed);
    oracle.clear();assert(prepare(1).accepted);
    auto changed_candidate=candidate;changed_candidate.binding_identity.generation++;
    auto identity=oracle.observe(1,NativeWeaponDryLease::primary,source,model,tracking.stamp(),rig,1,7,changed_candidate,true,100,equipment);
    assert(!identity.accepted && identity.reason==NativeWeaponDryLease::equipment_link_mismatch);
    oracle.clear();assert(prepare(1).accepted);
    auto stale=oracle.observe(1,NativeWeaponDryLease::primary,source,model,tracking.stamp(),rig,1,7,candidate,true,351,equipment);
    assert(!stale.accepted && !stale.after_primary && stale.reason==NativeWeaponDryLease::ineligible);
    oracle.clear();assert(prepare(1).accepted);tracking.clear_references();
    auto mapping=observe(NativeWeaponDryLease::primary,model);
    assert(!mapping.accepted && mapping.reason==NativeWeaponDryLease::tracking_changed);
    oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
    assert(observe(NativeWeaponDryLease::outer_exit,model).accepted);
    auto missing=prepare(2);assert(!missing.accepted && missing.after_primary && missing.reason==NativeWeaponDryLease::missing_consumer);
    oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
    assert(!observe(NativeWeaponDryLease::primary,model).accepted);
    // A weapon/slot transition must reject GPU even with unchanged source/base.
    for(unsigned field=0;field<8;++field) {
        oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
        assert(observe(NativeWeaponDryLease::outer_exit,model).accepted);
        auto swapped=candidate;
        switch(field) {case 0:swapped.entity+=16;break;case 1:swapped.inventory+=16;break;
            case 2:swapped.declaration+=16;break;case 3:swapped.declaration_table+=16;break;
            case 4:swapped.slots[1]+=16;break;case 5:swapped.slot_tables[1]+=16;break;
            case 6:swapped.slot_owner_fields[1]+=16;break;case 7:swapped.slot_identity_fields[1]++;break;}
        auto rejected=oracle.observe(1,NativeWeaponDryLease::gpu,source,model,tracking.stamp(),rig,1,7,swapped,true,100,equipment);
        assert(!rejected.accepted && rejected.after_primary && rejected.reason==NativeWeaponDryLease::equipment_link_mismatch);
    }
    oracle.clear();assert(prepare(1).accepted);assert(observe(NativeWeaponDryLease::primary,model).accepted);
    assert(observe(NativeWeaponDryLease::outer_exit,model).accepted);
    assert(!oracle.observe(1,NativeWeaponDryLease::gpu,source,model,tracking.stamp(),rig,1,7,{},true,100,equipment).accepted);
    oracle.clear();
    assert(prepare(1).accepted);
    auto absent=oracle.observe(1,NativeWeaponDryLease::primary,source,model,tracking.stamp(),rig,1,7,candidate,true,100);
    assert(!absent.accepted && absent.reason==NativeWeaponDryLease::equipment_absent);
    oracle.clear();assert(prepare(1).accepted);
    auto replacement=std::make_shared<NativeEquipmentOwnership>(*equipment);
    auto replaced=oracle.observe(1,NativeWeaponDryLease::primary,source,model,tracking.stamp(),rig,1,7,candidate,true,100,replacement);
    assert(!replaced.accepted && replaced.reason==NativeWeaponDryLease::equipment_changed);
    oracle.clear();
    assert(!std::memcmp(&input,&original,sizeof(input)));
    std::cout<<"PASS: shared frozen solver, immutable lease token, current model base, primary/secondary/GPU qualification, before/after hypothetical boundary failures and unchanged native input\n";
}
