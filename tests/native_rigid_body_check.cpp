#include "../src/native_rigid_body.hpp"
#include "../src/native_cast_exclusion.hpp"
#include "../src/mass_probe.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <iostream>
int main() {
    using namespace d2vr;
    const std::uintptr_t base=0x140000000,game=0x1000000,entity=0x2000000,component=0x3000000,entries=0x4000000,world=0x5000000,pool=0x6000000;
    const std::uint32_t id=0x10001,handle=0x1000002;const auto record=pool+2*0xb0;
    std::map<std::uintptr_t,unsigned char> bytes;
    auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];};
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}return true;};
    put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x3130b0,std::uintptr_t{0x7000000});put(game+0x3130b8,entity);put(entity+0xa8,id);
    put(entity,base+0x1c886a8);put(entity+0x268,rig::List{entries,1,1<<8});put(entries,component);put(component,base+0x1c745c8);put(component+0x10,entity);put(component+0x18,rig::Identity{7,10});
    put(entity+0xf0,std::uintptr_t{0x8000000});put(0x8000000,base+0x1b0dd88);
    put(component+0xe8,base+0x1c74798);put(component+0xf8,handle);put(component+0xc8,std::uintptr_t{0});put(component+0xd2,static_cast<unsigned char>(0));
    put(game+0x60410,world);put(world+0x28,pool);put(world+0x30,std::uint32_t{3});
    rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=1;put(record,t);put(record+0x44,std::uint32_t{130});put(record+0x40,std::uint32_t{1});put(record+0x70,handle);put(record+0x74,handle);
    const auto body=native_rigid_body(read,base,entity);assert(body && body->identity.generation==7 && body->pose.position.x==1);
    put(entity,base+0x1c87d58);assert(native_rigid_body(read,base,entity)); // Exact idEntity owns visible standalone dynamics too.
    put(record+0x44,std::uint32_t{129});assert(!native_rigid_body(read,base,entity));
    put(record+0x44,std::uint32_t{134});assert(!native_rigid_body(read,base,entity));
    put(record+0x44,std::uint32_t{130});put(record+0x74,handle+1);assert(!native_rigid_body(read,base,entity));
    put(record+0x74,handle);put(component+0xc8,entity);assert(!native_rigid_body(read,base,entity));
    put(component+0xc8,std::uintptr_t{0});put(component+0x18,rig::Identity{-1,10});assert(!native_rigid_body(read,base,entity));
    put(component+0x18,rig::Identity{7,10});put(entity,base+0x1c5e258);assert(!native_rigid_body(read,base,entity)); // Unverified pickup class remains excluded.
    put(entity,base+0x1c886a8);
    put(record+0x44,std::uint32_t{138});assert(native_rigid_body(read,base,entity)); // Waking a sleeping dynamic prop preserves ownership.
    put(entity+0xf0,std::uintptr_t{0});assert(!native_rigid_body(read,base,entity));put(entity+0xf0,std::uintptr_t{0x8000000});
    put(record+0x44,std::uint32_t{129});assert(!native_rigid_body(read,base,entity)); // Static.
    put(record+0x44,std::uint32_t{134});assert(!native_rigid_body(read,base,entity)); // Keyframed.
    put(record+0x44,std::uint32_t{130});put(record+0x70,handle+1);assert(!native_rigid_body(read,base,entity)); // Recycled physics handle.
    put(record+0x70,handle);put(record+0x74,handle+1);assert(!native_rigid_body(read,base,entity)); // Compound primary differs.
    put(record+0x74,handle);put(component+0xc8,entity);assert(!native_rigid_body(read,base,entity)); // Native parent attachment.
    put(component+0xc8,std::uintptr_t{0});put(game+0x3130b8,std::uintptr_t{0});assert(!native_rigid_body(read,base,entity));
    put(game+0x3130b8,entity);put(component+0x18,rig::Identity{-1,10});assert(!native_rigid_body(read,base,entity));
    put(component+0x18,rig::Identity{7,10});
    std::array<float,8> bounds{.9f,-.1f,0,0,1.1f,.1f,2.8f,0};
    put(0x8000000+0x1840,bounds);put(0x8000000+0x1860,std::array<float,3>{1,0,0});
    const Vec3 surface{1,0,2.6};const auto tall=native_rigid_body(read,base,entity);
    assert(tall && tall->bounds && tall->contact_distance(surface)==0 && length(tall->pose.position-surface)>2);
    assert(std::abs(tall->contact_distance({1.25,0,2.6})-.15)<1e-6);
    assert(std::abs(tall->contact_distance({1.25,.25,2.6})-std::hypot(.15,.15))<1e-6);
    auto hand=tall->pose;hand.position=surface;PhysicalGrab grab;
    grab.sample(hand,0,true,1,100);grab.sample(hand,0,true,1,110);grab.sample(hand,.9f,true,1,120);
    assert(!grab.bind(tall->pose));assert(grab.bind(tall->pose,tall->contact(surface)));
    assert(length(grab.target().position-tall->pose.position)<1e-9); // Contact pickup preserves the distant native pivot.
    std::array<std::uintptr_t,0x3fff> registry{};registry[0]=0x7000000;registry[1]=entity;
    put(game+0x3130b0,registry);put(entity+0x300,std::array<float,3>{1,0,0});
    std::vector<NativeRigidBody> candidates;
    assert(nearest_grab_body(read,base,surface,.3));
    assert(nearest_grab_body(read,base,surface,2.,&candidates) && candidates.size()==1); // Cache includes surfaces whose pivots are beyond 2m.
    assert(!nearest_grab_body(read,base,surface,4.)); // Ordinary acquisition/cache scope remains bounded at 2m.
    assert(nearest_grab_body(read,base,surface,4.,nullptr,GrabSearchScope::ControlledFixture));
    assert(!nearest_grab_body(read,base,surface,4.1,nullptr,GrabSearchScope::ControlledFixture));
    bounds[4]=bounds[0]-1;put(0x8000000+0x1840,bounds);assert(!native_rigid_body(read,base,entity)->bounds);
    assert(!nearest_grab_body(read,base,surface,.3)); // Invalid geometry cannot expand acquisition.
    bounds[4]=1.1f;bounds[6]=4;put(0x8000000+0x1840,bounds);assert(!native_rigid_body(read,base,entity)->bounds);
    bounds[6]=2.8f;put(0x8000000+0x1840,bounds);put(0x8000000+0x1860,std::array<float,3>{2,0,0});
    assert(!native_rigid_body(read,base,entity)->bounds); // Stale render bounds after native body movement.
    put(0x8000000+0x1860,std::array<float,3>{1,0,0});bounds[2]=1;put(0x8000000+0x1840,bounds);
    assert(!native_rigid_body(read,base,entity)->bounds); // Unrelated box cannot supply a contact.
    put(component+0x18,rig::Identity{7,10});unsigned reads{};
    auto recycling=[&](std::uintptr_t at,void* out,std::size_t size) {const bool ok=read(at,out,size);if(at==component+0x18 && ++reads==1) put(component+0x18,rig::Identity{8,10});return ok;};
    assert(!native_rigid_body(recycling,base,entity));
    {   // a native carryable (Pickable_Simple beside the body) is left to the game's own Use
        const std::uintptr_t list2=0x7000000,pickable=0x7100000;
        put(entity+0x268,rig::List{list2,2,2<<8});put(list2,component);put(list2+8,pickable);put(pickable,base+0x1c5a3b8);
        assert(!native_rigid_body(read,base,entity));
        put(entity+0x268,rig::List{entries,1,1<<8});
    }
    {   // a guard's body: 17 ragdoll bodies listed by its physics system; only a dead or unconscious guard's limbs are grabbable
        const std::uintptr_t npc=0x9000000,list3=0x9100000,physics=0x9200000,handles=0x9300000,npc_pool=0x9400000;
        const std::uint32_t npc_id=0x20002;
        put(game+0x3130b0+2*8,npc);put(npc,base+0x1c5e108);put(npc+0xa8,npc_id);put(npc+0xd1,static_cast<unsigned char>(0x88));
        put(npc+0xf0,std::uintptr_t{0x8100000});put(npc+0x268,rig::List{list3,1,1<<8});put(list3,physics);
        put(physics,base+0x1c54008);put(physics+0x18,rig::Identity{3,10});put(physics+0x100,handles);
        put(world+0x28,npc_pool);put(world+0x30,std::uint32_t{20});
        for(std::uint32_t b=0;b<17;++b) {
            const std::uint32_t h=0x1000000+b;put(handles+0x120+b*4,h);put(handles+0x28,handles+0x120);put(handles+0x30,std::uint32_t{17});const auto rec=npc_pool+b*0xb0;
            rig::Transform bt{};bt.f[0]=bt.f[5]=bt.f[10]=bt.f[15]=1;bt.f[12]=static_cast<float>(b);
            put(rec,bt);put(rec+0x40,std::uint32_t{5+b});put(rec+0x44,std::uint32_t{0x82});put(rec+0x70,h);put(rec+0x74,h);
        }
        {   // read-only mass probe: 17 rows from the pool records and the motion array
            const std::uintptr_t motions=0x9900000;put(world+0x120,motions);
            for(std::uint32_t b=0;b<17;++b) {put(motions+(5+b)*0x80+0x26,static_cast<std::uint16_t>(0x3f00+b));put(motions+(5+b)*0x80+0x40,std::array<float,3>{0,0,0});} // 0x3f00 = inverse 0.5 (2 kg)
            std::size_t reads_before=0;auto counting=[&](std::uintptr_t at,void* out,std::size_t size) {++reads_before;return read(at,out,size);};
            const auto rows=probe_ragdoll_masses(counting,base,npc,77);
            assert(reads_before>0 && rows.size()==17);
            for(int b=0;b<17;++b) {
                const auto& r=rows[static_cast<std::size_t>(b)];
                assert(r.bone==b && r.tick==77 && r.entity_id==npc_id && r.handle==0x1000000u+static_cast<std::uint32_t>(b));
                assert(r.raw_inverse_half==0x3f00+b && r.record_flags==0x82 && r.class_kg==ragdoll_class_kg[static_cast<std::size_t>(b)]);
                assert(std::string(ragdoll_bone_name(r.bone))==ragdoll_bone_name(b));
            }
            assert(std::abs(rows[0].decoded_kg-2.0)<1e-9 && !rows[0].rejected && !rows[10].rejected);
            put(motions+(5+3)*0x80+0x26,std::uint16_t{0x7fc0});   // NaN half on the neck
            const auto bad=probe_ragdoll_masses(read,base,npc,78);assert(bad.size()==17 && bad[3].rejected && bad[3].decoded_kg==0);
            put(motions+(5+3)*0x80+0x26,static_cast<std::uint16_t>(0x3f03));
            put(npc_pool+5*0xb0+0x44,std::uint32_t{0xce});   // bone 5 no longer resolves: skipped
            const auto fewer=probe_ragdoll_masses(read,base,npc,79);assert(fewer.size()==16 && fewer[5].bone==6);
            put(npc_pool+5*0xb0+0x44,std::uint32_t{0x82});
            const auto prop_row=probe_prop_mass(read,*native_ragdoll_body(read,base,npc,10),80);
            assert(prop_row && prop_row->bone==10 && prop_row->raw_inverse_half==0x3f0a);
            const auto header=mass_probe_csv_header();
            for(const auto& r:rows) {
                const auto line=mass_probe_csv_row(r);
                assert(std::count(line.begin(),line.end(),',')==std::count(header.begin(),header.end(),','));
            }
            assert(mass_probe_csv_row(rows[2]).find("chest")!=std::string::npos);
        }
        const auto wrist=native_ragdoll_body(read,base,npc,10);
        assert(wrist && wrist->bone==10 && wrist->handle==0x100000a && wrist->pose.position.x==10 && wrist->bounds);
        assert(!native_ragdoll_body(read,base,npc,17));
        {   // the list's data may be a heap array (a woken corpse, sim v429), not the inline buffer
            const std::uintptr_t heap=0x9500000;for(std::uint32_t b=0;b<17;++b) put(heap+b*4,std::uint32_t{0x1000000+b});
            put(handles+0x28,heap);assert(native_ragdoll_body(read,base,npc,10) && native_ragdoll_body(read,base,npc,10)->handle==0x100000a);
            put(handles+0x30,std::uint32_t{16});assert(!native_ragdoll_body(read,base,npc,10)); // not a 17-body ragdoll list
            put(handles+0x30,std::uint32_t{17});put(handles+0x28,handles+0x120);
        }
        const auto nearest=nearest_ragdoll_body(read,base,npc,{12.9,0,0});
        assert(nearest && nearest->bone==13); // the left foot, not the torso
        assert(nearest_ragdoll_body(read,base,npc,{1,0,0})->bone==6); // near the pelvis only a limb is offered: the torso is the native carry
        assert(native_grab_body(read,base,*wrist) && native_grab_body(read,base,*wrist)->bone==10);
        put(physics+0x18,rig::Identity{3,0x4a}); // flags change while pushed (still a started component)
        {const auto pushed=native_grab_body(read,base,*wrist);assert(pushed && pushed->identity==wrist->identity);}
        put(physics+0x18,rig::Identity{4,10}); // recycled component
        {const auto recycled=native_grab_body(read,base,*wrist);assert(recycled && recycled->identity!=wrist->identity);}
        put(physics+0x18,rig::Identity{3,10});
        put(npc+0xd1,static_cast<unsigned char>(0x80));assert(native_ragdoll_body(read,base,npc,10)); // alive but ragdolling (yanked): dynamic bodies
        put(npc_pool+10*0xb0+0x44,std::uint32_t{0xce});assert(!native_ragdoll_body(read,base,npc,10)); // alive: bodies follow the animation (keyframed)
        put(npc_pool+10*0xb0+0x44,std::uint32_t{0x82});
        put(npc+0xd1,static_cast<unsigned char>(0xa0));assert(native_ragdoll_body(read,base,npc,10)); // unconscious
        put(npc_pool+10*0xb0+0x44,std::uint32_t{0x8e});assert(!native_ragdoll_body(read,base,npc,10)); // keyframed body
        put(npc_pool+10*0xb0+0x44,std::uint32_t{0x82});put(npc,base+0x1c886a8);assert(!native_ragdoll_body(read,base,npc,10)); // not a guard
        put(npc,base+0x1c5e108);
        {   // physical hands I1: the clamp's exclusion of a met dead/unconscious guard is EXACTLY the body the contact service met (its bone and handle,
            // any of the 17: a torso push too), validated again here; mapping props alone, or a limb nearest the hand, dropped or misplaced the sweep
            for(std::uint32_t b=0;b<17;++b) {const auto rec=npc_pool+b*0xb0;const std::uintptr_t ud=0x9600000+b*0x100,comp=0x9700000+b*0x100;put(rec+0x90,ud);put(ud+8,comp);put(comp+0x10,npc);}
            put(physics+0xc8,std::array<unsigned char,12>{});
            put(npc_pool+3*0xb0+0x6c,std::uint32_t{0x00ff0004});
            const auto torso=native_cast_exclusion(read,base,world,npc,3,0x1000003,false);   // bone 3 is no grab bone
            assert(torso && torso->handle==0x1000003 && torso->record==npc_pool+3*0xb0 && torso->filter_info==0x00ff0004);
            const auto zero=native_cast_exclusion(read,base,world,npc,3,0x1000003,true);assert(zero && zero->handle==0x1000003 && zero->filter_info==0);
            assert(!native_cast_exclusion(read,base,world,npc,3,0x1000004,false));   // not the handle the service met (the list changed): unmapped
            assert(!native_cast_exclusion(read,base,world+8,npc,3,0x1000003,false)); // another world
            assert(!native_cast_exclusion(read,base,world,npc,-1,0x1000003,false));  // no bone published: unmapped (the caller casts without exclusion)
            {   // two hands on one corpse: each side keeps its own met body (the cache is keyed by side, and by the body, not just the entity)
                NativeExclusionCache cache;unsigned maps=0;put(npc_pool+10*0xb0+0x6c,std::uint32_t{0x00ff0008});
                const auto left=cache.get(npc,0,3,0x1000003,[&] {++maps;return native_cast_exclusion(read,base,world,npc,3,0x1000003,false);});
                const auto right=cache.get(npc,1,10,0x100000a,[&] {++maps;return native_cast_exclusion(read,base,world,npc,10,0x100000a,false);});
                assert(maps==2 && left && right && left->handle==0x1000003 && right->handle==0x100000a);   // the second hand did not reuse the first's body
                assert(cache.get(npc,0,3,0x1000003,[&] {++maps;return std::optional<NativeCastExclusion>{};})->handle==0x1000003 && maps==2);   // cached
                const auto failed=cache.get(npc,1,4,0x1000004,[&] {++maps;return native_cast_exclusion(read,base,world,npc,4,0x1000099,false);});
                assert(!failed && maps==3);cache.get(npc,1,4,0x1000004,[&] {++maps;return std::optional<NativeCastExclusion>{};});assert(maps==3);   // a failure is cached as well
            }
            put(0x9600000+3*0x100+8,std::uintptr_t{0});
            {const std::uintptr_t other=0x9800000;put(0x9700000+3*0x100+0x10,other);put(0x9600000+3*0x100+8,std::uintptr_t{0x9700000+3*0x100});
             assert(!native_cast_exclusion(read,base,world,npc,3,0x1000003,false));   // the body now belongs to another entity: not excluded
             put(0x9700000+3*0x100+0x10,npc);}
            put(0x9600000+3*0x100+8,std::uintptr_t{0x9700000+3*0x100});
            assert(native_cast_exclusion(read,base,world,npc,3,0x1000003,false));
            put(physics+0xc8,std::uintptr_t{0x1234});assert(!native_cast_exclusion(read,base,world,npc,3,0x1000003,false)); // carried: not excluded
            put(physics+0xc8,std::array<unsigned char,12>{});
            put(npc_pool+3*0xb0+0x44,std::uint32_t{0xce});assert(!native_cast_exclusion(read,base,world,npc,3,0x1000003,false)); // keyframed: awake guard
            put(npc_pool+3*0xb0+0x44,std::uint32_t{0x82});
            put(npc,base+0x1c886a8);assert(!native_cast_exclusion(read,base,world,npc,3,0x1000003,false)); // not a guard
            put(npc,base+0x1c5e108);
        }
        {   // a prop still maps by its own body
            put(world+0x28,pool);put(world+0x30,std::uint32_t{3});
            put(record+0x6c,std::uint32_t{0x00ff0008});
            const auto prop=native_cast_exclusion(read,base,world,entity,-1,handle,false);
            assert(prop && prop->record==record && prop->handle==handle && prop->filter_info==0x00ff0008);
            put(record+0x6c,std::uint32_t{0});const auto none=native_cast_exclusion(read,base,world,entity,-1,handle,false);assert(!none); // no filter word and not told to cast with zero: unmapped
            assert(native_cast_exclusion(read,base,world,entity,-1,handle,true));
        }
    }
    std::cout<<"PASS: native dynamic-body registry/component/handle ownership; static/keyframed/compound/attached/recycled/native-carryable exclusion; guard ragdoll limbs\n";
}
