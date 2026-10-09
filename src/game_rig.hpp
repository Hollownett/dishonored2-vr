#pragma once
// Read-only layout for the pinned Steam build; animation writes remain disabled.
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2vr {
namespace rig {
inline bool pointer(std::uintptr_t p) {return p>=0x10000 && p<0x0000800000000000 && p%8==0;}
template<class T,class Read> std::optional<T> value(Read& read,std::uintptr_t at) {
    T result{};
    if(at<0x10000 || at>=0x0000800000000000-sizeof(T) || !read(at,&result,sizeof(result))) return {};
    return result;
}
template<class Read> bool typed(Read& read,std::uintptr_t base,std::uintptr_t at,std::uintptr_t primary,std::uintptr_t secondary=0) {
    if(!pointer(at) || value<std::uintptr_t>(read,at)!=base+primary) return false;
    return !secondary || value<std::uintptr_t>(read,at+0x20)==base+secondary;
}
struct List {std::uintptr_t data{};std::uint32_t packed_count{},packed_capacity{};};
template<class Read> std::optional<List> list(Read& read,std::uintptr_t at,std::uint32_t limit) {
    auto l=value<List>(read,at);
    if(!l) return {};
    const auto count=l->packed_count&0xffffff,capacity=l->packed_capacity>>8;
    if(count>capacity || capacity>limit || (count && !pointer(l->data))) return {};
    l->packed_count=count;l->packed_capacity=capacity;return l;
}
struct Identity {std::int32_t generation{};std::uint32_t flags{};};
inline bool operator==(Identity a,Identity b) {return a.generation==b.generation && a.flags==b.flags;}
inline bool operator!=(Identity a,Identity b) {return !(a==b);}
template<class Read> std::optional<Identity> started(Read& read,std::uintptr_t at) {
    auto id=value<Identity>(read,at+0x18);
    if(!id || id->generation==-1) return {};
    const auto f=id->flags;
    const auto state=((((f>>2)&3)|((f&3)<<2))<<2)|((f>>4)&3);
    if((state&40)!=40) return {}; // Exact CF_STARTED mask used by native 0x7e7390.
    return id;
}
struct Arm {
    std::uintptr_t player{},body{},animation{},model{},skeleton{};
    Identity body_identity{},animation_identity{};
    unsigned side{};
};
inline bool operator==(const Arm& a,const Arm& b) {
    return a.player==b.player && a.body==b.body && a.animation==b.animation && a.model==b.model &&
        a.skeleton==b.skeleton && a.side==b.side && a.body_identity==b.body_identity && a.animation_identity==b.animation_identity;
}
template<class Read> std::optional<std::array<Arm,2>> player_arms(Read& read,std::uintptr_t base) {
    auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return {};
    auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    if(!player || !typed(read,base,*player,0x1c5e428)) return {};
    auto model=value<std::uintptr_t>(read,*player+0xf0);
    if(!model || !typed(read,base,*model,0x1b0d628) || value<std::uintptr_t>(read,*player+0x360)!=model) return {};
    auto components=list(read,*player+0x268,4096);
    if(!components || !components->packed_count) return {};
    std::vector<std::uintptr_t> addresses(components->packed_count);
    if(!read(components->data,addresses.data(),addresses.size()*8)) return {};
    std::uintptr_t body{};
    for(auto at:addresses) if(typed(read,base,at,0x1c5a8d8,0x1c5a9b0) && value<std::uintptr_t>(read,at+0x10)==player) {
        if(body) return {};
        body=at;
    }
    if(!body) return {};
    auto body_id=started(read,body);auto animation=value<std::uintptr_t>(read,body+0x58);
    if(!body_id || !animation || !typed(read,base,*animation,0x1b80408,0x1b804f0) ||
       value<std::uintptr_t>(read,*animation+0x38)!=model) return {};
    std::array<Arm,2> arms{};
    for(unsigned side=0;side<2;++side) {
        auto anim=value<std::uintptr_t>(read,body+(side?0x688:0x378));
        if(!anim || !typed(read,base,*anim,0x1b80408,0x1b804f0)) return {};
        auto id=started(read,*anim);auto m=value<std::uintptr_t>(read,*anim+0x38);
        if(!id || !m || !typed(read,base,*m,0x1b0d628)) return {};
        auto descriptor=value<std::uintptr_t>(read,*m+0x1fe8);
        if(!descriptor || !pointer(*descriptor)) return {};
        auto skeleton=value<std::uintptr_t>(read,*descriptor+0x50);
        if(!skeleton || !pointer(*skeleton) || value<std::uint16_t>(read,*skeleton+0x78)!=69) return {};
        arms[side]={*player,body,*anim,*m,*skeleton,*body_id,*id,side};
    }
    if(arms[0].model==arms[1].model || value<Identity>(read,body+0x18)!=body_id ||
       value<std::uintptr_t>(read,*game+0x3130b0)!=player) return {};
    auto after=list(read,*player+0x268,4096);
    std::vector<std::uintptr_t> again(addresses.size());
    if(!after || after->data!=components->data || after->packed_count!=components->packed_count ||
       !read(after->data,again.data(),again.size()*8) || again!=addresses) return {};
    for(const auto& arm:arms)
        if(value<Identity>(read,arm.animation+0x18)!=arm.animation_identity ||
           value<std::uintptr_t>(read,body+(arm.side?0x688:0x378))!=arm.animation ||
           value<std::uintptr_t>(read,arm.animation+0x38)!=arm.model) return {};
    return arms;
}
struct Transform {std::array<float,16> f{};};
inline bool valid(const Transform& t) {
    for(unsigned column=0;column<4;++column) for(unsigned row=0;row<3;++row)
        if(!std::isfinite(t.f[column*4+row]) || std::abs(t.f[column*4+row])>1e7f) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=i;j<3;++j) {
        float dot{};for(unsigned k=0;k<3;++k) dot+=t.f[i*4+k]*t.f[j*4+k];
        if(std::abs(dot-(i==j?1.f:0.f))>.03f) return false;
    }
    const auto& a=t.f;
    const float det=a[0]*(a[5]*a[10]-a[6]*a[9])-a[4]*(a[1]*a[10]-a[2]*a[9])+a[8]*(a[1]*a[6]-a[2]*a[5]);
    return std::abs(det-1.f)<=.03f;
}
struct Upload {std::array<Transform,69> joints;std::array<float,15> model_frame;std::int16_t first_transform{};};
using ArmHierarchy=std::array<std::int16_t,69>;
inline bool valid_hierarchy(const ArmHierarchy& parents) {
    unsigned roots{};
    for(auto parent:parents) {
        if(parent==-1) ++roots;
        else if(parent<0 || parent>=69) return false;
    }
    if(roots!=1) return false;
    for(unsigned joint=0;joint<69;++joint) {
        std::array<bool,69> visited{};int at=static_cast<int>(joint);
        while(at!=-1) {if(visited[at]) return false;visited[at]=true;at=parents[at];}
    }
    return true;
}
template<class Read> std::optional<ArmHierarchy> arm_hierarchy(Read& read,std::uintptr_t base,const Arm& arm) {
    auto current=player_arms(read,base);
    if(!current || arm.side>1 || !((*current)[arm.side]==arm) ||
       value<std::uintptr_t>(read,arm.skeleton)!=base+0x1b0bd08 ||
       value<std::uintptr_t>(read,base+0x1b0bd08+0x88)!=base+0x24f240) return {};
    // Exact native 0x24f240: the cooked declaration has a relative lookup,
    // whose record indices may exceed this skinned model's joint subset.
    auto blob=value<std::uintptr_t>(read,arm.skeleton+0x158);
    if(!blob || !pointer(*blob)) return {};
    auto relative=value<std::uint32_t>(read,*blob+0x1c);
    if(!relative || !*relative || *relative>1024*1024) return {};
    std::array<std::uint16_t,69> mapping{};std::uint16_t maximum{};
    if(!read(*blob+0x1c+*relative,mapping.data(),sizeof(mapping))) return {};
    for(auto index:mapping) {if(index>=2048) return {};if(index>maximum) maximum=index;}
    std::vector<std::uint16_t> records((static_cast<std::size_t>(maximum)+1)*2);
    if(!read(*blob+0x40,records.data(),records.size()*2)) return {};
    ArmHierarchy parents{};
    for(unsigned joint=0;joint<69;++joint) {
        const auto parent=records[mapping[joint]*2+1]&0x7fff;
        parents[joint]=parent>=0x4000?-1:static_cast<std::int16_t>(parent);
    }
    if(!valid_hierarchy(parents)) return {};
    std::array<std::uint16_t,69> again{};std::vector<std::uint16_t> record_check(records.size());
    auto after=player_arms(read,base);
    if(!after || !((*after)[arm.side]==arm) || value<std::uintptr_t>(read,arm.skeleton+0x158)!=blob ||
       value<std::uint32_t>(read,*blob+0x1c)!=relative ||
       !read(*blob+0x1c+*relative,again.data(),sizeof(again)) || again!=mapping ||
       !read(*blob+0x40,record_check.data(),record_check.size()*2) || record_check!=records) return {};
    return parents;
}
template<class Read> std::optional<Upload> arm_upload(Read& read,std::uintptr_t base,const Arm& arm,std::uintptr_t source,std::uint32_t caller_count) {
    auto current=player_arms(read,base);
    if(!current || arm.side>1 || !((*current)[arm.side]==arm) || caller_count!=69) return {};
    auto transforms=list(read,arm.animation+0x2d8,4096),states=list(read,arm.animation+0x2e8,4);
    if(!transforms || !states || states->packed_count!=4) return {};
    auto mapping=list(read,states->data+0x30,4096);
    if(!mapping || mapping->packed_count!=69) return {};
    std::array<std::int16_t,69> indices{};
    if(!read(mapping->data,indices.data(),sizeof(indices)) || indices[0]<0) return {};
    for(unsigned i=0;i<69;++i) if(indices[i]!=indices[0]+static_cast<int>(i) || static_cast<unsigned>(indices[i])>=transforms->packed_count) return {};
    if(source!=transforms->data+static_cast<std::uintptr_t>(indices[0])*64) return {};
    Upload upload{};upload.first_transform=indices[0];
    if(!read(source,upload.joints.data(),sizeof(upload.joints)) || !read(arm.model+0x1860,upload.model_frame.data(),sizeof(upload.model_frame))) return {};
    for(const auto& joint:upload.joints) if(!valid(joint)) return {};
    for(float f:upload.model_frame) if(!std::isfinite(f) || std::abs(f)>1e7f) return {};
    auto after=player_arms(read,base);
    if(!after || !((*after)[arm.side]==arm)) return {};
    return upload;
}
}
}
