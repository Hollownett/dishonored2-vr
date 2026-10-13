#define NOMINMAX
#include "../src/native_marker_child_binding.hpp"
#include <map>
#include <cassert>
#include <iostream>
using namespace d2vr;
constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x100000,fixture_marker=0x200000,fixture_manager=0x300000,fixture_list=0x400000,fixture_entity=0x500000,fixture_file=0x600000,fixture_iggy=0x700000,fixture_table=0x800000,fixture_boxed=0x900000,fixture_node=0xa00000,fixture_path=0xb00000;
std::map<std::uintptr_t,std::byte> memory;unsigned calls{};bool race{};
template<class T> void put(std::uintptr_t at,T value) {const auto* p=reinterpret_cast<const std::byte*>(&value);for(std::size_t i=0;i<sizeof(value);++i)memory[at+i]=p[i];}
bool read(std::uintptr_t at,void* out,std::size_t size) {auto* p=static_cast<std::byte*>(out);for(std::size_t i=0;i<size;++i){const auto it=memory.find(at+i);if(it==memory.end())return false;p[i]=it->second;}return true;}
std::uint64_t __fastcall get(const void* record,std::uint64_t,const char* field,void*,const char*,std::uint32_t* status) {
    assert(reinterpret_cast<std::uintptr_t>(record)==fixture_table);++calls;*status=0;
    if(!field) return fixture_boxed+1;
    if(!std::strcmp(field,"visible")) {if(race)put(fixture_marker+0xd4,1u);return fixture_base+0x2424049;}
    if(!std::strcmp(field,"x")) return 20;
    if(!std::strcmp(field,"y")) return 40;
    if(!std::strcmp(field,"width")) return 64;
    if(!std::strcmp(field,"height")) return 48;
    assert(false);return 7;
}
std::uint32_t __fastcall draw(void*,float*,void*,void*,void*,void*,std::uint32_t,float) {return 19;}
int main() {
    put(fixture_base+0x39a5290,fixture_game);put(fixture_game,fixture_base+0x1c940a8);put(fixture_marker+0x78,fixture_manager);put(fixture_marker+0xd4,0u);put(fixture_marker+0xc,0x10001u);
    put(fixture_marker+0x640,static_cast<unsigned char>(0));put(fixture_table,std::array<std::byte,40>{});
    put(fixture_marker+0x94,static_cast<unsigned char>(1));put(fixture_marker+0x96,static_cast<unsigned char>(0));put(fixture_marker+0x3c,std::array<float,3>{10,20,30});
    put(fixture_manager+0x18,fixture_list);put(fixture_manager+0x20,1u);put(fixture_list,fixture_marker);put(fixture_game+0x3130b0+8,fixture_entity);put(fixture_entity+0xa8,0x10001u);
    put(fixture_file+0x1608,fixture_iggy);put(fixture_file+0x1658,fixture_path);std::array<char,64> name{};std::memcpy(name.data(),"ui/hud.iggy",12);put(fixture_path,name);
    put(fixture_marker+0xf8,fixture_iggy);put(fixture_marker+0x108,fixture_table);put(fixture_marker+0x110,1u);put(fixture_boxed,static_cast<unsigned char>(0x24));put(fixture_boxed+0x28,fixture_node);put(fixture_node+0x80,fixture_iggy+0x26b0);
    NativeWristHud::observe_draw(fixture_iggy,GetTickCount64());NativeHudChildDraw::fixture(read,fixture_base,draw);
    const NativeInteractionPrompt::Api api{nullptr,nullptr,get};
    assert(!NativeMarkerChildBinding::observe(read,fixture_base,0,fixture_marker,fixture_file,api) && calls==0);
    const auto packet=NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api);
    assert(packet && packet->geometry.display_node==fixture_node && packet->world.x==10 && packet->geometry.fields==31 && calls==6);
    assert(NativeMarkerChildBinding::publish(*packet,3,GetTickCount64()));
    put(fixture_marker+0x3c,std::array<float,3>{11,22,33});
    const auto refreshed=NativeMarkerChildBinding::refresh(read,fixture_base,0x53aaee,fixture_marker,fixture_file,*packet);
    assert(refreshed && refreshed->world.x==11 && refreshed->qualified_tick==packet->qualified_tick && calls==6);
    put(fixture_marker+0x640,static_cast<unsigned char>(1));
    assert(!NativeMarkerChildBinding::refresh(read,fixture_base,0x53aaee,fixture_marker,fixture_file,*packet));
    assert(!NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api) && calls==6);
    // Clamped/offscreen markers resolve the alternate +398 receiver, never +F0.
    put(fixture_marker+0x398+8,fixture_iggy);put(fixture_marker+0x398+0x18,fixture_table);put(fixture_marker+0x398+0x20,1u);
    put(fixture_marker+0xf8,std::uintptr_t{0});
    const auto edge=NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api);
    assert(edge && edge->offscreen && edge->geometry.display_node==fixture_node && edge->world.x==11);
    assert(NativeMarkerChildBinding::refresh(read,fixture_base,0x53aaee,fixture_marker,fixture_file,*edge));
    put(fixture_marker+0xf8,fixture_iggy);put(fixture_marker+0x640,static_cast<unsigned char>(2));
    assert(!NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api));
    put(fixture_marker+0x640,static_cast<unsigned char>(1));
    put(fixture_marker+0x640,static_cast<unsigned char>(0));
    put(fixture_table,std::array<std::byte,40>{std::byte{1}});
    assert(!NativeMarkerChildBinding::refresh(read,fixture_base,0x53aaee,fixture_marker,fixture_file,*packet));
    put(fixture_table,std::array<std::byte,40>{});
    auto expired=*packet;expired.qualified_tick=GetTickCount64()-251;
    assert(!NativeMarkerChildBinding::refresh(read,fixture_base,0x53aaee,fixture_marker,fixture_file,expired));
    assert(!NativeMarkerChildBinding::publish(expired,3,GetTickCount64()));
    NativeMarkerWorldFrame frame{{{100,200,300},{{{1,0,0},{0,1,0},{0,0,1}}}},{{0,1.7,0},{}},{0,1.7,0},3,42,1000};
    const auto placed=native_marker_world_point({102,200,300},frame,3,42,1000,1000);
    assert(placed && std::abs(placed->z+2)<1e-9 && std::abs(placed->y-1.7)<1e-9);
    assert(!native_marker_world_point({102,200,300},frame,3,43,1000,1000));
    assert(!native_marker_world_point({102,200,300},frame,4,42,1000,1000));
    assert(!native_marker_world_point({102,200,300},frame,3,42,749,1000));
    put(fixture_marker+0xc,0x20001u);assert(!NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api));put(fixture_marker+0xc,0x10001u);
    put(fixture_node+0x80,fixture_node);assert(!NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api));put(fixture_node+0x80,fixture_iggy+0x26b0);
    race=true;assert(!NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api));race=false;put(fixture_marker+0xd4,0u);
    put(fixture_marker+0x96,static_cast<unsigned char>(1));assert(!NativeMarkerChildBinding::observe(read,fixture_base,0x53aaee,fixture_marker,fixture_file,api));
    std::cout<<"PASS: individual fixture_marker slot/fixture_node/world identity, native geometry getters, stale spawn, wrong tree, read race and detached rejection\n";
}
