#pragma once
#include "game_rig.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <limits>

namespace d2vr::rig {
// Pinned native 2371e0/237460, verified by inspect_native_surface_buffers.py.
// Numeric buffer tokens are never dereferenced as COM objects.
struct GeometryBuffer {std::uintptr_t resource{};UINT offset{},stride{},bytes{};};
inline bool operator==(const GeometryBuffer& a,const GeometryBuffer& b){return a.resource==b.resource && a.offset==b.offset && a.stride==b.stride && a.bytes==b.bytes;}
struct GeometryRoute {
    std::uintptr_t surface{},geometry{};
    std::vector<GeometryBuffer> vertices;
    GeometryBuffer index;
    UINT count{};DXGI_FORMAT format{};
};
template<class Read> std::optional<GeometryRoute> native_note_geometry(Read& read,std::uintptr_t image_base,std::uintptr_t surface) {
    if(!pointer(surface))return {};
    const auto geometry=value<std::uintptr_t>(read,surface+0x90);
    if(!geometry || !typed(read,image_base,*geometry,0x1ab3a20))return {};
    const auto count=value<std::uint32_t>(read,*geometry+0x34),vertex_count=value<std::uint32_t>(read,*geometry+0x38);
    const auto index_type=value<std::uint8_t>(read,*geometry+0x9c);
    if(!count || !*count || *count>3000000 || *count%3 || !vertex_count || !*vertex_count || *vertex_count>500000 ||
       !index_type || (*index_type!=2 && *index_type!=4))return {};
    const auto ranges=list(read,surface+0xa8,1024);
    if(!ranges || ranges->packed_count!=1)return {};
    const auto range=value<std::array<std::uint32_t,3>>(read,ranges->data);
    if(!range || (*range!=std::array<std::uint32_t,3>{0,0,*count}))return {};
    GeometryRoute result{surface,*geometry,{}, {},*count,*index_type==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT};
    const auto collect=[&](std::uintptr_t at,UINT flags)->std::optional<std::vector<GeometryBuffer>> {
        const auto list_before=list(read,at,32);if(!list_before || !list_before->packed_count)return {};
        std::vector<GeometryBuffer> buffers;
        for(unsigned i=0;i<list_before->packed_count;++i) {
            const auto entry=value<std::array<std::uint64_t,4>>(read,list_before->data+i*32);
            if(!entry || !pointer((*entry)[0]) || (*entry)[2]>std::numeric_limits<UINT>::max() || (*entry)[3]>256)return {};
            const auto wrapper=value<std::array<std::uint32_t,8>>(read,(*entry)[0]);if(!wrapper)return {};
            const std::uintptr_t resource=(*wrapper)[0]|(static_cast<std::uint64_t>((*wrapper)[1])<<32);
            if(!pointer(resource) || !(*wrapper)[3] || (*wrapper)[3]>128*1024*1024 || (*wrapper)[6]>3 || (*wrapper)[7]!=flags || (*entry)[2]>=(*wrapper)[3])return {};
            if(flags==1 && !(*entry)[3])return {};
            buffers.push_back({resource,static_cast<UINT>((*entry)[2]),static_cast<UINT>((*entry)[3]),(*wrapper)[3]});
            if(value<std::array<std::uint64_t,4>>(read,list_before->data+i*32)!=entry || value<std::array<std::uint32_t,8>>(read,(*entry)[0])!=wrapper)return {};
        }
        const auto list_after=list(read,at,32);
        if(!list_after || list_after->data!=list_before->data || list_after->packed_count!=list_before->packed_count || list_after->packed_capacity!=list_before->packed_capacity)return {};
        return buffers;
    };
    const auto vertices=collect(*geometry+0xa0,1),indices=collect(*geometry+0x298,2);
    if(!vertices || !indices || indices->size()!=1)return {};
    result.vertices=*vertices;result.index=(*indices)[0];
    if(static_cast<std::uint64_t>(result.count)*(*index_type)+result.index.offset>result.index.bytes)return {};
    if(value<std::uintptr_t>(read,surface+0x90)!=geometry || !typed(read,image_base,*geometry,0x1ab3a20) ||
       value<std::uint32_t>(read,*geometry+0x34)!=count || value<std::uint32_t>(read,*geometry+0x38)!=vertex_count ||
       value<std::uint8_t>(read,*geometry+0x9c)!=index_type || value<std::array<std::uint32_t,3>>(read,ranges->data)!=range)return {};
    const auto final_ranges=list(read,surface+0xa8,1024);
    if(!final_ranges || final_ranges->data!=ranges->data || final_ranges->packed_count!=1)return {};
    return result;
}
inline bool matches_native_note_geometry(ID3D11DeviceContext* context,const GeometryRoute& route,UINT count,UINT instances,UINT start,INT base,UINT first=0) {
    if(!context || count!=route.count || !count || instances!=1 || start || base || first || route.vertices.empty() || route.vertices.size()>32)return false;
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
    if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return false;
    Microsoft::WRL::ComPtr<ID3D11Buffer> index;DXGI_FORMAT format{};UINT offset{};context->IAGetIndexBuffer(&index,&format,&offset);
    if(reinterpret_cast<std::uintptr_t>(index.Get())!=route.index.resource || format!=route.format || offset!=route.index.offset)return false;
    D3D11_BUFFER_DESC desc{};index->GetDesc(&desc);if(desc.ByteWidth!=route.index.bytes)return false;
    std::array<ID3D11Buffer*,32> raw{};std::array<UINT,32> strides{},offsets{};
    context->IAGetVertexBuffers(0,32,raw.data(),strides.data(),offsets.data());
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,32> owned;for(unsigned i=0;i<32;++i)owned[i].Attach(raw[i]);
    for(std::size_t i=0;i<route.vertices.size();++i) {
        const auto& expected=route.vertices[i];
        if(reinterpret_cast<std::uintptr_t>(raw[i])!=expected.resource || strides[i]!=expected.stride || offsets[i]!=expected.offset)return false;
        raw[i]->GetDesc(&desc);if(desc.ByteWidth!=expected.bytes)return false;
    }
    // Slots outside the native format may retain unrelated stale bindings.
    return true;
}
}
