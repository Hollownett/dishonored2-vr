#define NOMINMAX
#include "../src/note_hand_mesh.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>

using namespace d2vr;
namespace nh=d2vr::note_hand;
template<class T> static void put(std::ostream& out,const T& value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));}
static std::string pack(const nh::Mesh& mesh) {
    std::ostringstream out(std::ios::binary);
    put(out,std::array<char,8>{'D','2','V','R','N','H','0','1'});put(out,1u);put(out,mesh.character);put(out,mesh.side);
    put(out,mesh.source_sha256);put(out,static_cast<std::uint32_t>(mesh.resource.size()));out.write(mesh.resource.data(),mesh.resource.size());put(out,mesh.inverse);
    put(out,static_cast<std::uint32_t>(mesh.surfaces.size()));
    for(const auto& s:mesh.surfaces) {
        put(out,s.kind);put(out,static_cast<std::uint32_t>(s.vertices.size()));put(out,static_cast<std::uint32_t>(s.indices.size()));
        for(const auto& v:s.vertices)put(out,v);for(auto i:s.indices)put(out,i);
    }
    return out.str();
}
static bool rejects(const std::string& bytes) {try {std::istringstream in(bytes,std::ios::binary);(void)nh::read(in);return false;}catch(const std::invalid_argument&){return true;}}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i) {std::ifstream file(argv[i],std::ios::binary);const auto installed=nh::read(file);std::size_t vertices{},triangles{};for(const auto& s:installed.surfaces){vertices+=s.vertices.size();triangles+=s.indices.size()/3;}std::cout<<"PACK "<<installed.resource<<" vertices "<<vertices<<" triangles "<<triangles<<"\n";}
    nh::Mesh source;source.resource="models/characters/player/corvo/left_arm.md6";source.source_sha256[0]=1;
    for(auto& b:source.inverse)b={1,0,0,0,0,1,0,0,0,0,1,0};
    nh::Surface surface;surface.kind=1;
    for(unsigned i=0;i<3;++i) {nh::Vertex v;v.position={static_cast<float>(i),0,0};v.weights[0]=128;v.weights[1]=127;v.joints[1]=1;surface.vertices.push_back(v);}
    surface.indices={0,1,2};source.surfaces.push_back(surface);
    const auto bytes=pack(source);std::istringstream in(bytes,std::ios::binary);const auto mesh=nh::read(in);
    assert(mesh.resource==source.resource && mesh.surfaces[0].indices==surface.indices);
    assert(rejects(bytes.substr(0,bytes.size()-1)) && rejects(bytes+"x"));
    auto bad=source;bad.surfaces[0].vertices[0].weights[0]=0;assert(rejects(pack(bad)));
    bad=source;bad.surfaces[0].vertices[0].joints[0]=69;assert(rejects(pack(bad)));
    bad=source;bad.surfaces[0].indices[0]=3;assert(rejects(pack(bad)));
    bad=source;bad.inverse[0][0]=NAN;assert(rejects(pack(bad)));
    bad=source;bad.side=1;assert(rejects(pack(bad)));
    bad=source;bad.source_sha256={};assert(rejects(pack(bad)));
    rig::ArmBinding binding{};binding.inverse=source.inverse;
    rig::AvatarMeshToken token{0x10000,0x20000,{{0x30000,1,3,1}}};
    assert(nh::matches(mesh,binding,token));binding.inverse[0][3]=.001f;assert(!nh::matches(mesh,binding,token));binding.inverse=source.inverse;
    token.surfaces[0].triangles=2;assert(!nh::matches(mesh,binding,token));token.surfaces[0].triangles=1;
    // The exact logical arm and primary mesh resource must own the shape.
    std::vector<unsigned char> memory(0x40000);
    const auto write=[&](std::uintptr_t at,const auto& v){std::memcpy(memory.data()+at,&v,sizeof(v));};
    const auto text=[&](std::uintptr_t at,const std::string& v){std::memcpy(memory.data()+at,v.c_str(),v.size()+1);};
    auto reader=[&](std::uintptr_t at,void* out,std::size_t n){if(at>=memory.size() || n>memory.size()-at)return false;std::memcpy(out,memory.data()+at,n);return true;};
    rig::Arm arm{};arm.model=0x10000;arm.skeleton=0x18000;
    const std::uintptr_t module=0x140000000;
    write(arm.model+0x1fe8,std::uintptr_t{0x14000});write(0x14050,arm.skeleton);write(0x14008,std::uintptr_t{0x30000});write(0x14068,std::uintptr_t{0x1c000});
    write(arm.model+0x1bb0,std::uintptr_t{0x30000});write(0x1c000,rig::List{0x20000,1,256});write(0x20000,std::uintptr_t{0x24000});
    write(0x24000,module+0x1b0d118);write(0x24058,rig::List{0x28000,1,256});write(0x28000,std::uintptr_t{0x2c000});
    write(0x2c030,1u);write(0x2c054,3u);write(0x2c058,1u);text(0x30000,mesh.resource);
    write(0x24008,std::uintptr_t{0x34000});text(0x34000,"models/characters/player/corvo/player_leftarm_corvo.md6mesh");
    const auto bound=nh::bind(reader,module,arm,binding,mesh);assert(bound && bound->primary==0x24000);
    text(0x34000,"models/characters/player/corvo/player_leftarm_sw.md6mesh");assert(!nh::bind(reader,module,arm,binding,mesh));
    text(0x34000,"models/characters/player/corvo/player_leftarm_corvo.md6mesh");write(0x2c054,4u);assert(!nh::bind(reader,module,arm,binding,mesh));
    std::array<rig::Transform,69> palette{};
    for(auto& t:palette)t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;
    palette[1].f[12]=1;
    const std::array<float,15> model{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1};
    NoteViewMapping mapping{{{0,1.7,0},{}},{{1,2,3},{{{1,0,0},{0,1,0},{0,0,1}}}},{0,1.7,0},.45,.25,true};
    const auto geometry=nh::skin(mesh,binding.inverse,palette,model,mapping);
    assert(geometry.vertices.size()==3 && geometry.indices==std::vector<std::uint32_t>({0,1,2}));
    const auto expected=world_to_xr_controller({1+127./255.,2,3},mapping.origin,mapping.anchor,1.,mapping.controller_centre,mapping.origin.position);
    assert(expected && length(geometry.vertices[0]-*expected)<1e-7);
    // A live inverse change is used, never the cached file inverse.
    auto inverse=binding.inverse;inverse[0][3]=2;
    const auto altered=nh::skin(mesh,inverse,palette,model,mapping);
    assert(length(altered.vertices[0]-geometry.vertices[0])>1.);
    std::cout<<"PASS: installed mesh pack validation, ownership shape/inverse gates, eight-weight affine skinning and XR mapping\n";
}
