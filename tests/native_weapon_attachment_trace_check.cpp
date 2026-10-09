#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_weapon_attachment_trace.hpp"
#include <cassert>
#include <map>
#include <vector>
#include <iostream>
struct Memory;
static Memory* live_memory{};
namespace d2vr {
struct NativeWeaponAttachmentTraceCheck {
    static void equipment_publication(bool (*reader)(std::uintptr_t,void*,std::size_t),std::uintptr_t base,
        std::shared_ptr<const rig::NativeArmBindingSnapshot> snapshot,const NativeAttachmentCandidate& candidate) {
        NativeWeaponAttachmentTrace trace;trace.read_=reader;trace.base_=base;trace.active_=true;
        trace.publish(snapshot,7,GetTickCount64(),3,true);
        auto absent=trace.check_equipment(candidate,trace.publication_);
        assert(absent.reason==NativeWeaponDryLease::equipment_absent);
        trace.reject_equipment(absent,candidate,0,0);assert(trace.equipment_absent()==1);
        auto equipment=std::make_shared<NativeEquipmentOwnership>();equipment->attachment=candidate;
        std::vector<unsigned char> generation(8);assert(reader(candidate.binding+0x18,generation.data(),generation.size()));
        equipment->guards.push_back({candidate.binding+0x18,generation});
        trace.equipment_=equipment;trace.equipment_rig_=snapshot;trace.equipment_epoch_=7;
        assert(trace.check_equipment(candidate,trace.publication_).reason==NativeWeaponDryLease::okay);
        auto wrong=candidate;wrong.entity+=8;
        auto mismatch=trace.check_equipment(wrong,trace.publication_);
        assert(mismatch.reason==NativeWeaponDryLease::equipment_link_mismatch);
        trace.reject_equipment(mismatch,wrong,0,1);assert(trace.equipment_mismatch()==1 && trace.equipment_==equipment);
        auto wrong_epoch=trace.publication_;wrong_epoch.epoch=8;
        auto stale=trace.check_equipment(candidate,wrong_epoch);
        assert(stale.reason==NativeWeaponDryLease::equipment_changed);
        trace.reject_equipment(stale,candidate,0,1);assert(trace.equipment_changed()==1 && !trace.equipment_);
        auto recycled=std::make_shared<NativeEquipmentOwnership>(*equipment);recycled->guards[0].bytes[0]^=1;
        trace.equipment_=recycled;trace.equipment_rig_=snapshot;trace.equipment_epoch_=7;
        auto changed_bytes=trace.check_equipment(candidate,trace.publication_);
        assert(changed_bytes.reason==NativeWeaponDryLease::equipment_changed);
        trace.reject_equipment(changed_bytes,candidate,0,1);assert(trace.equipment_changed()==2 && !trace.equipment_);
        trace.equipment_=equipment;trace.equipment_rig_=snapshot;trace.equipment_epoch_=7;
        auto new_rig=std::make_shared<const rig::NativeArmBindingSnapshot>(*snapshot);
        trace.publish(new_rig,7,GetTickCount64(),3,true);assert(!trace.equipment_);
        trace.equipment_=equipment;trace.equipment_rig_=new_rig;trace.equipment_epoch_=7;
        trace.publish(new_rig,7,GetTickCount64(),1,true);assert(!trace.equipment_);
        trace.equipment_=equipment;trace.equipment_rig_=new_rig;trace.equipment_epoch_=7;
        assert(!trace.refresh_equipment(new_rig,7,false) && !trace.equipment_);
        trace.clear();assert(!trace.publication_.arms && !trace.equipment_);trace.active_=false;
    }
    static void gpu_buffer() {
        NativeWeaponAttachmentTrace trace;trace.base_=0x140000000;
        trace.observations_=std::make_unique<std::array<NativeWeaponAttachmentTrace::ModelObservation,8192>>();
        NativeAttachmentCandidate candidate{};candidate.arm.model=0x350000;candidate.arm.side=1;candidate.joint=69;
        NativeWeaponSourceMatch match{};match.model.valid=true;match.model.model=candidate.arm.model;
        match.model.values={0,2,3,1,0,0,0,1,0,0,0,1,1,1,1};
        auto input=match.model.values;
        trace.observe_model(5,candidate,trace.base_+0x541310,match,&input);
        assert(trace.model_observations()==1);
        const auto saved=(*trace.observations_)[0];
        assert(saved.site==5 && saved.caller==0x541310 && saved.has_input_base && saved.input_base_matched && !saved.offset_valid);
        input[0]=-0.0f;trace.observe_model(5,candidate,0,match,&input);
        assert(!(*trace.observations_)[1].input_base_matched && (*trace.observations_)[0].input_base[0]==0);
        trace.active_=true;
        const auto source=trace.match_upload(candidate.arm,0x500000,input,trace.base_+0x541310);
        assert(!source.matched && trace.gpu_observations()==1 && trace.model_observations()==3);
        trace.active_=false;
        (void)trace.match_upload(candidate.arm,0x500000,input);
        assert(trace.gpu_observations()==1 && trace.model_observations()==3);
        for(unsigned i=0;i<8200;++i) trace.observe_model(5,candidate,0,match,&input);
        assert(trace.model_observations()==8192 && trace.disable());
    }
    static void owned_scope(bool (*reader)(std::uintptr_t,void*,std::size_t),std::uintptr_t base,
        std::shared_ptr<const rig::NativeArmBindingSnapshot> snapshot,std::uintptr_t binding) {
        static unsigned nesting{},queries{};static void* owned_binding{};static bool focused=true;
        owned_binding=reinterpret_cast<void*>(binding);
        NativeWeaponAttachmentTrace trace;trace.instance_=&trace;trace.read_=reader;trace.base_=base;
        trace.logic_=+[]()->UINT{return focused?2u:0u;};trace.active_=true;
        trace.dry_reader(+[]()->rig::ArmTrackingSnapshot{return {};},+[]()->bool{return true;});
        trace.publish(snapshot,1,GetTickCount64(),3,true);
        trace.original_query_=+[](void* animation,int stage,short joint,float* p,float* a)->unsigned char {
            ++queries;const auto* outer=NativeWeaponAttachmentTrace::outer_scope_;assert(outer);
            assert(reinterpret_cast<std::uintptr_t>(animation)==outer->candidate.arm.animation && stage==3 && joint==outer->candidate.joint);
            assert(outer->secondary==focused);p[0]=91;a[0]=92;return 0x81;
        };
        trace.original_outer_=+[](void* object) {
            assert(object==owned_binding);
            if(nesting) {assert(!NativeWeaponAttachmentTrace::outer_scope_);return;}
            auto& self=*NativeWeaponAttachmentTrace::instance_;auto* scope=NativeWeaponAttachmentTrace::outer_scope_;assert(scope);
            float p[3]{},a[9]{};
            assert(self.invoke_query(reinterpret_cast<void*>(scope->candidate.arm.animation),3,scope->candidate.joint,p,a,self.base_+0x754234)==0x81);
            assert(p[0]==91 && a[0]==92 && !scope->secondary);
            focused=false;
            assert(self.invoke_query(reinterpret_cast<void*>(scope->candidate.arm.animation),3,scope->candidate.joint,p,a,self.base_+0x754234)==0x81);
            focused=true;
            ++nesting;self.invoke_outer(object,0x999999);--nesting;
            assert(NativeWeaponAttachmentTrace::outer_scope_==scope);
        };
        trace.invoke_outer(owned_binding,base+0x7550f1);
        assert(queries==2 && trace.outer_calls()==2 && trace.owned_outer()==1 && !trace.outer_scope_);
        assert(trace.equipment_absent()==1 && trace.dry().prepared()==0); // Missing typed link never solves or changes native forwarding.
        trace.original_outer_=nullptr;trace.original_query_=nullptr;assert(trace.disable());trace.instance_=nullptr;
    }
    static void forwarding() {
        static unsigned compose_calls{},unpack_calls{},outer_calls{},query_calls{};
        static void* expected_binding=reinterpret_cast<void*>(0x123400);
        NativeWeaponAttachmentTrace trace;
        trace.instance_=&trace;
        trace.original_compose_=+[](void* binding,int stage,float* position,float* axis,float* linear,float* angular,float step) {
            ++compose_calls;assert(binding==expected_binding && stage==3 && step==.125f);
            for(unsigned i=0;i<3;++i) {position[i]=static_cast<float>(i+10);linear[i]=static_cast<float>(i+20);angular[i]=static_cast<float>(i+30);}
            for(unsigned i=0;i<9;++i) axis[i]=static_cast<float>(i+40);
        };
        trace.original_unpack_=+[](const void* source,float* axis,float* position) {
            ++unpack_calls;assert(source==reinterpret_cast<const void*>(0x456700));
            for(unsigned i=0;i<9;++i) axis[i]=static_cast<float>(i+50);
            for(unsigned i=0;i<3;++i) position[i]=static_cast<float>(i+60);
        };
        std::array<float,3> position{},linear{},angular{};std::array<float,9> axis{};
        trace.compose(expected_binding,3,position.data(),axis.data(),linear.data(),angular.data(),.125f);
        assert(compose_calls==1 && trace.calls()==1 && trace.samples()==0);
        for(unsigned i=0;i<3;++i) assert(position[i]==i+10.f && linear[i]==i+20.f && angular[i]==i+30.f);
        for(unsigned i=0;i<9;++i) assert(axis[i]==i+40.f);
        trace.unpack(reinterpret_cast<const void*>(0x456700),axis.data(),position.data());
        assert(unpack_calls==1);
        for(unsigned i=0;i<3;++i) assert(position[i]==i+60.f);
        for(unsigned i=0;i<9;++i) assert(axis[i]==i+50.f);
        NativeWeaponAttachmentTrace::OuterScope inherited{};
        trace.outer_scope_=&inherited;
        trace.original_outer_=+[](void* binding) {
            ++outer_calls;assert(binding==expected_binding && !NativeWeaponAttachmentTrace::outer_scope_);
        };
        trace.invoke_outer(expected_binding,0x999999);
        assert(outer_calls==1 && trace.outer_scope_==&inherited);
        trace.original_query_=+[](void* animation,int stage,short joint,float* p,float* a)->unsigned char {
            ++query_calls;assert(animation==expected_binding && stage==2 && joint==17);
            assert(!NativeWeaponAttachmentTrace::outer_scope_->secondary);
            p[0]=71;a[0]=72;return 0x81;
        };
        inherited.secondary=true;
        assert(trace.invoke_query(expected_binding,2,17,position.data(),axis.data(),0x999999)==0x81);
        assert(query_calls==1 && position[0]==71 && axis[0]==72 && inherited.secondary);
        trace.original_outer_=+[](void*) {throw std::runtime_error("native exception");};
        try {trace.invoke_outer(expected_binding,0x999999);assert(false);} catch(const std::runtime_error&) {}
        assert(trace.outer_scope_==&inherited);
        trace.outer_scope_=nullptr;
        // These fake originals are forwarding fixtures, not installed hooks.
        trace.original_outer_=nullptr;trace.original_query_=nullptr;
        trace.publish({},1,GetTickCount64(),3,true);assert(!trace.publication_.arms);
        assert(trace.disable());trace.instance_=nullptr;
    }
};
}
struct Memory {
    std::map<std::uintptr_t,std::vector<unsigned char>> fields;
    template<class T> void put(std::uintptr_t at,const T& value) {
        std::vector<unsigned char> bytes(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));fields[at]=bytes;
    }
    bool operator()(std::uintptr_t at,void* output,std::size_t size) {
        auto it=fields.upper_bound(at);if(it==fields.begin()) return false;--it;
        if(at<it->first || size>it->second.size() || at-it->first>it->second.size()-size) return false;
        std::memcpy(output,it->second.data()+at-it->first,size);return true;
    }
};
int main() {
    {
        d2vr::NativeWeaponAttachmentTrace trace;d2vr::rig::Arm arm{};arm.side=1;
        std::array<float,15> model{};alignas(16) std::array<d2vr::rig::Transform,69> baseline{};
        unsigned calls{};
        const auto original=[&](void* object,const void* input,UINT count) {
            ++calls;assert(object==nullptr && input==baseline.data() && count==69);
        };
        trace.invoke_input_upload(arm,nullptr,nullptr,baseline.data(),69,model,0x541310,original);
        assert(calls==1 && trace.input_fallbacks()==0); // Disabled baseline has no experiment side effects.
        trace.input_experiment(true);
        trace.invoke_input_upload(arm,nullptr,nullptr,baseline.data(),69,model,0x541310,original);
        assert(calls==2 && trace.input_fallbacks()==1 && trace.input_gpu_substitutions()==0);
        try {trace.invoke_input_upload(arm,nullptr,nullptr,baseline.data(),69,model,0x541310,
            [&](void*,const void* input,UINT){++calls;assert(input==baseline.data());throw std::runtime_error("original");});assert(false);
        } catch(const std::runtime_error&) {}
        assert(calls==3 && trace.input_unwinds()==1 && trace.input_fallbacks()==2);
    }
    using namespace d2vr;using namespace d2vr::rig;
    constexpr std::uintptr_t base=0x140000000,game=0x300000,player=0x310000,body=0x320000,inventory=0x330000,
        animation=0x340000,model=0x350000,skeleton=0x360000,parent=0x370000,entity=0x380000,binding=0x390000;
    Identity ready{1,0x2000000a};Memory memory;
    auto pointer=[&](std::uintptr_t at,std::uintptr_t value) {memory.put(at,value);};
    pointer(base+0x39a5290,game);pointer(game,base+0x1c940a8);pointer(game+0x3130b0,player);
    pointer(player,base+0x1c5e428);pointer(body,base+0x1c5a8d8);pointer(body+0x10,player);memory.put(body+0x18,ready);
    pointer(body+0x20,base+0x1c5a9b0);pointer(animation+0x20,base+0x1b804f0);
    pointer(body+0x378,animation);pointer(body+0x60,inventory);
    pointer(animation,base+0x1b80408);memory.put(animation+0x18,ready);pointer(animation+0x10,parent);pointer(animation+0x38,model);
    pointer(model+0x1fe8,0x430000);pointer(0x430000+0x50,skeleton);
    pointer(model,base+0x1b0d628);memory.put(skeleton+0x78,std::uint16_t{69});
    pointer(parent,base+0x1c87d58);pointer(parent+0xf0,model);pointer(entity,base+0x1c886a8);pointer(entity+0x358,binding);
    pointer(binding+8,0x610000);pointer(0x610000,base+0x123450);
    pointer(binding+0x20,base+0x1c574c0);pointer(binding,base+0x1c573e8);memory.put(binding+0x18,ready);pointer(binding+0x10,entity);pointer(binding+0x3c8,parent);
    pointer(binding+0x400,animation);memory.put(binding+0x3d0,std::int16_t{20});memory.put(binding+0x84,std::uint32_t{123});
    pointer(inventory,base+0x1b9a878);pointer(inventory+0x10,player);memory.put(inventory+0x18,ready);
    memory.put(inventory+0x68,std::array<std::uintptr_t,2>{0,0});
    Arm left{player,body,animation,model,skeleton,ready,ready,0};
    Arm right{player,body,0x400000,0x410000,0x420000,ready,ready,1};
    NativeArmBindingSnapshot snapshot{{left,right},{},{}};
    auto candidate=native_attachment_candidate(memory,base,snapshot,binding,3);
    assert(candidate && candidate->arm.side==0 && candidate->joint==20 && candidate->slots[0]==0);
    assert(!native_attachment_candidate(memory,base,snapshot,binding,2));
    memory.put(binding+0x3d0,std::int16_t{69});assert(!native_attachment_candidate(memory,base,snapshot,binding,3));
    memory.put(binding+0x3d0,std::int16_t{20});pointer(game+0x3130b0,0x500000);assert(!native_attachment_candidate(memory,base,snapshot,binding,3));
    pointer(game+0x3130b0,player);pointer(binding+0x400,right.animation);assert(!native_attachment_candidate(memory,base,snapshot,binding,3));
    pointer(binding+0x400,animation);pointer(entity+0x358,0x500000);assert(!native_attachment_candidate(memory,base,snapshot,binding,3));
    pointer(entity+0x358,binding);
    constexpr std::uintptr_t unknown_slot=0x600000;
    memory.put(inventory+0x68,std::array<std::uintptr_t,2>{unknown_slot,0});pointer(unknown_slot,base+0x111111);pointer(unknown_slot+0x10,0x700000);memory.put(unknown_slot+0x18,UINT64{123456});
    candidate=native_attachment_candidate(memory,base,snapshot,binding,3);
    assert(candidate && candidate->slots[1]==unknown_slot && candidate->slot_owner_fields[1]==0x700000); // No asserted equipment/entity equivalence.
    live_memory=&memory;
    NativeWeaponAttachmentTraceCheck::equipment_publication(+[](std::uintptr_t at,void* out,std::size_t size){return (*live_memory)(at,out,size);},
        base,std::make_shared<const NativeArmBindingSnapshot>(snapshot),*candidate);
    auto swapped=*candidate;swapped.slot_identity_fields[1]++;
    assert(!same_attachment_candidate(*candidate,swapped));
    swapped=*candidate;swapped.inventory+=16;assert(!same_attachment_candidate(*candidate,swapped));
    swapped=*candidate;swapped.declaration_table+=16;assert(!same_attachment_candidate(*candidate,swapped));
    // Stable slot pointer is insufficient when its generation changes between reads.
    unsigned generation_reads{};
    auto generation_changes=[&](std::uintptr_t at,void* output,std::size_t size) {
        if(at==unknown_slot+0x18 && ++generation_reads==2) memory.put(at,UINT64{123457});
        return memory(at,output,size);
    };
    assert(!native_attachment_candidate(generation_changes,base,snapshot,binding,3));
    memory.put(unknown_slot+0x18,UINT64{123456});
    unsigned declaration_reads{};
    auto declaration_changes=[&](std::uintptr_t at,void* output,std::size_t size) {
        if(at==0x610000 && ++declaration_reads==2) pointer(at,base+0x123458);
        return memory(at,output,size);
    };
    assert(!native_attachment_candidate(declaration_changes,base,snapshot,binding,3));
    pointer(0x610000,base+0x123450);
    unsigned slot_reads{};
    auto changing=[&](std::uintptr_t at,void* output,std::size_t size) {
        if(at==inventory+0x68 && ++slot_reads==2) memory.put(inventory+0x68,std::array<std::uintptr_t,2>{0,0});
        return memory(at,output,size);
    };
    assert(!native_attachment_candidate(changing,base,snapshot,binding,3));
    live_memory=&memory;
    NativeWeaponAttachmentTraceCheck::owned_scope(+[](std::uintptr_t at,void* out,std::size_t size){return (*live_memory)(at,out,size);},
        base,std::make_shared<const NativeArmBindingSnapshot>(snapshot),binding);
    NativeWeaponAttachmentTraceCheck::forwarding();
    NativeWeaponAttachmentTraceCheck::gpu_buffer();
    std::cout<<"PASS: bounded native attachment ownership, stage/socket checks, Empty and unknown slots, mid-read transition, unchanged seven-argument ABI outputs and exact single calls\n";
}

