#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/note_hand_art.hpp"
#include "../src/render_size.hpp"
#include <cassert>
#include <iostream>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    const auto mesh=[](unsigned side,std::array<std::uint8_t,4> color) {
        auto m=std::make_shared<d2vr::note_hand::Mesh>();m->side=side;
        d2vr::note_hand::Surface s;s.vertices.resize(4);s.indices={0,1,2,2,1,3};s.uv={{{0,1}},{{1,1}},{{0,0}},{{1,0}}};
        s.hand=true;s.diffuse.width=s.diffuse.height=1;s.diffuse.rgba.assign(color.begin(),color.end());m->surfaces.push_back(s);return m;
    };
    auto left=mesh(0,{240,12,20,255}),right=mesh(1,{15,25,230,255});
    const auto geometry=[](double x,double z) {
        d2vr::note_hand::Geometry g;g.vertices={{x-.2,-.2,z},{x+.2,-.2,z},{x-.2,.2,z},{x+.2,.2,z}};g.indices={0,1,2,2,1,3};return g;
    };
    auto a=geometry(0,-.5),b=geometry(0,-1.);std::array<const d2vr::note_hand::Geometry*,2> hands{&a,&b};
    d2vr::NoteHandArt renderer(device.Get());renderer.set_mesh(0,left);renderer.set_mesh(1,right);
    XrView view{XR_TYPE_VIEW};view.pose.orientation.w=1;view.fov={-.78539816f,.78539816f,.78539816f,-.78539816f};
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=128;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> eye;assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&eye)));
    ComPtr<ID3D11RenderTargetView> rtv;assert(SUCCEEDED(device->CreateRenderTargetView(eye.Get(),nullptr,&rtv)));
    auto* original=rtv.Get();context->OMSetRenderTargets(1,&original,nullptr);const D3D11_VIEWPORT original_vp{2,3,90,91,.2f,.8f};context->RSSetViewports(1,&original_vp);
    const auto pixel=[](const d2vr::RgbaPixels& image,unsigned x,unsigned y) {const auto i=(y*image.width+x)*4;return std::array<unsigned,3>{std::to_integer<unsigned>(image.bytes[i]),std::to_integer<unsigned>(image.bytes[i+1]),std::to_integer<unsigned>(image.bytes[i+2])};};
    for(float eye_x:{-.032f,.032f}) {
        const float green[]{0,1,0,1};context->ClearRenderTargetView(rtv.Get(),green);view.pose.position.x=eye_x;
        renderer.draw(eye.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,view,hands);
        const auto image=d2vr::readback_rgba(eye.Get());
        const auto center=pixel(image,64,64);assert(center[0]>210 && center[1]<10 && center[2]<10); // near hand wins even though far hand draws last
        assert((pixel(image,4,4)==std::array<unsigned,3>{0,255,0}));
        D3D11_VIEWPORT restored{};UINT n=1;context->RSGetViewports(&n,&restored);assert(n==1 && std::memcmp(&restored,&original_vp,sizeof(restored))==0);
        ComPtr<ID3D11RenderTargetView> restored_rtv;context->OMGetRenderTargets(1,&restored_rtv,nullptr);assert(restored_rtv.Get()==rtv.Get());
    }
    // Moving the same skinned geometry changes the pixels; depth is cleared for each eye/frame.
    a=geometry(.8,-.5);b=geometry(0,-1.);renderer.draw(eye.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,view,hands);
    const auto moved=pixel(d2vr::readback_rgba(eye.Get()),64,64);assert(moved[2]>180 && moved[0]<10);
    // Native UVs select different texels; sleeves use the same posed topology and render even without the hand-material flag.
    auto uvmesh=mesh(0,{240,12,20,255});uvmesh->surfaces[0].diffuse={2,1,{255,0,0,255,0,0,255,255}};
    auto sleeve=uvmesh->surfaces[0];sleeve.hand=false;uvmesh->surfaces.push_back(sleeve);renderer.set_mesh(0,uvmesh);
    a=geometry(0,-.5);const auto sleeve_geometry=geometry(-.32,-.6);
    a.vertices.insert(a.vertices.end(),sleeve_geometry.vertices.begin(),sleeve_geometry.vertices.end());
    for(auto index:sleeve_geometry.indices)a.indices.push_back(index+4);
    const float green[]{0,1,0,1};context->ClearRenderTargetView(rtv.Get(),green);renderer.draw(eye.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,view,hands);
    const auto uv_image=d2vr::readback_rgba(eye.Get());const auto u0=pixel(uv_image,48,64),u1=pixel(uv_image,79,64);
    assert(u0[0]>u0[2] && u1[2]>u1[0]);assert((pixel(uv_image,20,64)!=std::array<unsigned,3>{0,255,0}));
    bool rejected=false;b.indices[0]=3;try{renderer.draw(eye.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,view,hands);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    auto v1=mesh(0,{255,0,0,255});v1->surfaces[0].uv.clear();rejected=false;
    try{renderer.set_mesh(0,v1);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    std::cout<<"PASS native hand art: stereo textured triangles, shared hand depth, frame depth reset, animated geometry, full native sleeve surfaces, context restoration, topology/v1 rejection\n";
}

