#pragma once
#include "avatar_fit_reuse.hpp"
#include "note_reading.hpp"
#include "rig_transform.hpp"
#include <istream>
#include <memory>
#include <numeric>

namespace d2vr::note_hand {
using Inverse=std::array<std::array<float,12>,69>;
struct Vertex {std::array<float,3> position{};std::array<std::uint8_t,8> weights{},joints{};};
static_assert(sizeof(Vertex)==28);
struct Texture {std::uint32_t width{},height{};std::vector<std::uint8_t> rgba;};
struct Surface {std::uint32_t kind{};std::vector<Vertex> vertices;std::vector<std::uint16_t> indices;
    std::vector<std::array<float,2>> uv;Texture diffuse;bool hand{};};
struct Mesh {
    std::uint32_t character{},side{};
    std::array<std::uint8_t,32> source_sha256{};
    std::string resource;
    Inverse inverse{};
    std::vector<Surface> surfaces;
};
template<class T> inline T take(std::istream& in) {
    T out{};if(!in.read(reinterpret_cast<char*>(&out),sizeof(out)))throw std::invalid_argument("Truncated note hand mesh");return out;
}
inline Mesh read(std::istream& in) {
    const auto magic=take<std::array<char,8>>(in);const auto version=take<std::uint32_t>(in);
    if(magic!=std::array<char,8>{'D','2','V','R','N','H','0','1'} || (version!=1 && version!=2))
        throw std::invalid_argument("Unknown note hand mesh");
    Mesh out;out.character=take<std::uint32_t>(in);out.side=take<std::uint32_t>(in);
    if(out.character>1 || out.side>1)throw std::invalid_argument("Unknown note hand identity");
    out.source_sha256=take<decltype(out.source_sha256)>(in);
    if(std::all_of(out.source_sha256.begin(),out.source_sha256.end(),[](auto b){return b==0;}))throw std::invalid_argument("Missing note source hash");
    const auto length=take<std::uint32_t>(in);if(!length || length>255)throw std::invalid_argument("Invalid note resource name");
    out.resource.resize(length);if(!in.read(out.resource.data(),length))throw std::invalid_argument("Truncated note resource name");
    const std::string character=out.character?"emily":"corvo",limb=out.side?"right_arm":"left_arm";
    if(out.resource!="models/characters/player/"+character+"/"+limb+".md6")throw std::invalid_argument("Note resource identity disagrees");
    out.inverse=take<Inverse>(in);
    for(const auto& row:out.inverse)for(float f:row)if(!std::isfinite(f) || std::abs(f)>1e7f)throw std::invalid_argument("Invalid note inverse bind");
    const auto count=take<std::uint32_t>(in);if(!count || count>64)throw std::invalid_argument("Invalid note surface count");
    std::uint64_t total_vertices{},total_indices{},total_texture_bytes{};
    for(unsigned s=0;s<count;++s) {
        Surface surface;surface.kind=take<std::uint32_t>(in);
        const auto vertices=take<std::uint32_t>(in),indices=take<std::uint32_t>(in);
        total_vertices+=vertices;total_indices+=indices;
        if(!vertices || vertices>65536 || !indices || indices>3000000 || indices%3 || total_vertices>500000 || total_indices>3000000)
            throw std::invalid_argument("Invalid note geometry bounds");
        surface.vertices.resize(vertices);surface.indices.resize(indices);
        for(auto& vertex:surface.vertices) {
            vertex=take<Vertex>(in);
            for(float p:vertex.position)if(!std::isfinite(p) || std::abs(p)>1e6f)throw std::invalid_argument("Invalid note mesh vertex");
            if(std::accumulate(vertex.weights.begin(),vertex.weights.end(),0u)!=255)throw std::invalid_argument("Invalid note skin weights");
            for(unsigned i=0;i<8;++i)if(vertex.weights[i] && vertex.joints[i]>=69)throw std::invalid_argument("Invalid note skin joint");
        }
        for(auto& index:surface.indices) {index=take<std::uint16_t>(in);if(index>=vertices)throw std::invalid_argument("Invalid note mesh triangle");}
        if(version==2) {
            const auto flags=take<std::uint32_t>(in);if(flags>1)throw std::invalid_argument("Invalid note hand surface flags");surface.hand=flags!=0;
            surface.uv.resize(vertices);
            for(auto& uv:surface.uv) {uv=take<std::array<float,2>>(in);for(float f:uv)if(!std::isfinite(f) || std::abs(f)>1e6f)throw std::invalid_argument("Invalid note mesh UV");}
            auto& texture=surface.diffuse;texture.width=take<std::uint32_t>(in);texture.height=take<std::uint32_t>(in);
            const std::uint64_t bytes=static_cast<std::uint64_t>(texture.width)*texture.height*4;total_texture_bytes+=bytes;
            if(!texture.width || !texture.height || texture.width>4096 || texture.height>4096 || total_texture_bytes>128*1024*1024)
                throw std::invalid_argument("Invalid note diffuse texture bounds");
            texture.rgba.resize(static_cast<std::size_t>(bytes));
            if(!in.read(reinterpret_cast<char*>(texture.rgba.data()),static_cast<std::streamsize>(bytes)))throw std::invalid_argument("Truncated note diffuse texture");
        }
        out.surfaces.push_back(std::move(surface));
    }
    if(in.peek()!=std::char_traits<char>::eof())throw std::invalid_argument("Trailing note mesh data");
    return out;
}
inline bool matches(const Mesh& mesh,const rig::ArmBinding& binding,const rig::AvatarMeshToken& token) {
    if(mesh.surfaces.size()!=token.surfaces.size())return false;
    // File quaternion reconstruction can round slightly. Skinning below uses
    // the live inverse rows, preserving the actual native palette calculation.
    for(unsigned joint=0;joint<69;++joint)for(unsigned i=0;i<12;++i)
        if(!std::isfinite(binding.inverse[joint][i]) || std::abs(mesh.inverse[joint][i]-binding.inverse[joint][i])>1e-4f)return false;
    for(unsigned i=0;i<mesh.surfaces.size();++i) {
        const auto& s=mesh.surfaces[i];const auto& t=token.surfaces[i];
        if(!rig::pointer(t.resource) || s.kind!=t.kind || s.vertices.size()!=t.vertices || s.indices.size()/3!=t.triangles)return false;
    }
    return true;
}
template<class Read> inline std::optional<rig::AvatarMeshToken> bind(Read& reader,std::uintptr_t base,
        const rig::Arm& arm,const rig::ArmBinding& binding,const Mesh& mesh) {
    if(arm.side!=mesh.side)return {};
    const auto token=rig::avatar_mesh_token(reader,base,arm);
    if(!token || !matches(mesh,binding,*token))return {};
    std::array<char,256> text{};
    // descriptor+8 is the native logical resource C string, not an idStr.
    if(!reader(token->source,text.data(),mesh.resource.size()+1) || std::string(text.data())!=mesh.resource ||
       rig::avatar_mesh_token(reader,base,arm)!=token)return {};
    // Logical arm definitions can select other mesh variants with the same rig.
    const auto primary_name=rig::value<std::uintptr_t>(reader,token->primary+8);
    const std::string character=mesh.character?"emily":"corvo",limb=mesh.side?"rightarm":"leftarm";
    const std::string expected="models/characters/player/"+character+"/player_"+limb+"_"+character+".md6mesh";
    text={};
    if(!primary_name || !rig::pointer(*primary_name) || !reader(*primary_name,text.data(),expected.size()+1) ||
       std::string(text.data())!=expected || rig::value<std::uintptr_t>(reader,token->primary+8)!=primary_name ||
       rig::avatar_mesh_token(reader,base,arm)!=token)return {};
    return token;
}
struct Geometry {std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;};
// The CPU menu poser supplies an XR-metre joint palette; native bind vertices
// and inverse bind rows retain their model units until this final uniform scale.
inline Geometry skin_xr(const Mesh& mesh,const Inverse& inverse,const std::array<rig::Transform,69>& palette,double model_scale) {
    if(!std::isfinite(model_scale) || model_scale<=0 || model_scale>1000)throw std::invalid_argument("Invalid note hand model scale");
    for(const auto& transform:palette)if(!rig::valid(transform))throw std::invalid_argument("Invalid XR note hand palette");
    for(const auto& row:inverse)for(float f:row)if(!std::isfinite(f))throw std::invalid_argument("Invalid XR note inverse bind");
    Geometry result;
    for(const auto& surface:mesh.surfaces) {
        const auto offset=static_cast<std::uint32_t>(result.vertices.size());
        for(const auto& vertex:surface.vertices) {
            Vec3 p{};const auto& v=vertex.position;
            for(unsigned i=0;i<8;++i)if(vertex.weights[i]) {
                const auto joint=vertex.joints[i];if(joint>=69)throw std::invalid_argument("Invalid XR note skin joint");
                const auto& b=inverse[joint];const auto& t=palette[joint];
                const Vec3 local{(b[0]*v[0]+b[1]*v[1]+b[2]*v[2]+b[3])*model_scale,
                    (b[4]*v[0]+b[5]*v[1]+b[6]*v[2]+b[7])*model_scale,(b[8]*v[0]+b[9]*v[1]+b[10]*v[2]+b[11])*model_scale};
                p=p+(rig::position(t)+rig::column(t,0)*local.x+rig::column(t,1)*local.y+rig::column(t,2)*local.z)*(vertex.weights[i]/255.);
            }
            if(!finite(p))throw std::invalid_argument("Invalid XR note skinned vertex");result.vertices.push_back(p);
        }
        for(auto index:surface.indices) {if(index>=surface.vertices.size())throw std::invalid_argument("Invalid XR note triangle");result.indices.push_back(offset+index);}
    }
    return result;
}
// The skinned triangles as one flat XR-space list (NoteHandReceipt::triangles): the occluder a note page or the hand wheel's ring is drawn behind.
inline std::shared_ptr<const std::vector<Vec3>> triangles(const Geometry& geometry) {
    auto out=std::make_shared<std::vector<Vec3>>();out->reserve(geometry.indices.size());
    for(auto index:geometry.indices)out->push_back(geometry.vertices.at(index));
    return out;
}
// v776: an occluder is trusted only where its hand is (a wrong world->XR mapping would put it elsewhere): some vertex lies within radius of the grip.
inline bool at_hand(const std::vector<Vec3>& triangles,Vec3 grip,double radius) {
    if(!finite(grip) || !(radius>0))return false;
    for(const auto& p:triangles)if(length(p-grip)<=radius)return true;
    return false;
}
inline Geometry skin(const Mesh& mesh,const Inverse& inverse,const std::array<rig::Transform,69>& palette,
        const std::array<float,15>& model_frame,const NoteViewMapping& mapping) {
    if(!mapping.active)throw std::invalid_argument("Inactive note hand mapping");
    const rig::ModelFrame frame(model_frame);Geometry result;
    // The shared body solver widens torso-connected bases with an affine scale.
    for(const auto& transform:palette)for(unsigned column=0;column<4;++column)for(unsigned row=0;row<3;++row)
        if(!std::isfinite(transform.f[column*4+row]) || std::abs(transform.f[column*4+row])>1e7f)
            throw std::invalid_argument("Invalid note hand palette");
    for(const auto& surface:mesh.surfaces) {
        const auto offset=static_cast<std::uint32_t>(result.vertices.size());
        for(const auto& vertex:surface.vertices) {
            Vec3 p{};const auto& v=vertex.position;
            for(unsigned i=0;i<8;++i)if(vertex.weights[i]) {
                const auto joint=vertex.joints[i];const auto& b=inverse[joint];const auto& t=palette[joint];
                const Vec3 local{b[0]*v[0]+b[1]*v[1]+b[2]*v[2]+b[3],b[4]*v[0]+b[5]*v[1]+b[6]*v[2]+b[7],b[8]*v[0]+b[9]*v[1]+b[10]*v[2]+b[11]};
                p=p+(rig::position(t)+rig::column(t,0)*local.x+rig::column(t,1)*local.y+rig::column(t,2)*local.z)*(vertex.weights[i]/255.);
            }
            const auto xr=world_to_xr_controller(frame.world_point(p),mapping.origin,mapping.anchor,1.,mapping.controller_centre,mapping.origin.position);
            if(!xr || !finite(*xr))throw std::invalid_argument("Invalid note mesh XR mapping");
            result.vertices.push_back(*xr);
        }
        for(auto index:surface.indices)result.indices.push_back(offset+index);
    }
    return result;
}
} // namespace d2vr::note_hand
