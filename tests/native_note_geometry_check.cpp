#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_note_geometry.hpp"
#include <map>
#include <cassert>
#include <iostream>
#include <functional>
using Microsoft::WRL::ComPtr;
namespace r=d2vr::rig;
struct Memory {
    std::map<std::uintptr_t,unsigned char> data;
    std::function<void(std::uintptr_t)> before;
    template<class T>void put(std::uintptr_t at,const T& value){auto* bytes=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)data[at+i]=bytes[i];}
    bool operator()(std::uintptr_t at,void* output,std::size_t size){if(before)before(at);auto* bytes=static_cast<unsigned char*>(output);for(std::size_t i=0;i<size;++i){auto p=data.find(at+i);if(p==data.end())return false;bytes[i]=p->second;}return true;}
};
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    auto buffer=[&](UINT bytes,UINT flags){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=flags;ComPtr<ID3D11Buffer> b;assert(SUCCEEDED(device->CreateBuffer(&d,nullptr,&b)));return b;};
    auto vertex=buffer(36,D3D11_BIND_VERTEX_BUFFER),index=buffer(6,D3D11_BIND_INDEX_BUFFER),other=buffer(36,D3D11_BIND_VERTEX_BUFFER);
    constexpr std::uintptr_t base=0x140000000,surface=0x100000,geometry=0x200000,vd=0x300000,id=0x310000,vw=0x400000,iw=0x410000,ranges=0x500000;
    Memory m;m.put(surface+0x90,geometry);m.put(geometry,base+0x1ab3a20);m.put(geometry+0x34,3u);m.put(geometry+0x38,3u);m.put(geometry+0x9c,std::uint8_t{2});
    m.put(surface+0xa8,r::List{ranges,1,1<<8});m.put(ranges,std::array<std::uint32_t,3>{0,0,3});
    m.put(geometry+0xa0,r::List{vd,1,1<<8});m.put(geometry+0x298,r::List{id,1,1<<8});
    m.put(vd,std::array<std::uint64_t,4>{vw,1,0,12});m.put(id,std::array<std::uint64_t,4>{iw,1,0,0});
    const auto wrapper=[&](std::uintptr_t at,ID3D11Buffer* resource,UINT bytes,UINT flags){std::array<std::uint32_t,8> words{};auto token=reinterpret_cast<std::uintptr_t>(resource);words[0]=static_cast<std::uint32_t>(token);words[1]=static_cast<std::uint32_t>(token>>32);words[3]=bytes;words[7]=flags;m.put(at,words);};
    wrapper(vw,vertex.Get(),36,1);wrapper(iw,index.Get(),6,2);
    auto route=r::native_note_geometry(m,base,surface);assert(route && route->vertices.size()==1 && route->count==3);
    auto* vb=vertex.Get();UINT stride=12,offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);context->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assert(r::matches_native_note_geometry(context.Get(),*route,3,1,0,0));
    assert(!r::matches_native_note_geometry(context.Get(),*route,6,1,0,0));assert(!r::matches_native_note_geometry(context.Get(),*route,3,2,0,0));
    assert(!r::matches_native_note_geometry(context.Get(),*route,3,1,1,0));assert(!r::matches_native_note_geometry(context.Get(),*route,3,1,0,1));
    vb=other.Get();context->IASetVertexBuffers(0,1,&vb,&stride,&offset);assert(!r::matches_native_note_geometry(context.Get(),*route,3,1,0,0));
    auto bad=m;bad.put(vd+16,std::uint64_t{999});assert(!r::native_note_geometry(bad,base,surface));
    bad=m;bad.put(ranges,std::array<std::uint32_t,3>{0,1,3});assert(!r::native_note_geometry(bad,base,surface));
    bad=m;unsigned hits{};bad.before=[&](std::uintptr_t at){if(at==vw && ++hits==2)bad.put(vw,std::uint64_t{0x800000});};assert(!r::native_note_geometry(bad,base,surface));
    std::cout<<"PASS native note geometry: exact native buffer/stride/index/range match; wrong draw, ownership mutation and unsupported ranges rejected\n";
}
