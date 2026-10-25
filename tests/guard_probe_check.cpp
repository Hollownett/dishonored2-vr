#include "../src/guard_probe.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
std::size_t commas(const std::string& s) {std::size_t n=0;for(const char c:s) n+=c==',';return n;}
struct Fixture {
    const std::uintptr_t base=0x140000000,game=0x1000000,npc=0x9000000,list3=0x9100000,physics=0x9200000,handles=0x9300000,npc_pool=0x9400000,world=0x5000000,model=0x8100000;
    const std::uint32_t npc_id=0x20002;
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T> void put(std::uintptr_t at,const T& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];}
    bool read(std::uintptr_t at,void* out,std::size_t size) {auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}return true;}
    Fixture() {
        put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x3130b0+2*8,npc);
        put(npc,base+0x1c5e108);put(npc+0xa8,npc_id);put(npc+0xd1,static_cast<unsigned char>(0x01));
        put(npc+0xf0,model);put(model+0x1860,std::array<float,3>{1.5f,2.5f,3.5f});
        put(model+0x186c,std::array<float,9>{0,1,0,-1,0,0,0,0,1});   // model axes, row 0 = forward (0,1,0): yaw 90 deg
        put(npc+0x268,rig::List{list3,1,1<<8});put(list3,physics);
        put(physics,base+0x1c54008);put(physics+0x18,rig::Identity{3,10});put(physics+0x100,handles);
        put(game+0x60410,world);put(world+0x28,npc_pool);put(world+0x30,std::uint32_t{20});
        put(handles+0x28,handles+0x120);put(handles+0x30,std::uint32_t{17});
        for(std::uint32_t b=0;b<17;++b) {
            const std::uint32_t h=0x1000000+b;put(handles+0x120+b*4,h);const auto rec=npc_pool+b*0xb0;
            rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;put(rec,t);put(rec+0x40,std::uint32_t{5+b});put(rec+0x44,std::uint32_t{0x82});put(rec+0x70,h);put(rec+0x74,h);
        }
        bodies(false);
    }
    void bodies(bool dynamic) {put(npc_pool+0x44,std::uint32_t{dynamic?0x82u:0x86u});} // flags & 7: 2 dynamic, 6 keyframed
};
}
int main() {
    {   // parse_accepts_three_forms
        auto a=parse_guard_probe("shove\n");CHECK(a && a->experiment==GuardExperiment::shove);
        auto b=parse_guard_probe("shove_decl shove_damage\n");CHECK(b && b->experiment==GuardExperiment::shove_decl && b->decl=="shove_damage");
        auto c=parse_guard_probe("teleport_step 0.1 5");CHECK(c && c->experiment==GuardExperiment::teleport_step && c->step_m==0.1 && c->steps==5);
        auto d=parse_guard_probe("  teleport_step   0.5   20  \r\n");CHECK(d && d->steps==20 && d->step_m==0.5);
    }
    {   // parse_rejects_garbage_and_bounds
        for(const char* bad:{"","   ","\n","nothing","shove now","shove_decl","shove_decl A/B","shove_decl a b","shove_decl ../x","teleport_step","teleport_step 0.1","teleport_step 0.1 5 6",
                "teleport_step 0.51 5","teleport_step 0.1 21","teleport_step 0 5","teleport_step -0.1 5","teleport_step 0.1 0","teleport_step 0.1 -1","teleport_step nan 5","teleport_step inf 5",
                "teleport_step 0.1x 5","teleport_step 0.1 5.5","SHOVE"})
            CHECK(!parse_guard_probe(bad));
        CHECK(!parse_guard_probe("shove_decl "+std::string(65,'a')));
        CHECK(parse_guard_probe("shove_decl "+std::string(64,'a')));
    }
    {   // csv_row_field_count_matches_header
        GuardSample s;s.tick=12345;s.entity_id=0x20002;s.position={1.5,-2.25,3};s.state_d1=0x20;s.unaware=true;s.bodies_dynamic=true;s.read_ok=true;s.unaware_ok=true;
        s.yaw_deg=-179.5;
        const auto row=guard_sample_csv_row(s);
        CHECK(commas(row)==commas(guard_sample_csv_header()) && commas(row)==10);
        CHECK(row=="12345,131074,1.5000,-2.2500,3.0000,32,1,1,1,1,-179.50");
        CHECK(guard_sample_csv_header()=="tick_ms,entity_id,x,y,z,state_d1,unaware,bodies_dynamic,read_ok,unaware_ok,yaw_deg");
        CHECK(commas(guard_sample_csv_row(GuardSample{}))==10);
    }
    {   // yaw_wrap_reads_as_small_change (Task 13): -179 to 179 is a 2 degree turn, not 358 (the "no yaw pop" rule compares steps)
        CHECK(std::abs(guard_yaw_change(-179,179)-2)<1e-9 && std::abs(guard_yaw_change(179,-179)-2)<1e-9);
        CHECK(std::abs(guard_yaw_change(10,25)-15)<1e-9 && std::abs(guard_yaw_change(90,-90)-180)<1e-9 && guard_yaw_change(0,0)==0);
        CHECK(std::abs(guard_yaw_change(-170,175)-15)<1e-9);
    }
    {   // sample_garbage_read_is_marked_not_ok
        Fixture f;auto read=[&](std::uintptr_t at,void* out,std::size_t n){return f.read(at,out,n);};
        int asked=0;auto ask=[&](std::uintptr_t)->std::optional<bool> {++asked;return true;};
        auto good=sample_guard(read,f.base,f.npc,7,ask);CHECK(good.read_ok && good.entity_id==f.npc_id && good.position.x==1.5 && good.position.z==3.5 && good.unaware && good.unaware_ok && asked==1 && good.tick==7);
        CHECK(std::abs(good.yaw_deg-90)<1e-4);   // atan2 of row 0 in the ground plane
        f.put(f.model+0x186c,std::array<float,9>{-1,-.0001f,0,0,-1,0,0,0,1});CHECK(std::abs(std::abs(sample_guard(read,f.base,f.npc,7,ask).yaw_deg)-180)<.01);
        f.put(f.model+0x186c,std::array<float,9>{0,1,0,-1,0,0,0,0,1});
        f.put(f.npc+0xf0,std::uintptr_t{0xdead0001}); // an unreadable (and misaligned) model pointer
        asked=0;auto bad=sample_guard(read,f.base,f.npc,8,ask);CHECK(!bad.read_ok && !bad.unaware && !bad.bodies_dynamic && bad.position.x==0 && asked==0);
        f.put(f.npc+0xf0,std::uintptr_t{0x7ff000000000});bad=sample_guard(read,f.base,f.npc,8,ask);CHECK(!bad.read_ok); // aligned, unmapped
        f.put(f.npc+0xf0,f.model);f.put(f.model+0x1860,std::array<float,3>{NAN,0,0});CHECK(!sample_guard(read,f.base,f.npc,9,ask).read_ok);
        CHECK(!sample_guard(read,f.base,0,9,ask).read_ok && !sample_guard(read,f.base,0x123,9,ask).read_ok);
        auto none=[&](std::uintptr_t at,void*,std::size_t){(void)at;return false;};
        CHECK(!sample_guard(none,f.base,f.npc,9,ask).read_ok);
    }
    {   // a stealth test that did not run is neither aware nor unaware
        Fixture f;auto read=[&](std::uintptr_t at,void* out,std::size_t n){return f.read(at,out,n);};
        auto did_not_run=[&](std::uintptr_t)->std::optional<bool> {return std::nullopt;};
        auto s=sample_guard(read,f.base,f.npc,1,did_not_run);CHECK(s.read_ok && !s.unaware && !s.unaware_ok);
        auto aware=[&](std::uintptr_t)->std::optional<bool> {return false;};
        s=sample_guard(read,f.base,f.npc,1,aware);CHECK(s.unaware_ok && !s.unaware);
    }
    {   // sample_flags_ragdoll_bodies
        Fixture f;auto read=[&](std::uintptr_t at,void* out,std::size_t n){return f.read(at,out,n);};
        auto ask=[&](std::uintptr_t)->std::optional<bool> {return false;};
        auto s=sample_guard(read,f.base,f.npc,1,ask);CHECK(s.read_ok && !s.bodies_dynamic && !s.unaware && s.state_d1==1);
        f.bodies(true);s=sample_guard(read,f.base,f.npc,2,ask);CHECK(s.read_ok && s.bodies_dynamic);
        f.bodies(false);CHECK(!sample_guard(read,f.base,f.npc,3,ask).bodies_dynamic);
    }
    std::puts("guard_probe_check passed");
}
