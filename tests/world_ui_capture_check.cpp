#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/world_ui_capture.hpp"
#include "../src/context_state.hpp"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
struct Target {Ptr<ID3D11Texture2D> texture;Ptr<ID3D11RenderTargetView> view;};
static Target target(ID3D11Device* device,UINT width=64,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=64;d.MipLevels=d.ArraySize=1;d.Format=format;
    d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Target out;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&out.texture)),"create target");
    check(SUCCEEDED(device->CreateRenderTargetView(out.texture.Get(),nullptr,&out.view)),"create view");return out;
}
static std::array<unsigned char,4> pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,UINT x,UINT y) {
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Texture2D> staging;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"staging");
    context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE m{};
    check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)),"readback");
    const auto* p=static_cast<const unsigned char*>(m.pData)+y*m.RowPitch+x*4;
    const std::array<unsigned char,4> out{p[0],p[1],p[2],p[3]};context->Unmap(staging.Get(),0);return out;
}
static Ptr<ID3DBlob> shader(const char* source,const char* profile) {
    Ptr<ID3DBlob> code;check(SUCCEEDED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",profile,0,0,&code,nullptr)),"shader compile");return code;
}
int main() {
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        auto scene=target(device.Get());const FLOAT green[4]{0,1,0,1};context->ClearRenderTargetView(scene.view.Get(),green);
        auto* native=scene.view.Get();context->OMSetRenderTargets(1,&native,nullptr);
        auto vsCode=shader("float4 main(uint id:SV_VertexID):SV_Position {float2 p[3]={float2(-.5,-.5),float2(-.5,.5),float2(.5,-.5)};return float4(p[id],0,1);}","vs_5_0");
        auto psCode=shader("float4 main():SV_Target {return float4(1,0,0,.5);}","ps_5_0");
        Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;
        check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)),"vertex shader");
        check(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps)),"pixel shader");
        const auto draw=[&](ID3D11DeviceContext* ctx,ID3D11RenderTargetView* rtv) {
            ctx->OMSetRenderTargets(1,&rtv,nullptr);const D3D11_VIEWPORT vp{0,0,64,64,0,1};ctx->RSSetViewports(1,&vp);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->Draw(3,0);
        };
        d2vr::WorldUiCapture capture;d2vr::ContextState isolated(device.Get());
        {
            d2vr::ContextState::Scope scope(isolated);
            auto redirected=capture.redirect(context.Get(),scene.view.Get(),0,0,1);check(bool(redirected),"redirect wheel");
            check(redirected.Get()!=native,"capture must be distinct");draw(context.Get(),redirected.Get());
            auto repeated=capture.redirect(context.Get(),scene.view.Get(),0,0,1);check(repeated.Get()==redirected.Get(),"same scope target stable");
        }
        Ptr<ID3D11RenderTargetView> restored;context->OMGetRenderTargets(1,&restored,nullptr);
        check(restored.Get()==native,"native RTV restored");
        const auto wheel=capture.snapshot(0);check(wheel && wheel.width==64 && wheel.height==64 && wheel.epoch && wheel.recorded_tick,"snapshot metadata");
        const auto inside=pixel(device.Get(),context.Get(),wheel.texture.Get(),20,40);
        check(inside[0]==255 && inside[1]==0 && inside[3]>=127 && inside[3]<=128,"native draw captured with alpha and not cleared on repeated bind");
        const auto outside=pixel(device.Get(),context.Get(),wheel.texture.Get(),0,0);check(outside[3]==0,"transparent capture border");
        const auto scenePixel=pixel(device.Get(),context.Get(),scene.texture.Get(),20,40);check(scenePixel[0]==0 && scenePixel[1]==255 && scenePixel[3]==255,"scene untouched");
        {
            d2vr::ContextState::Scope scope(isolated);
            auto fresh=capture.redirect(context.Get(),scene.view.Get(),0,0,4);check(bool(fresh),"new scope redirect");
            check(pixel(device.Get(),context.Get(),wheel.texture.Get(),20,40)[3]==0,"new scope clears previous movie pixels");
            D3D11_BLEND_DESC b{};b.RenderTarget[0].BlendEnable=TRUE;
            b.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;b.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;b.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
            b.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;b.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;b.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
            b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            Ptr<ID3D11BlendState> blend;check(SUCCEEDED(device->CreateBlendState(&b,&blend)),"alpha blend state");
            context->OMSetBlendState(blend.Get(),nullptr,~0u);draw(context.Get(),fresh.Get());
            const auto blended=pixel(device.Get(),context.Get(),wheel.texture.Get(),20,40);
            check(blended[0]>=127 && blended[0]<=128 && blended[3]>=127 && blended[3]<=128,"capture preserves alpha blending onto transparency");
        }
        Ptr<ID3D11DeviceContext> deferred;check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"deferred context");
        auto notes=capture.redirect(deferred.Get(),scene.view.Get(),1,1,2);check(bool(notes),"deferred redirect notes eye1");draw(deferred.Get(),notes.Get());
        Ptr<ID3D11CommandList> list;check(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"finish movie list");context->ExecuteCommandList(list.Get(),TRUE);
        const auto noteImage=capture.snapshot(1,1);check(noteImage.texture.Get()!=wheel.texture.Get(),"movies and eyes isolated");
        check(pixel(device.Get(),context.Get(),noteImage.texture.Get(),20,40)[3]>=127,"deferred UI visible after execution");
        auto resized=target(device.Get(),96);auto resizedRtv=capture.redirect(context.Get(),resized.view.Get(),0,0,3);check(bool(resizedRtv),"resize redirect");
        const auto newWheel=capture.snapshot(0);check(newWheel.width==96 && newWheel.epoch>wheel.epoch && newWheel.texture.Get()!=wheel.texture.Get(),"resize advances resource epoch");
        check(pixel(device.Get(),context.Get(),newWheel.texture.Get(),20,40)[3]==0,"resized capture cleared");
        auto unsupported=target(device.Get(),64,DXGI_FORMAT_R32_FLOAT);check(!capture.redirect(context.Get(),unsupported.view.Get(),0),"reject non-RGBA");
        Ptr<ID3D11Device> other;check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&other,nullptr,nullptr)),"second device");
        auto foreign=target(other.Get());check(!capture.redirect(context.Get(),foreign.view.Get(),0),"reject foreign device");
        check(!capture.redirect(context.Get(),scene.view.Get(),3),"reject unknown movie");
        check(capture.redirects==5 && capture.clears==4 && capture.recreates==3 && capture.rejected==3 && capture.create_failures==0,"capture telemetry");
        capture.clear();check(!capture.snapshot(0),"clear invalidates publication");
        check(pixel(device.Get(),context.Get(),wheel.texture.Get(),20,40)[3]>=127,"snapshot retains old resources across resize and clear");
        std::puts("PASS: world UI alpha capture, native target preservation/restoration, deferred execution, resize/lifetime and rejection checks (WARP)");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
