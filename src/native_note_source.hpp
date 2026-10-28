#pragma once
#include "game_rig.hpp"

namespace d2vr::rig {
// Read-only route checks; the caller must still validate native ownership and lifetime.
template<class Read> std::optional<std::uintptr_t> render_palette_source(Read& read,std::uintptr_t animation,std::uint32_t count) {
    if(!pointer(animation) || (count!=20 && count!=69))return {};
    const auto transforms=list(read,animation+0x2d8,4096),states=list(read,animation+0x2e8,4);
    if(!transforms || !states || states->packed_count!=4)return {};
    const auto mapping=list(read,states->data+0x30,4096);
    if(!mapping || mapping->packed_count!=count)return {};
    std::array<std::int16_t,69> indices{},again{};
    if(!read(mapping->data,indices.data(),count*sizeof(indices[0])) || indices[0]<0)return {};
    for(unsigned i=0;i<count;++i)if(indices[i]!=indices[0]+static_cast<int>(i) ||
        static_cast<unsigned>(indices[i])>=transforms->packed_count)return {};
    const auto after_transforms=list(read,animation+0x2d8,4096),after_states=list(read,animation+0x2e8,4);
    const auto after_mapping=list(read,states->data+0x30,4096);
    const auto same=[](const List& a,const List& b) {
        return a.data==b.data && a.packed_count==b.packed_count && a.packed_capacity==b.packed_capacity;
    };
    if(!after_transforms || !after_states || !after_mapping || !same(*transforms,*after_transforms) ||
       !same(*states,*after_states) || !same(*mapping,*after_mapping) ||
       !read(mapping->data,again.data(),count*sizeof(again[0])) || again!=indices)return {};
    return transforms->data+static_cast<std::uintptr_t>(indices[0])*64;
}

template<class Read> std::optional<std::array<std::uintptr_t,2>> native_skin_buffers(Read& read,std::uintptr_t model,std::uint32_t count) {
    if(!pointer(model) || (count!=20 && count!=69))return {};
    const auto current=value<std::int8_t>(read,model+0x1fb2),previous=value<std::int8_t>(read,model+0x1fb3);
    const auto packed=value<std::uint32_t>(read,model+0x2000);
    // Native skin rows include padding to a multiple of eight joints.
    if(!current || !previous || *current<0 || *current>1 || *previous<0 || *previous>1 ||
       !packed || (*packed&0xffffff)!=((count+7u)&~7u))return {};
    const std::array<std::uintptr_t,2> wrappers{model+0x2008+static_cast<unsigned>(*current)*0x38,
        model+0x2008+static_cast<unsigned>(*previous)*0x38};
    const auto a=value<std::uintptr_t>(read,wrappers[0]),b=value<std::uintptr_t>(read,wrappers[1]);
    if(!a || !b || !pointer(*a) || !pointer(*b))return {};
    if(value<std::int8_t>(read,model+0x1fb2)!=current || value<std::int8_t>(read,model+0x1fb3)!=previous ||
       value<std::uint32_t>(read,model+0x2000)!=packed || value<std::uintptr_t>(read,wrappers[0])!=a ||
       value<std::uintptr_t>(read,wrappers[1])!=b)return {};
    return std::array<std::uintptr_t,2>{*a,*b};
}
}
