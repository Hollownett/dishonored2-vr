#define NOMINMAX
#include "../src/note_hand_mesh.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>

using namespace d2vr;
namespace nh=d2vr::note_hand;
template<class T> static void put(std::ostream& out,const T& value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));}
static std::string pack(const nh::Mesh& mesh,unsigned version=1) {
    std::ostringstream out(std::ios::binary);
    put(out,std::array<char,8>{'D','2','V','R','N','H','0','1'});put(out,version);put(out,mesh.character);put(out,mesh.side);
    put(out,mesh.source_sha256);put(out,static_cast<std::uint32_t>(mesh.resource.size()));out.write(mesh.resource.data(),mesh.resource.size());put(out,mesh.inverse);
    put(out,static_cast<std::uint32_t>(mesh.surfaces.size()));
    for(const auto& s:mesh.surfaces) {
        put(out,s.kind);put(out,static_cast<std::uint32_t>(s.vertices.size()));put(out,static_cast<std::uint32_t>(s.indices.size()));
        for(const auto& v:s.vertices)put(out,v);for(auto i:s.indices)put(out,i);
        if(version==2) {put(out,s.hand?1u:0u);for(const auto& uv:s.uv)put(out,uv);put(out,s.diffuse.width);put(out,s.diffuse.height);
            out.write(reinterpret_cast<const char*>(s.diffuse.rgba.data()),s.diffuse.rgba.size());}
    }
    return out.str();
}
static bool rejects(const std::string& bytes) {try {std::istringstream in(bytes,std::ios::binary);(void)nh::read(in);return false;}catch(const std::invalid_argument&){return true;}}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i) {std::ifstream file(argv[i],std::ios::binary);const auto installed=nh::read(file);std::size_t vertices{},triangles{};for(const auto& s:installed.surfaces){vertices+=s.vertices.size();triangles+=s.indices.size()/3;}std::cout<<"PACK "<<installed.resource<<" vertices "<<vertices<<" triangles "<<triangles<<"\n";}
    // The note-hand id table (launcher/NoteHandMesh.cs writes the same names): D2 Corvo/Emily, DOTO Billie, whose md6 inherits Emily's.
    assert(nh::resource(0,0)=="models/characters/player/corvo/left_arm.md6" && nh::resource(1,1)=="models/characters/player/emily/right_arm.md6");
    assert(nh::resource(2,0)=="models/characters/dlc01/player/billie/player_leftarm_billie.md6" &&
           nh::resource(2,1)=="models/characters/dlc01/player/billie/player_rightarm_billie.md6");
    assert(nh::primary_mesh(0,0)=="models/characters/player/corvo/player_leftarm_corvo.md6mesh" &&
           nh::primary_mesh(1,1)=="models/characters/player/emily/player_rightarm_emily.md6mesh" &&
           nh::primary_mesh(2,0)=="models/characters/dlc01/player/billie/player_leftarm_billie.md6mesh" &&
           nh::primary_mesh(2,1)=="models/characters/dlc01/player/billie/player_rightarm_billie.md6mesh");
    assert(nh::file_name(0,0)=="note_hand_corvo_left.bin" && nh::file_name(1,1)=="note_hand_emily_right.bin" &&
           nh::file_name(2,0)=="note_hand_billie_left.bin" && nh::file_name(2,1)=="note_hand_billie_right.bin");
    // This game's first player id (D2 Corvo 0, DOTO Billie 2) owns the generic cases below; D2 builds them exactly as before.
    const std::uint32_t own=game_traits::note_hand_first_character;
    nh::Mesh source;source.character=own;source.resource=nh::resource(own,0);source.source_sha256[0]=1;
    for(auto& b:source.inverse)b={1,0,0,0,0,1,0,0,0,0,1,0};
    nh::Surface surface;surface.kind=1;
    for(unsigned i=0;i<3;++i) {nh::Vertex v;v.position={static_cast<float>(i),0,0};v.weights[0]=128;v.weights[1]=127;v.joints[1]=1;surface.vertices.push_back(v);}
    surface.indices={0,1,2};source.surfaces.push_back(surface);
    auto textured=source;textured.surfaces[0].uv={{{0,0}},{{1,0}},{{0,1}}};textured.surfaces[0].diffuse={1,1,{12,34,56,255}};textured.surfaces[0].hand=true;
    const auto v2=pack(textured,2);std::istringstream v2in(v2,std::ios::binary);const auto v2mesh=nh::read(v2in);
    assert(v2mesh.surfaces[0].uv==textured.surfaces[0].uv && v2mesh.surfaces[0].diffuse.rgba==textured.surfaces[0].diffuse.rgba && v2mesh.surfaces[0].hand);
    assert(rejects(v2.substr(0,v2.size()-1)));
    auto bad_texture=textured;bad_texture.surfaces[0].diffuse.width=4097;assert(rejects(pack(bad_texture,2)));
    bad_texture=textured;bad_texture.surfaces[0].uv[0][0]=NAN;assert(rejects(pack(bad_texture,2)));
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
    write(0x24008,std::uintptr_t{0x34000});text(0x34000,nh::primary_mesh(own,0));
    const auto bound=nh::bind(reader,module,arm,binding,mesh);assert(bound && bound->primary==0x24000);
    text(0x34000,std::string(nh::identities[own].folder)+"player_leftarm_sw.md6mesh");assert(!nh::bind(reader,module,arm,binding,mesh));
    text(0x34000,nh::primary_mesh(own,0));write(0x2c054,4u);assert(!nh::bind(reader,module,arm,binding,mesh));write(0x2c054,3u);
    assert(nh::bind(reader,module,arm,binding,mesh));
    // Another logical arm (the other side, the other character) never owns the shape.
    text(0x30000,nh::resource(own,1));assert(!nh::bind(reader,module,arm,binding,mesh));
    // Emily's logical left arm: never Corvo's; Billie's md6 inherits it, so it may name hers (her primary mesh still decides).
    text(0x30000,nh::resource(1,0));assert(bool(nh::bind(reader,module,arm,binding,mesh))==(own==2));
    text(0x30000,mesh.resource);assert(nh::bind(reader,module,arm,binding,mesh));
    // Billie (Death of the Outsider, id 2): her own md6 names; the D2 build refuses her file and DOTO refuses Corvo's and Emily's.
    auto billie=source;billie.character=2;billie.resource="models/characters/dlc01/player/billie/player_leftarm_billie.md6";
    auto corvo=source;corvo.character=0;corvo.resource="models/characters/player/corvo/left_arm.md6";
    auto emily=source;emily.character=1;emily.resource="models/characters/player/emily/left_arm.md6";
    auto billie_right=billie;billie_right.side=1;billie_right.resource="models/characters/dlc01/player/billie/player_rightarm_billie.md6";
    auto billie_as_emily=billie;billie_as_emily.resource=emily.resource;assert(rejects(pack(billie_as_emily))); // the file names her own md6
    auto billie_void=billie_right;billie_void.resource="models/characters/dlc01/player/billie/player_rightarm_void_billie.md6";assert(rejects(pack(billie_void)));
    auto too_high=billie;too_high.character=3;assert(rejects(pack(too_high)));
    if constexpr(game_traits::note_hand_first_character==2) {
        assert(!rejects(pack(billie)) && !rejects(pack(billie_right)) && rejects(pack(corvo)) && rejects(pack(emily)));
        std::istringstream billie_in(pack(billie_right),std::ios::binary);const auto right=nh::read(billie_in);
        assert(right.character==2 && right.side==1 && right.resource==billie_right.resource);
        // The live logical resource is her md6 (her arm defs name it) or, inherited, Emily's; either way her primary mesh must own the arm.
        assert(nh::bind(reader,module,arm,binding,mesh));
        text(0x30000,"models/characters/player/emily/left_arm.md6");assert(nh::bind(reader,module,arm,binding,mesh));
        text(0x34000,"models/characters/player/emily/player_leftarm_emily.md6mesh");assert(!nh::bind(reader,module,arm,binding,mesh));
        text(0x34000,nh::primary_mesh(2,0));text(0x30000,"models/characters/player/emily/right_arm.md6");assert(!nh::bind(reader,module,arm,binding,mesh));
        text(0x30000,mesh.resource);
        // Her void right arm is another mesh: a right arm whose primary is the void mesh is refused (the note keeps its controller attachment).
        rig::Arm right_arm=arm;right_arm.side=1;text(0x30000,right.resource);
        text(0x34000,"models/characters/dlc01/player/billie/player_rightarm_void_billie.md6mesh");assert(!nh::bind(reader,module,right_arm,binding,right));
        text(0x34000,nh::primary_mesh(2,1));assert(nh::bind(reader,module,right_arm,binding,right));
        text(0x30000,"models/characters/dlc01/player/billie/player_rightarm_void_billie.md6");assert(!nh::bind(reader,module,right_arm,binding,right));
        text(0x30000,mesh.resource);text(0x34000,nh::primary_mesh(2,0));
    } else {
        assert(rejects(pack(billie)) && rejects(pack(billie_right)) && !rejects(pack(corvo)) && !rejects(pack(emily)));
    }
    assert(nh::bind(reader,module,arm,binding,mesh));
    std::array<rig::Transform,69> palette{};
    for(auto& t:palette)t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;
    palette[1].f[12]=1;
    const std::array<float,15> model{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1};
    NoteViewMapping mapping{{{0,1.7,0},{}},{{1,2,3},{{{1,0,0},{0,1,0},{0,0,1}}}},{0,1.7,0},.45,.25,true};
    const auto xr_geometry=nh::skin_xr(mesh,binding.inverse,palette,.5);
    assert(length(xr_geometry.vertices[0]-Vec3{127./255.,0,0})<1e-7);
    assert(std::abs(xr_geometry.vertices[1].x-(.5+127./255.))<1e-7);
    bool scale_rejected=false;try{(void)nh::skin_xr(mesh,binding.inverse,palette,0);}catch(const std::invalid_argument&){scale_rejected=true;}assert(scale_rejected);
    const auto geometry=nh::skin(mesh,binding.inverse,palette,model,mapping);
    assert(geometry.vertices.size()==3 && geometry.indices==std::vector<std::uint32_t>({0,1,2}));
    const auto expected=world_to_xr_controller({1+127./255.,2,3},mapping.origin,mapping.anchor,1.,mapping.controller_centre,mapping.origin.position);
    assert(expected && length(geometry.vertices[0]-*expected)<1e-7);
    // v776 (headset v775): the hand wheel's occluder is these skinned triangles as one flat list, trusted only where its hand is.
    const auto flat=nh::triangles(geometry);
    assert(flat && flat->size()==3 && length((*flat)[0]-geometry.vertices[0])<1e-12 && length((*flat)[2]-geometry.vertices[2])<1e-12);
    assert(nh::at_hand(*flat,*expected,.001) && nh::at_hand(*flat,*expected+Vec3{.2,0,0},.3));          // the grip is on or near the hand
    assert(!nh::at_hand(*flat,*expected+Vec3{5,0,0},.3) && !nh::at_hand(*flat,Vec3{NAN,0,0},.3) && !nh::at_hand({},*expected,.3)); // a wrong mapping, a bad grip, no triangles
    nh::Geometry reordered=geometry;reordered.indices={2,1,0,0,1,2};
    assert(nh::triangles(reordered)->size()==6 && length((*nh::triangles(reordered))[0]-geometry.vertices[2])<1e-12); // the indices order the list
    auto affine=palette;affine[0].f[0]=1.2f;affine[1].f[5]=.8f;
    const auto widened=nh::skin(mesh,binding.inverse,affine,model,mapping);
    assert(widened.vertices.size()==3 && length(widened.vertices[1]-geometry.vertices[1])>.09);
    for(const float invalid:{NAN,INFINITY,1.01e7f,-1.01e7f}) {
        auto broken=affine;broken[68].f[9]=invalid;
        bool refused=false;try{(void)nh::skin(mesh,binding.inverse,broken,model,mapping);}catch(const std::invalid_argument&){refused=true;}
        assert(refused); // all bases are validated, including joints without vertex weights
        broken=affine;broken[0].f[12]=invalid;refused=false;
        try{(void)nh::skin(mesh,binding.inverse,broken,model,mapping);}catch(const std::invalid_argument&){refused=true;}
        assert(refused);
    }
    // A live inverse change is used, never the cached file inverse.
    auto inverse=binding.inverse;inverse[0][3]=2;
    const auto altered=nh::skin(mesh,inverse,palette,model,mapping);
    assert(length(altered.vertices[0]-geometry.vertices[0])>1.);
    std::cout<<"PASS ("<<game_traits::name<<"): installed mesh pack validation, ownership shape/inverse gates, Billie's note-hand identity, eight-weight affine skinning and XR mapping\n";
}
