#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_child_capture.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <iostream>
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
static std::array<unsigned char,4> pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,unsigned x,unsigned y){
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));std::array<unsigned char,4> result;
    std::memcpy(result.data(),static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4,4);context->Unmap(staging.Get(),0);return result;
}
int main(){
    { // v713: three messages drawn every frame (headset v712: the rune hint, the counter-serum message) each keep their own slot
        d2vr::NativeMessageSlots slots;std::array<unsigned,3> first{};
        for(UINT64 t=1000;t<1200;t+=16)for(unsigned m=0;m<3;++m){const auto pick=slots.assign(10+m,t);assert(pick);
            if(t==1000)first[m]=pick->slot;assert(pick->slot==first[m] && pick->fresh==(t==1000));}
        assert(first[0]!=first[1] && first[1]!=first[2] && first[0]!=first[2]);
        for(unsigned m=3;m<4;++m)assert(slots.assign(10+m,1200));          // a fourth still fits
        assert(!slots.assign(20,1200));                                      // a fifth never takes a drawn one's slot
        const auto later=slots.assign(20,1600);assert(later && later->fresh); // idle >300 ms: reused
    }
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context,deferred;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    Ptr<ID3D11Texture2D> scene;Ptr<ID3D11RenderTargetView> root;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&scene)));assert(SUCCEEDED(device->CreateRenderTargetView(scene.Get(),nullptr,&root)));
    const FLOAT green[]{0,1,0,1};context->ClearRenderTargetView(root.Get(),green);auto* native=root.Get();context->OMSetRenderTargets(1,&native,nullptr);
    Ptr<ID3DBlob> vcode,pcode;constexpr char vs_source[]="float4 main(uint id:SV_VertexID):SV_Position {float2 p[3]={float2(-.5,-.5),float2(-.5,.5),float2(.5,-.5)};return float4(p[id],0,1);}";
    constexpr char ps_source[]="float4 main():SV_Target{return float4(1,0,0,.5);}";
    assert(SUCCEEDED(D3DCompile(vs_source,sizeof(vs_source)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vcode,nullptr)));
    assert(SUCCEEDED(D3DCompile(ps_source,sizeof(ps_source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&pcode,nullptr)));
    Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;assert(SUCCEEDED(device->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),nullptr,&vs)));
    assert(SUCCEEDED(device->CreatePixelShader(pcode->GetBufferPointer(),pcode->GetBufferSize(),nullptr,&ps)));
    const auto draw=[&](ID3D11DeviceContext* ctx){const D3D11_VIEWPORT vp{0,0,64,64,0,1};ctx->RSSetViewports(1,&vp);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->Draw(3,0);};
    d2vr::NativeHudChildCapture capture;d2vr::NativeHudChildScope child{};child.kind=d2vr::NativeHudChildKind::prompt;child.node=100;child.owner=200;child.epoch=3;
    d2vr::NativeHudChildDraw::current=&child;
    assert(!capture.begin(context.Get(),root.Get(),child,0,0,1)); // no immediate worker fallback
    assert(capture.begin(context.Get(),root.Get(),child,0,0,1,GetCurrentThreadId()));
    assert(!capture.begin(context.Get(),root.Get(),child,0,0,2,GetCurrentThreadId())); // nested transition rejects
    auto redirected=capture.replacement(context.Get(),1,&native);assert(redirected && redirected.Get()!=root.Get());
    auto* isolated=redirected.Get();assert(!capture.replacement(context.Get(),1,&isolated)); // internal filter target passes through
    context->OMSetRenderTargets(1,&isolated,nullptr);draw(context.Get());assert(capture.end(child));
    Ptr<ID3D11RenderTargetView> restored;context->OMGetRenderTargets(1,&restored,nullptr);assert(restored.Get()==root.Get());
    auto art=capture.snapshot(0);assert(art && art.child.owner==200 && art.child.epoch==3);
    assert(pixel(device.Get(),context.Get(),art.image.texture.Get(),0,0)[3]==0);
    assert(pixel(device.Get(),context.Get(),art.image.texture.Get(),20,40)[0]==255);
    assert((pixel(device.Get(),context.Get(),scene.Get(),20,40)==std::array<unsigned char,4>({0,255,0,255})));
    deferred->OMSetRenderTargets(1,&native,nullptr);assert(capture.begin(deferred.Get(),root.Get(),child,0,1,2));draw(deferred.Get());assert(capture.end(child));
    assert(!capture.snapshot(0,1));Ptr<ID3D11CommandList> list;assert(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)));capture.finished(deferred.Get(),list.Get());assert(!capture.snapshot(0,1));
    context->ExecuteCommandList(list.Get(),TRUE);capture.executed(list.Get());auto right=capture.snapshot(0,1);assert(right && right.image.texture.Get()!=art.image.texture.Get());
    assert(pixel(device.Get(),context.Get(),right.image.texture.Get(),20,40)[3]>=127);
    std::array<unsigned char,48> alpha{};alpha[16+4+3]=1;alpha[32+8+3]=255;
    const auto box=d2vr::NativeHudChildCapture::alpha_bounds(alpha.data(),3,3,16,alpha.size());assert(box && box->left==1 && box->top==1 && box->right==3 && box->bottom==3);
    assert(!d2vr::NativeHudChildCapture::alpha_bounds(alpha.data(),3,3,16,32));
    capture.clear();assert(!capture.snapshot(0));d2vr::NativeHudChildDraw::current=nullptr;
    std::cout<<"PASS: native child scoped alpha pixels, original root restoration/suppression, filter passthrough, owned deferred completion and measured bounds\n";
}
