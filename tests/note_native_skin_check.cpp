#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include "../src/note_native_skin.hpp"
#include "../src/native_note_geometry.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <cstring>
using Microsoft::WRL::ComPtr;
using Skin=d2vr::NoteNativeSkin;
int main() {
    Skin::Rows identity{1,0,0,0,0,1,0,0,0,0,1,0};
    std::vector<Skin::Rows> animated(69,identity),inverse(69,identity);
    animated[0][3]=.9f;inverse[0][7]=2;
    auto packed=Skin::pack(animated,inverse);assert(packed.size()==72 && packed[0][3]==.9f && packed[0][7]==2 && packed[71]==identity);
    assert(Skin::pack(std::vector<Skin::Rows>(20,identity),std::vector<Skin::Rows>(20,identity)).size()==24);
    bool rejected=false;animated[0][0]=std::numeric_limits<float>::quiet_NaN();try{Skin::pack(animated,inverse);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    const auto buffer=[&](UINT size,UINT flags,const void* bytes) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=flags;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=bytes;ComPtr<ID3D11Buffer> result;assert(SUCCEEDED(device->CreateBuffer(&desc,&data,&result)));return result;
    };
    std::vector<Skin::Rows> native(72,identity);auto current=buffer(3456,D3D11_BIND_SHADER_RESOURCE,native.data());
    auto previous=buffer(3456,D3D11_BIND_SHADER_RESOURCE,native.data());auto unrelated=buffer(3456,D3D11_BIND_SHADER_RESOURCE,native.data());
    D3D11_SHADER_RESOURCE_VIEW_DESC viewdesc{};viewdesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;viewdesc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;viewdesc.Buffer.NumElements=216;
    const auto view=[&](ID3D11Buffer* source){ComPtr<ID3D11ShaderResourceView> result;assert(SUCCEEDED(device->CreateShaderResourceView(source,&viewdesc,&result)));return result;};
    auto cv=view(current.Get()),pv=view(previous.Get()),uv=view(unrelated.Get());
    native[0][3]=.9f;Skin::Snapshot snapshot{1,7,100,{{reinterpret_cast<std::uintptr_t>(current.Get()),reinterpret_cast<std::uintptr_t>(previous.Get()),native}}};
    auto prepared=Skin::prepare(device.Get(),snapshot);
    const char* code="Buffer<float4> pose:register(t3); Buffer<float4> oldpose:register(t4); float4 vs(float2 p:POSITION):SV_POSITION{return float4(p.x+(pose[0].w+oldpose[0].w)*.5,p.y,0,1);} float4 ps():SV_TARGET{return float4(1,0,0,1);}";
    ComPtr<ID3DBlob> vsbytes,psbytes,error;
    assert(SUCCEEDED(D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vsbytes,&error)));
    assert(SUCCEEDED(D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&psbytes,&error)));
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    assert(SUCCEEDED(device->CreateVertexShader(vsbytes->GetBufferPointer(),vsbytes->GetBufferSize(),nullptr,&vs)));
    assert(SUCCEEDED(device->CreatePixelShader(psbytes->GetBufferPointer(),psbytes->GetBufferSize(),nullptr,&ps)));
    D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};ComPtr<ID3D11InputLayout> layout;
    assert(SUCCEEDED(device->CreateInputLayout(&element,1,vsbytes->GetBufferPointer(),vsbytes->GetBufferSize(),&layout)));
    const float vertices[]{-.4f,-.4f,0,.4f,.4f,-.4f};const unsigned indices[]{0,1,2};
    auto vertex=buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices),index=buffer(sizeof(indices),D3D11_BIND_INDEX_BUFFER,indices);
    auto* vb=vertex.Get();UINT stride=8,offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);context->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R32_UINT,0);
    context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    ID3D11ShaderResourceView* resources[]{cv.Get(),pv.Get(),uv.Get()};context->VSSetShaderResources(3,3,resources);context->PSSetShaderResources(9,1,&resources[2]);
    context->PSSetShaderResources(7,1,&resources[0]);context->GSSetShaderResources(2,1,&resources[0]);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=32;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)));ComPtr<ID3D11RenderTargetView> rtv;assert(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    auto* rt=rtv.Get();context->OMSetRenderTargets(1,&rt,nullptr);D3D11_VIEWPORT vp{0,0,32,32,0,1};context->RSSetViewports(1,&vp);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;assert(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));context->RSSetState(raster.Get());
    const auto center=[&](){D3D11_TEXTURE2D_DESC sd=td;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&sd,nullptr,&staging)));context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));auto* pixel=static_cast<unsigned char*>(mapped.pData)+16*mapped.RowPitch+16*4;std::array<unsigned,3> result{pixel[0],pixel[1],pixel[2]};context->Unmap(staging.Get(),0);return result;};
    float green[]{0,1,0,1};context->ClearRenderTargetView(rtv.Get(),green);context->DrawIndexed(3,0,0);assert(center()[0]==255);
    ComPtr<ID3D11ShaderResourceView> cached;
    for(unsigned pass=0;pass<2;++pass) {
        context->ClearRenderTargetView(rtv.Get(),green);
        {Skin::Scope scope(context.Get(),prepared);assert(scope.replaced()==3 && scope.models()==1);
            std::array<ID3D11ShaderResourceView*,3> observed{};context->VSGetShaderResources(3,3,observed.data());
            assert(observed[0]!=cv.Get() && observed[1]!=pv.Get() && observed[0]==observed[1] && observed[2]==uv.Get());
            if(cached)assert(cached.Get()==observed[0]);else cached=observed[0];for(auto* srv:observed)srv->Release();
            ComPtr<ID3D11ShaderResourceView> pixel,inactive;context->PSGetShaderResources(7,1,&pixel);context->GSGetShaderResources(2,1,&inactive);
            assert(pixel.Get()==cached.Get() && inactive.Get()==cv.Get());context->DrawIndexed(3,0,0);
        }
        assert(center()[1]==255);
        std::array<ID3D11ShaderResourceView*,3> restored{};context->VSGetShaderResources(3,3,restored.data());
        for(unsigned i=0;i<3;++i){assert(restored[i]==resources[i]);restored[i]->Release();}
        ComPtr<ID3D11ShaderResourceView> pixel;context->PSGetShaderResources(7,1,&pixel);assert(pixel.Get()==cv.Get());
    }
    {Skin::Scope inactive(context.Get(),prepared,false);assert(inactive.replaced()==0 && inactive.models()==0);}
    {Skin::Scope filtered(context.Get(),prepared,true,2);assert(filtered.replaced()==0 && filtered.models()==0);}
    {Skin::Scope allowed(context.Get(),prepared,true,1);assert(allowed.replaced()==3 && allowed.models()==1);}
    ComPtr<ID3D11Buffer> vb_after,ib_after;UINT st{},of{},io{};DXGI_FORMAT fmt{};context->IAGetVertexBuffers(0,1,&vb_after,&st,&of);context->IAGetIndexBuffer(&ib_after,&fmt,&io);
    assert(vb_after.Get()==vertex.Get() && ib_after.Get()==index.Get() && st==8 && of==0 && io==0 && fmt==DXGI_FORMAT_R32_UINT);
    ComPtr<ID3D11VertexShader> vs_after;ComPtr<ID3D11PixelShader> ps_after;context->VSGetShader(&vs_after,nullptr,nullptr);context->PSGetShader(&ps_after,nullptr,nullptr);
    assert(vs_after.Get()==vs.Get() && ps_after.Get()==ps.Get());ComPtr<ID3D11ShaderResourceView> material;context->PSGetShaderResources(9,1,&material);assert(material.Get()==uv.Get());
    // Deferred recording must retain the replacement through execution while its
    // subsequent draw uses the restored native palette on the same geometry.
    ComPtr<ID3D11DeviceContext> deferred;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    deferred->IASetVertexBuffers(0,1,&vb,&stride,&offset);deferred->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R32_UINT,0);
    deferred->IASetInputLayout(layout.Get());deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deferred->VSSetShader(vs.Get(),nullptr,0);deferred->PSSetShader(ps.Get(),nullptr,0);
    deferred->VSSetShaderResources(3,3,resources);deferred->PSSetShaderResources(9,1,&resources[2]);deferred->PSSetShaderResources(7,1,&resources[0]);
    deferred->OMSetRenderTargets(1,&rt,nullptr);deferred->RSSetViewports(1,&vp);deferred->RSSetState(raster.Get());
    d2vr::rig::GeometryRoute route{};route.vertices.push_back({reinterpret_cast<std::uintptr_t>(vertex.Get()),0,8,sizeof(vertices)});
    route.index={reinterpret_cast<std::uintptr_t>(index.Get()),0,0,sizeof(indices)};route.count=3;route.format=DXGI_FORMAT_R32_UINT;
    assert(d2vr::rig::matches_native_note_geometry(deferred.Get(),route,3,1,0,0));
    const float blue[]{0,0,1,1};context->ClearRenderTargetView(rtv.Get(),blue);
    deferred->ClearRenderTargetView(rtv.Get(),green);
    {Skin::Scope scope(deferred.Get(),prepared,true,1);assert(scope.replaced()==3 && scope.models()==1);
        ComPtr<ID3D11ShaderResourceView> replaced;deferred->VSGetShaderResources(3,1,&replaced);assert(replaced.Get()==cached.Get());
        deferred->DrawIndexed(3,0,0);
    }
    ComPtr<ID3D11ShaderResourceView> restored_deferred;deferred->VSGetShaderResources(3,1,&restored_deferred);assert(restored_deferred.Get()==cv.Get());
    assert(d2vr::rig::matches_native_note_geometry(deferred.Get(),route,3,1,0,0));
    ComPtr<ID3D11CommandList> commands;assert(SUCCEEDED(deferred->FinishCommandList(TRUE,&commands)));
    assert(center()[2]==255);context->ExecuteCommandList(commands.Get(),TRUE);assert(center()[1]==255);
    commands.Reset();deferred->ClearRenderTargetView(rtv.Get(),green);deferred->DrawIndexed(3,0,0);
    assert(SUCCEEDED(deferred->FinishCommandList(TRUE,&commands)));context->ExecuteCommandList(commands.Get(),TRUE);assert(center()[0]==255);
    std::cout<<"PASS native note skin: affine packing, exact current/previous resources, cache/filter, immediate/deferred native IA and shaders, moved pixels after ExecuteCommandList, scope restoration\n";
}
