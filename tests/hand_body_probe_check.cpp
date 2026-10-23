// Plan B Task B7: the read-only hand-body probe's pure parts (filter word split, CSV shape, userdata chain, garbage pointers, unresolved bodies).
#include "../src/hand_body_probe.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>

namespace {
std::size_t commas(const std::string& text) {std::size_t n=0;for(const char c:text) if(c==',') ++n;return n;}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::size_t largest_read{};
    template<class T> void put(std::uintptr_t at,const T& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i) bytes[at+i]=p[i];}
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        largest_read=std::max(largest_read,size);
        auto* p=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}
        return true;
    }
};
}

int main() {
    using namespace d2vr;
    const std::uintptr_t base=0x140000000,world=0x5000000,pool=0x6000000;
    // filter_word_split: layer in the low 5 bits, mask in the upper 27.
    {
        const FilterWord word{0xffffffe3u};
        assert(word.layer()==3);assert(word.mask()==0x07ffffffu);
        assert((FilterWord{0x000887e4u}.layer()==4) && FilterWord{0x000887e4u}.mask()==0x000887e4u>>5);
        assert(FilterWord{6}.layer()==6 && FilterWord{6}.mask()==0);
    }
    // csv_row_field_count_matches_header
    {
        BodyProbeRow row;row.tick=3;row.handle=0x1000002;row.filter=0x000887e4u;row.flags=130;row.userdata=0x7000000;row.userdata_plus8=0x7100000;row.userdata_vtable_rva=0x1aff038;row.what="prop";
        assert(commas(body_probe_csv_row(row))==commas(body_probe_csv_header()));
        row.what=nullptr;assert(commas(body_probe_csv_row(row))==commas(body_probe_csv_header()));
        QueueProbe q;q.callbacks=7;
        assert(commas(queue_probe_csv_row(1,q,7,"t9:3;t2:4"))==commas(queue_probe_csv_header()));
    }
    // probe_prop_userdata_reads_fixture
    {
        Memory m;
        const std::uintptr_t record=pool+2*0xb0,userdata=0x7000000,owner=0x7100000,entity=0x7200000;
        m.put(record+0x6c,std::uint32_t{0x000887e4u});m.put(record+0x90,userdata);
        m.put(userdata,base+0x1aff038);m.put(userdata+8,owner);
        m.put(owner,base+0x1c74578);m.put(owner+0x10,entity);
        m.put(entity,base+0x1c886a8);m.put(entity+0xa8,std::uint32_t{0x10001});
        NativeRigidBody body;body.record=record;body.handle=0x1000002;body.flags=130;
        const auto row=probe_prop_userdata(m,base,body,5);
        assert(row && row->userdata==userdata && row->userdata_plus8==owner && row->userdata_vtable_rva==0x1aff038);
        assert(row->owner_vtable_rva==0x1c74578 && row->entity==entity && row->entity_vtable_rva==0x1c886a8 && row->entity_id==0x10001);
        assert(row->filter==0x000887e4u && row->handle==0x1000002 && row->flags==130 && row->tick==5 && std::string(row->what)=="prop");
        assert(!row->typeinfo_decoded && !row->owner_is_player); // owner vtable[0] is not mapped in this fixture
        // G2: owner vtable slot 0 is `lea rax,[rip+rel32]; ret`; typeinfo+0x60 is the typenum; the live range sits at RVA 0x39e8610/14.
        {
            const std::uintptr_t stub=base+0x300000,typeinfo=base+0x400000;
            const std::int32_t rel=static_cast<std::int32_t>(typeinfo-(stub+7));
            std::array<unsigned char,8> code{0x48,0x8d,0x05,0,0,0,0,0xc3};std::memcpy(&code[3],&rel,4);
            m.put(stub,code);m.put(base+0x1c74578,stub);m.put(typeinfo+0x60,std::uint32_t{0x120});
            m.put(base+0x39e8610,std::uint32_t{0x100});m.put(base+0x39e8614,std::uint32_t{0x1ff});
            BodyProbeRow g;g.userdata=userdata;hand_body_probe_detail::fill_chain(m,base,g,entity);
            assert(g.typeinfo_decoded && g.owner_typenum==0x120 && g.type_in_range && g.type_range_lo==0x100 && g.type_range_hi==0x1ff && g.owner_is_player);
            m.put(typeinfo+0x60,std::uint32_t{0x20});hand_body_probe_detail::fill_chain(m,base,g,0x1);
            assert(g.typeinfo_decoded && !g.type_in_range && !g.owner_is_player);
            code[7]=0x90;m.put(stub,code);hand_body_probe_detail::fill_chain(m,base,g,entity);assert(!g.typeinfo_decoded);
        }
        assert(m.largest_read<=64);
        // A vtable outside the image reports RVA 0.
        m.put(userdata,std::uintptr_t{0x9000000});
        assert(probe_prop_userdata(m,base,body,5)->userdata_vtable_rva==0);
        // Null userdata: a row of zeros, no crash.
        m.put(record+0x90,std::uintptr_t{0});
        const auto empty=probe_prop_userdata(m,base,body,5);
        assert(empty && empty->userdata==0 && empty->userdata_plus8==0 && empty->userdata_vtable_rva==0 && empty->entity==0 && empty->filter==0x000887e4u);
        // Unreadable record: no row.
        body.record=0x1234560;assert(!probe_prop_userdata(m,base,body,5));
    }
    // garbage_pointers_rejected: outside user space, misaligned, crossing a page: nothing beyond the record is read.
    {
        const std::uintptr_t record=pool+2*0xb0;
        NativeRigidBody body;body.record=record;body.handle=1;body.flags=2;
        for(const std::uintptr_t bad:{std::uintptr_t{0xffff800000001000},std::uintptr_t{0x8},std::uintptr_t{0x7000004},std::uintptr_t{0x7000ff8}}) {
            Memory m;
            m.put(record+0x6c,std::uint32_t{7});m.put(record+0x90,bad);
            // The bytes exist, so a stray read would succeed and show up as non-zero output.
            m.put(bad,std::uintptr_t{base+0x1aff038});m.put(bad+8,std::uintptr_t{0x7100000});m.put(bad+16,std::uintptr_t{0});
            const auto row=probe_prop_userdata(m,base,body,1);
            assert(row && row->userdata==bad && row->userdata_plus8==0 && row->userdata_vtable_rva==0 && row->entity==0);
        }
        // A garbage owner is not followed but the pointer is kept.
        Memory m;m.put(record+0x6c,std::uint32_t{7});m.put(record+0x90,std::uintptr_t{0x7000000});
        m.put(std::uintptr_t{0x7000000},base+0x1aff038);m.put(std::uintptr_t{0x7000008},std::uintptr_t{0x7100004});
        const auto row=probe_prop_userdata(m,base,body,1);
        assert(row && row->userdata_plus8==0x7100004 && row->owner_vtable_rva==0 && row->entity==0);
    }
    // probe_player_bodies_skips_unresolved
    {
        Memory empty;
        assert(probe_player_bodies(empty,base,1,0x1000).empty());
        assert(probe_player_bodies(empty,base,1).empty());
        Memory m;
        const std::uintptr_t physics=0x8000000,proxy_object=0x8100000,ud=0x7000000;
        m.put(world+0x28,pool);m.put(world+0x30,std::uint32_t{4});
        m.put(physics+0x218,std::uint32_t{0x00ff7fe6u});m.put(physics+0x21c,std::uint32_t{5});
        m.put(physics+0x210,std::uint32_t{0xffffffffu}); // second body not created yet: skipped
        m.put(physics+0x148,proxy_object);m.put(proxy_object+0x38,std::uint32_t{0x2000001});
        const std::uintptr_t record=pool+1*0xb0;
        m.put(record+0x70,std::uint32_t{0x2000001});m.put(record+0x6c,std::uint32_t{0x00ff7fe6u});m.put(record+0x44,std::uint32_t{130});m.put(record+0x90,ud);
        m.put(ud,base+0x1b02768);m.put(ud+8,std::uintptr_t{0});
        const auto rows=probe_physics_bodies(m,base,world,physics,0x3000003u,9); // query handle 3 has no record: skipped
        assert(rows.size()==2);
        assert(std::string(rows[0].what)=="player_cfg_filter" && rows[0].filter==0x00ff7fe6u && FilterWord{rows[0].filter}.layer()==6);
        assert(std::string(rows[1].what)=="player_proxy_body" && rows[1].handle==0x2000001 && rows[1].userdata_vtable_rva==0x1b02768 && rows[1].userdata_plus8==0);
        // A recycled handle (record stores another) does not resolve.
        m.put(record+0x70,std::uint32_t{0x2000009});
        assert(probe_physics_bodies(m,base,world,physics,0,9).size()==1);
        assert(probe_physics_bodies(m,base,0,physics,0,9).empty());
    }
    // world filter vtable and busy flag reads (no engine call).
    {
        Memory m;const std::uintptr_t filter=0x9000000,game=0x1000000,object=0x1100000;
        assert(world_filter_vtable_rva(m,base,world)==0);
        m.put(world+0x470,filter);m.put(filter,base+0x1ad4d98);
        assert(world_filter_vtable_rva(m,base,world)==0x1ad4d98);
        m.put(base+0x39a5290,game);m.put(game+0x337121,std::uint8_t{0});m.put(game+0x337122,std::uint8_t{0});m.put(game+0x1e8960,object);m.put(object+0xc34,std::uint8_t{0});
        assert(physics_busy_flag(m,base) && !*physics_busy_flag(m,base));
        m.put(object+0xc34,std::uint8_t{1});assert(*physics_busy_flag(m,base));
        m.put(object+0xc34,std::uint8_t{0});m.put(game+0x337122,std::uint8_t{1});assert(*physics_busy_flag(m,base));
        Memory none;assert(!physics_busy_flag(none,base));
        HandBodyProbeCounters::reset();
        m.put(std::uintptr_t{0xa000004},std::uint16_t{9});
        HandBodyProbeCounters::note_stage0(m,base,11);
        HandBodyProbeCounters::note_dispatch(m,base,0xa000000,12);
        const auto q=HandBodyProbeCounters::snapshot();
        assert(q.callbacks==1 && q.dispatch_samples==1 && q.dispatch_busy==1 && q.stage0_samples==1 && q.off_stage0_thread==1);
        assert(HandBodyProbeCounters::record_type_text()=="t9:1");
        HandBodyProbeCounters::reset();
    }
    std::cout<<"hand_body_probe_check ok\n";
    return 0;
}
