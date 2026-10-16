#include "../src/body_renderer.hpp"
#include "native_rig_fixture.hpp"
#include <cassert>
#include <iostream>

int main() {
    auto memory=fixture();
    constexpr std::uintptr_t descriptor=0x211000000,skeleton=0x212000000,
        names=0x213000000,inverse=0x214000000,name_pool=0x215000000,text=0x216000000,
        body_poses=0x217000000,body_states=0x218000000,body_map=0x219000000;
    memory.put(main_anim+0x18,r::Identity{321,10});
    memory.put(main_model+0x1fe8,descriptor);memory.put(descriptor+0x50,skeleton);
    memory.put(skeleton,base_address+0x1b0bd08);memory.put(skeleton+0x78,std::uint16_t{20});
    memory.put(skeleton+0x70,names);memory.put(skeleton+0x120,inverse);
    memory.put(base_address+0x22995c0,base_address+0x22995d0);
    memory.put(base_address+0x22995d0,base_address+0x1b093c8);
    memory.list(base_address+0x22995d8,name_pool,20,20);
    memory.list(main_anim+0x2d8,body_poses,23,23);
    memory.list(main_anim+0x2e8,body_states,4,4);memory.list(body_states+0x30,body_map,20,20);
    memory.put(main_model+0x1860,std::array<float,15>{10,20,30,1,0,0,0,1,0,0,0,1,1,1,1});
    for(unsigned joint=0;joint<20;++joint) {
        memory.put(names+joint*2,static_cast<std::uint16_t>(joint));
        memory.put(name_pool+joint*0x48+0x10,text+joint*128);
        const std::string label=joint==0?"origin":joint==4?"center_spine03_jnt":joint==8?"camera_jnt":"joint_"+std::to_string(joint);
        for(size_t c=0;c<128;++c) memory.put(text+joint*128+c,static_cast<unsigned char>(0));
        for(size_t c=0;c<=label.size();++c) memory.put(text+joint*128+c,static_cast<unsigned char>(label[c]));
        memory.put(inverse+joint*48,std::array<float,12>{1,0,0,0,0,1,0,0,0,0,1,0});
        memory.put(body_map+joint*2,static_cast<std::int16_t>(3+joint));
        r::Transform transform{};transform.f[0]=transform.f[5]=transform.f[10]=1;transform.f[12]=static_cast<float>(joint);
        memory.put(body_poses+(3+joint)*64,transform);
    }
    const auto arms=r::player_arms(memory,base_address);assert(arms);
    const auto binding=r::native_body_bind(memory,base_address,(*arms)[0]);assert(binding);
    constexpr std::uintptr_t record_list=0x21a000000,records=0x21b000000,material=0x21c000000;
    memory.put(main_model+0x1c00,record_list);memory.list(record_list,records,1,1);
    memory.put(records,material);memory.put(main_model+0x1c30,std::uint8_t{0x24});
    memory.list(main_model+0x18a0,0,0,0);
    const auto route=r::body_material_route(memory,base_address,(*arms)[0],*binding);
    assert(route && route->draw_materials.size()==1 && route->draw_materials[0]==material && route->override_count==0);
    memory.list(record_list,records,65,65);assert(!r::body_material_route(memory,base_address,(*arms)[0],*binding));
    memory.list(record_list,records,1,1);
    const auto original=memory;
    const auto upload=[&](std::uintptr_t model=main_model,std::uintptr_t source=0x217000000+3*64,unsigned count=20) {
        return r::body_upload(memory,base_address,(*arms)[0],*binding,model,source,count);
    };
    const auto native=upload();assert(native && native->first_transform==3 && native->joints[19].f[12]==19);
    assert(memory.bytes==original.bytes);
    assert(!upload(models[0]));assert(!upload(main_model,body_poses));assert(!upload(main_model,body_poses+3*64,69));
    memory.put(body_map+10*2,std::int16_t{14});assert(!upload());memory=original;
    memory.put(body_poses+3*64,2.f);assert(!upload());memory=original;
    memory.put(main_model+0x1860+12*4,0.f);assert(!upload());memory=original;
    memory.put(main_anim+0x18,r::Identity{322,10});assert(!upload());memory=original;
    memory.recycle_at=main_anim+0x18;assert(!upload());memory=original;
    r::BodyPalettePublication publication;publication.publish(*binding,native->joints,1,100);
    assert(publication.palette(*binding,1,100,false));
    assert(!publication.palette(*binding,2,100,false));assert(!publication.palette(*binding,1,351,false));
    assert(!publication.palette(*binding,1,99,false));assert(!publication.palette(*binding,1,100,true));
    auto changed=*binding;++changed.animation_identity.generation;
    assert(!publication.palette(changed,1,100,false));
    publication.clear();assert(!publication.palette(*binding,1,100,false));
    // Model-local setter/refresh contracts are exercised with owned mock game
    // memory; no real engine calls, shared surfaces or material objects mutate.
    constexpr std::uintptr_t body_name=0x225000000,arm_records_list=0x226000000,
        arm_records=0x227000000,body_material=0x228000000,material_name=0x229000000,
        override_entries=0x22a000000;
    const auto text_at=[&](std::uintptr_t at,const std::string& name) {
        for(size_t i=0;i<=name.size();++i) memory.put(at+i,static_cast<unsigned char>(name[i]));
    };
    memory.put(descriptor+8,body_name);text_at(body_name,"models/characters/player/corvo/body.md6");
    memory.put(models[0]+0x1c00,arm_records_list);memory.list(arm_records_list,arm_records,1,1);
    memory.put(arm_records,body_material);memory.put(body_material+8,material_name);
    const std::string selected_name="models/characters/player/corvo/textures/player_corvo_body.material";
    text_at(material_name,selected_name);
    memory.put(base_address+0x1b0d628+0x58,base_address+0x2367b0);
    memory.put(base_address+0x1b0d628+0xb8,base_address+0x25ed30);
    memory.put(base_address+0x1b0d628+0x78,base_address+0x25f3c0);
    assert(r::body_material_candidate(memory,base_address,(*arms)[0],*binding)==body_material);
    unsigned setters=0,refreshes=0;
    const auto set_all=[&](void* model,void* selected) {
        assert(reinterpret_cast<std::uintptr_t>(model)==main_model);++setters;
        memory.list(main_model+0x18a0,override_entries,1,1);
        memory.put(override_entries,reinterpret_cast<std::uintptr_t>(selected));
    };
    const auto refresh=[&](void* model) {
        assert(reinterpret_cast<std::uintptr_t>(model)==main_model);++refreshes;
        const auto selected=r::value<std::uintptr_t>(memory,override_entries).value();
        memory.put(records,selected?selected:material);
        memory.put(main_model+0x1c30,static_cast<std::uint8_t>(selected?0x34:0x24));
    };
    r::BodyMaterialOverride override;
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,false,set_all,refresh));assert(setters==0);
    const auto material_object=memory.bytes; // verify fields of source material remain untouched below
    assert(override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    assert(override.active() && setters==1 && refreshes==1);
    assert(override.owns(*binding) && override.restoration_pending());
    auto recycled=*binding;++recycled.animation_identity.generation;
    assert(!override.restore_owned(memory,base_address,(*arms)[0],recycled,true,set_all,refresh));
    assert(setters==1);
    memory.put(main_anim+0x18,recycled.animation_identity);
    assert(!override.apply(memory,base_address,(*arms)[0],recycled,true,set_all,refresh));
    assert(override.reason()==r::BodyMaterialOverride::Reason::owner_or_override && override.owns(*binding));
    assert(setters==1 && refreshes==1);
    memory.put(main_anim+0x18,binding->animation_identity);
    auto state_changed=*binding;state_changed.animation_identity.flags^=4;
    memory.put(main_anim+0x18,state_changed.animation_identity);
    assert(override.apply(memory,base_address,(*arms)[0],state_changed,true,set_all,refresh));
    memory.put(main_anim+0x18,binding->animation_identity);
    text_at(body_name,"models/characters/player/emily/body.md6");
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    text_at(body_name,"models/characters/player/corvo/body.md6");
    text_at(material_name,"models/characters/female/emily/textures/emily_body.material");
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    text_at(material_name,selected_name);
    assert(setters==1 && refreshes==1);
    unsigned legacy_reads=0,active_reads=0;
    const auto counted_legacy=[&](std::uintptr_t at,void* out,std::size_t size) {++legacy_reads;return memory(at,out,size);};
    assert(r::body_material_route(counted_legacy,base_address,(*arms)[0],*binding));
    assert(r::body_material_candidate(counted_legacy,base_address,(*arms)[0],*binding)==body_material);
    const auto counted_active=[&](std::uintptr_t at,void* out,std::size_t size) {++active_reads;return memory(at,out,size);};
    assert(override.apply(counted_active,base_address,(*arms)[0],*binding,true,set_all,refresh));
    assert(active_reads*4<legacy_reads*3 && setters==1 && refreshes==1);
    std::cout<<"material validation reads: legacy route+candidate="<<legacy_reads<<", active apply="<<active_reads<<'\n';
    // A rebuilt arm draw list cannot revoke the body's still-valid owned material.
    memory.put(models[0]+0x1c00,std::uintptr_t{});
    assert(override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    memory.put(models[0]+0x1c00,arm_records_list);
    // Native draw records may be rebuilt while our model-local override remains.
    memory.put(records,material);
    assert(override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    assert(setters==1 && refreshes==2);
    assert(override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));assert(setters==1);
    assert(override.restore(memory,base_address,(*arms)[0],true,set_all,refresh));assert(setters==2 && !override.active());
    assert(r::value<std::uintptr_t>(memory,records)==material);
    assert(r::value<std::uintptr_t>(memory,body_material+8)==material_name);
    for(size_t i=0;i<=selected_name.size();++i) assert(memory.bytes.at(material_name+i)==material_object.at(material_name+i));
    assert(override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));assert(setters==3);
    memory.put(override_entries,std::uintptr_t{0x22b000000});
    assert(!override.restore(memory,base_address,(*arms)[0],true,set_all,refresh));assert(setters==3);
    assert(r::value<std::uintptr_t>(memory,override_entries)==std::uintptr_t{0x22b000000}); // foreign edit preserved
    memory.put(override_entries,body_material);
    assert(override.restore(memory,base_address,(*arms)[0],true,set_all,refresh));assert(setters==4);
    const auto no_refresh=[](void*) {};
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,no_refresh));
    assert(!override.active() && override.restoration_pending() && override.owns(*binding));
    assert(override.original_drawable(*binding)==false);
    // A failed same-owner rebuild consumes at most one native retry per250ms;
    // visibility alone never rebuilds materials. Restoration has no backoff.
    const auto retries=override.refreshes();
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,no_refresh,1000));
    assert(override.refreshes()==retries+1);
    for(std::uint64_t tick=1001;tick<1250;++tick)
        assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,no_refresh,tick));
    assert(override.refreshes()==retries+1 && override.refresh_deferred()==249);
    assert(override.reason()==r::BodyMaterialOverride::Reason::retry_deferred);
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,no_refresh,1250));
    assert(override.refreshes()==retries+2);
    assert(!override.original_drawable(recycled));
    assert(override.restore_owned(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    assert(!override.restoration_pending());
    const auto refresh_without_visibility=[&](void* model) {
        refresh(model);memory.put(main_model+0x1c30,std::uint8_t{0x24});
    };
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh_without_visibility));
    const auto visibility_refreshes=override.refreshes();
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh_without_visibility,1300));
    assert(override.refreshes()==visibility_refreshes && override.reason()==r::BodyMaterialOverride::Reason::visibility);
    unsigned enables{};
    const auto enable=[&](std::uintptr_t model) {
        assert(model==main_model);++enables;
        const auto flags=r::value<std::uint8_t>(memory,model+0x1c30).value();memory.put(model+0x1c30,static_cast<std::uint8_t>(flags|0x10));
    };
    assert(!override.enable_drawable_owned(memory,base_address,(*arms)[0],recycled,true,enable));
    assert(!override.enable_drawable_owned(memory,base_address,(*arms)[0],*binding,false,enable));
    assert(enables==0 && override.original_drawable(*binding)==false);
    assert(override.enable_drawable_owned(memory,base_address,(*arms)[0],*binding,true,enable));
    assert(enables==1 && override.active() && r::value<std::uint8_t>(memory,main_model+0x1c30)==0x34);
    assert(override.restore_owned(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    text_at(body_name,"models/characters/player/emily/body.md6");
    assert(!override.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));assert(setters==8);
    text_at(body_name,"models/characters/player/corvo/body.md6");
    r::BodyMaterialOverride interrupted;
    assert(interrupted.apply(memory,base_address,(*arms)[0],*binding,true,set_all,refresh));
    const auto throwing_refresh=[&](void* model) {refresh(model);throw std::runtime_error("controlled refresh interruption");};
    bool caught=false;
    try {(void)interrupted.restore(memory,base_address,(*arms)[0],true,set_all,throwing_refresh);}
    catch(const std::runtime_error&) {caught=true;}
    assert(caught && interrupted.restoration_pending());
    const auto restoration_setters=setters,restoration_refreshes=refreshes;
    assert(interrupted.restore(memory,base_address,(*arms)[0],true,set_all,refresh));
    assert(!interrupted.restoration_pending() && setters==restoration_setters && refreshes==restoration_refreshes);
    std::cout<<"PASS: exact owned 20-joint source, foreign/recycled owners rejected; bounded rebuild retry; visibility isolation; interrupted restoration recovery\n";
}
