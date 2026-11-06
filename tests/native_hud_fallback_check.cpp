#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_fallback.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using Microsoft::WRL::ComPtr;
static void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Snapshot {
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{}; ComPtr<ID3D11ClassInstance> ps_class;UINT ps_classes{};
    ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11Buffer> constant;ComPtr<ID3D11Buffer> ps_constant;
    ComPtr<ID3D11BlendState> blend;FLOAT factor[4]{};UINT mask{};ComPtr<ID3D11DepthStencilState> depth;UINT stencil{};
    ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;ComPtr<ID3D11RasterizerState> raster;
    std::array<D3D11_VIEWPORT,16> viewport{};UINT count=16;ComPtr<ID3D11Predicate> predicate;BOOL predicate_value{};ComPtr<ID3D11UnorderedAccessView> uav;
    explicit Snapshot(ID3D11DeviceContext* c){c->VSGetShader(&vs,nullptr,nullptr);std::array<ID3D11ClassInstance*,256> instances{};ps_classes=256;c->PSGetShader(&ps,instances.data(),&ps_classes);if(ps_classes)ps_class.Attach(instances[0]);for(unsigned i=1;i<ps_classes;++i)instances[i]->Release();c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);c->PSGetShaderResources(0,1,&srv);c->PSGetSamplers(0,1,&sampler);c->VSGetConstantBuffers(0,1,&constant);c->PSGetConstantBuffers(0,1,&ps_constant);
        c->OMGetBlendState(&blend,factor,&mask);c->OMGetDepthStencilState(&depth,&stencil);c->OMGetRenderTargets(1,&rtv,&dsv);c->RSGetState(&raster);c->RSGetViewports(&count,viewport.data());c->GetPredication(&predicate,&predicate_value);c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,1,&uav);}
    void equals(const Snapshot& b)const {require(vs==b.vs && ps==b.ps && gs==b.gs && hs==b.hs && ds==b.ds,"shader stages restored");require(ps_class==b.ps_class && ps_classes==b.ps_classes,"shader class instances restored");
        require(layout==b.layout && topology==b.topology,"IA restored");require(srv==b.srv && sampler==b.sampler && constant==b.constant && ps_constant==b.ps_constant,"resource slots restored");
        require(blend==b.blend && mask==b.mask && !memcmp(factor,b.factor,sizeof factor),"blend restored");require(depth==b.depth && stencil==b.stencil,"depth restored");
        require(rtv==b.rtv && dsv==b.dsv && raster==b.raster,"targets/raster restored");require(count==b.count && !memcmp(viewport.data(),b.viewport.data(),count*sizeof(D3D11_VIEWPORT)),"viewports restored");
        require(predicate==b.predicate && predicate_value==b.predicate_value,"predication restored");require(uav==b.uav,"pixel UAV restored");}
};
int main(){try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> immediate,deferred;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&immediate)),"WARP device");
    require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"deferred context");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=4;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> scene,scene2,art;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&scene)),"scene");require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&scene2)),"scaled scene");
    std::array<unsigned char,64> pixels{};for(unsigned i=0;i<16;++i){pixels[i*4]=128;pixels[i*4+3]=128;}
    D3D11_SUBRESOURCE_DATA data{pixels.data(),16,0};require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&art)),"art");
    D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> root;require(SUCCEEDED(device->CreateRenderTargetView(scene.Get(),&rd,&root)),"typed root");
    ComPtr<ID3D11RenderTargetView> root2;require(SUCCEEDED(device->CreateRenderTargetView(scene2.Get(),&rd,&root2)),"typed scaled root");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=rd.Format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> source;require(SUCCEEDED(device->CreateShaderResourceView(art.Get(),&sd,&source)),"baseline SRV");
    D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampling.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;require(SUCCEEDED(device->CreateSamplerState(&sampling,&sampler)),"sampler");
    D3D11_RASTERIZER_DESC rasterdesc{};rasterdesc.FillMode=D3D11_FILL_WIREFRAME;rasterdesc.CullMode=D3D11_CULL_FRONT;rasterdesc.ScissorEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;require(SUCCEEDED(device->CreateRasterizerState(&rasterdesc,&raster)),"raster");
    D3D11_BLEND_DESC blending{};blending.RenderTarget[0].RenderTargetWriteMask=3;
    ComPtr<ID3D11BlendState> blend;require(SUCCEEDED(device->CreateBlendState(&blending,&blend)),"blend");
    D3D11_DEPTH_STENCIL_DESC depthdesc{};depthdesc.DepthEnable=TRUE;depthdesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depthdesc.DepthFunc=D3D11_COMPARISON_LESS;
    ComPtr<ID3D11DepthStencilState> depth;require(SUCCEEDED(device->CreateDepthStencilState(&depthdesc,&depth)),"depth");
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> constant;require(SUCCEEDED(device->CreateBuffer(&cb,nullptr,&constant)),"constant");
    const char shader[]="float4 VS(float4 p:POSITION):SV_POSITION{return p;} interface IColor {float4 color();}; class NativeColor:IColor {float4 color(){return float4(0,1,0,1);}}; IColor activeColor; float4 PS():SV_TARGET{return activeColor.color();}";
    ComPtr<ID3DBlob> vcode,pcode,error;require(SUCCEEDED(D3DCompile(shader,sizeof shader,nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,&vcode,&error)),"VS compile");require(SUCCEEDED(D3DCompile(shader,sizeof shader,nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&pcode,&error)),"PS compile");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;require(SUCCEEDED(device->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),nullptr,&vs)),"VS");ComPtr<ID3D11ClassLinkage> linkage;ComPtr<ID3D11ClassInstance> instance;require(SUCCEEDED(device->CreateClassLinkage(&linkage)),"class linkage");require(SUCCEEDED(device->CreatePixelShader(pcode->GetBufferPointer(),pcode->GetBufferSize(),linkage.Get(),&ps)),"PS");require(SUCCEEDED(linkage->CreateClassInstance("NativeColor",0,0,0,0,&instance)),"class instance");
    D3D11_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};ComPtr<ID3D11InputLayout> layout;require(SUCCEEDED(device->CreateInputLayout(&input,1,vcode->GetBufferPointer(),vcode->GetBufferSize(),&layout)),"layout");
    const char stages[]=R"(
struct V{float4 p:SV_POSITION;};
[maxvertexcount(1)]void GS(point V v[1],inout PointStream<V> s){s.Append(v[0]);}
struct P{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};
P constants(InputPatch<V,3> p,uint id:SV_PrimitiveID){P o;o.e[0]=o.e[1]=o.e[2]=o.i=1;return o;}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("constants")]
V HS(InputPatch<V,3> p,uint i:SV_OutputControlPointID){return p[i];}
[domain("tri")]V DS(P p,float3 uv:SV_DomainLocation,const OutputPatch<V,3> patch){V o;o.p=patch[0].p;return o;}
)";
    ComPtr<ID3DBlob> gcode,hcode,dcode;
    require(SUCCEEDED(D3DCompile(stages,sizeof stages,nullptr,nullptr,nullptr,"GS","gs_5_0",0,0,&gcode,&error)),"GS compile");
    require(SUCCEEDED(D3DCompile(stages,sizeof stages,nullptr,nullptr,nullptr,"HS","hs_5_0",0,0,&hcode,&error)),"HS compile");
    require(SUCCEEDED(D3DCompile(stages,sizeof stages,nullptr,nullptr,nullptr,"DS","ds_5_0",0,0,&dcode,&error)),"DS compile");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    require(SUCCEEDED(device->CreateGeometryShader(gcode->GetBufferPointer(),gcode->GetBufferSize(),nullptr,&gs)),"GS");
    require(SUCCEEDED(device->CreateHullShader(hcode->GetBufferPointer(),hcode->GetBufferSize(),nullptr,&hs)),"HS");
    require(SUCCEEDED(device->CreateDomainShader(dcode->GetBufferPointer(),dcode->GetBufferSize(),nullptr,&ds)),"DS");
    D3D11_QUERY_DESC q{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;require(SUCCEEDED(device->CreatePredicate(&q,&predicate)),"predicate");
    D3D11_BUFFER_DESC uavdesc{};uavdesc.ByteWidth=16;uavdesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Buffer> uavbuffer;require(SUCCEEDED(device->CreateBuffer(&uavdesc,nullptr,&uavbuffer)),"UAV buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_UINT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=4;
    ComPtr<ID3D11UnorderedAccessView> uav;require(SUCCEEDED(device->CreateUnorderedAccessView(uavbuffer.Get(),&ud,&uav)),"pixel UAV");
    d2vr::NativeHudFallback fallback;unsigned iteration=0;
    for(auto* c:{immediate.Get(),deferred.Get()}) {++iteration;
        const float blue[4]{0,0,1,1};c->ClearRenderTargetView(root.Get(),blue);auto* target=root.Get();c->OMSetRenderTargets(1,&target,nullptr);
        c->VSSetShader(vs.Get(),nullptr,0);auto* dynamic=instance.Get();c->PSSetShader(ps.Get(),&dynamic,1);c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        auto* resource=source.Get();c->PSSetShaderResources(0,1,&resource);auto* sample=sampler.Get();c->PSSetSamplers(0,1,&sample);auto* buffer=constant.Get();c->VSSetConstantBuffers(0,1,&buffer);
        const float factors[4]{.1f,.2f,.3f,.4f};c->OMSetBlendState(blend.Get(),factors,0x1234);c->OMSetDepthStencilState(depth.Get(),37);c->RSSetState(raster.Get());
        const D3D11_VIEWPORT vp[2]{{1,1,2,2,.2f,.8f},{0,0,1,1,0,1}};c->RSSetViewports(2,vp);
        c->GSSetShader(gs.Get(),nullptr,0);c->HSSetShader(hs.Get(),nullptr,0);c->DSSetShader(ds.Get(),nullptr,0);c->SetPredication(predicate.Get(),TRUE); auto* pixeluav=uav.Get();c->OMSetRenderTargetsAndUnorderedAccessViews(1,&target,nullptr,1,1,&pixeluav,nullptr); Snapshot before(c);require(fallback.blit(c,root.Get(),art.Get(),rd.Format),"fallback blit");Snapshot after(c);before.equals(after);
        require(!fallback.blit(c,root.Get(),scene.Get(),rd.Format),"reject self feedback");
        {   // v1013: the same alpha path scaled into a sub-rectangle, as the watch composites native art. Restore the
            // bound target and the PS constant-buffer slot around the draw.
            c->SetPredication(nullptr,FALSE); // the previous blit restored the test's occlusion predicate
            auto* scaled_target=root2.Get();c->OMSetRenderTargets(1,&scaled_target,nullptr);c->ClearRenderTargetView(scaled_target,blue);
            Snapshot before_scaled(c);
            require(fallback.blit_scaled(c,root2.Get(),art.Get(),D3D11_BOX{0,0,0,4,4,1},D3D11_RECT{1,1,3,3},rd.Format),"scaled fallback blit");
            Snapshot after_scaled(c);before_scaled.equals(after_scaled);
        }
        if(c==deferred.Get()){ComPtr<ID3D11CommandList> list;require(SUCCEEDED(c->FinishCommandList(TRUE,&list)),"finish deferred fallback");immediate->ExecuteCommandList(list.Get(),TRUE);}
        immediate->SetPredication(nullptr,FALSE);auto stagingdesc=desc;stagingdesc.BindFlags=0;stagingdesc.Usage=D3D11_USAGE_STAGING;stagingdesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
        require(SUCCEEDED(device->CreateTexture2D(&stagingdesc,nullptr,&staging)),"staging");immediate->CopyResource(staging.Get(),scene.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"read pixels");
        const auto* pixel=static_cast<unsigned char*>(mapped.pData);const bool good=std::abs(pixel[0]-128)<=1 && pixel[1]==0 && std::abs(pixel[2]-127)<=1 && pixel[3]==255;immediate->Unmap(staging.Get(),0);require(good,"same-queue native premultiplied alpha over scene");
        ComPtr<ID3D11Texture2D> staging2;require(SUCCEEDED(device->CreateTexture2D(&stagingdesc,nullptr,&staging2)),"scaled staging");
        immediate->CopyResource(staging2.Get(),scene2.Get());D3D11_MAPPED_SUBRESOURCE scaled{};require(SUCCEEDED(immediate->Map(staging2.Get(),0,D3D11_MAP_READ,0,&scaled)),"read scaled pixels");
        const auto* scaled_pixels=static_cast<const unsigned char*>(scaled.pData);
        const auto* outside=scaled_pixels;const auto* inside=scaled_pixels+scaled.RowPitch+4;
        const bool scaled_good=outside[0]==0 && outside[1]==0 && outside[2]==255 && outside[3]==255 &&
            std::abs(inside[0]-128)<=1 && inside[1]==0 && std::abs(inside[2]-127)<=1 && inside[3]==255;
        if(!scaled_good)std::cerr<<"iteration "<<iteration<<" scaled outside "<<int(outside[0])<<','<<int(outside[1])<<','<<int(outside[2])<<','<<int(outside[3])
            <<" inside "<<int(inside[0])<<','<<int(inside[1])<<','<<int(inside[2])<<','<<int(inside[3])<<" pitch "<<scaled.RowPitch<<'\n';
        immediate->Unmap(staging2.Get(),0);require(scaled_good,"scaled premultiplied alpha in the requested sub-rect");
    }
    // v1014: the watch fade is a constant alpha in blit_scaled's compound constant buffer. A premultiplied source
    // fades by multiplying rgb and a exactly: WARP readback at alpha 0, 0.5 and 1 over a transparent target.
    {
        D3D11_TEXTURE2D_DESC fd{};fd.Width=fd.Height=4;fd.MipLevels=1;fd.ArraySize=1;fd.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        fd.SampleDesc.Count=1;fd.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> faded;require(SUCCEEDED(device->CreateTexture2D(&fd,nullptr,&faded)),"fade target");
        D3D11_RENDER_TARGET_VIEW_DESC frd{};frd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;frd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> fade_rtv;require(SUCCEEDED(device->CreateRenderTargetView(faded.Get(),&frd,&fade_rtv)),"fade target view");
        auto staging_desc=fd;staging_desc.BindFlags=0;staging_desc.Usage=D3D11_USAGE_STAGING;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> fade_staging;require(SUCCEEDED(device->CreateTexture2D(&staging_desc,nullptr,&fade_staging)),"fade staging");
        const float transparent[4]{0,0,0,0};
        const std::array<float,3> fades{0.f,.5f,1.f};
        for(auto alpha:fades) {
            auto* target=fade_rtv.Get();immediate->OMSetRenderTargets(1,&target,nullptr);
            immediate->ClearRenderTargetView(target,transparent);
            require(fallback.blit_scaled(immediate.Get(),target,art.Get(),D3D11_BOX{0,0,0,4,4,1},D3D11_RECT{0,0,4,4},frd.Format,true,alpha),"fade blit");
            immediate->CopyResource(fade_staging.Get(),faded.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(immediate->Map(fade_staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"read fade pixels");
            const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+mapped.RowPitch*2+8;
            const auto expected=static_cast<unsigned>(std::lround(128.f*alpha));
            const bool good=std::abs(static_cast<int>(pixel[0])-static_cast<int>(expected))<=1 && pixel[1]==0 &&
                std::abs(static_cast<int>(pixel[3])-static_cast<int>(expected))<=1;
            if(!good)std::cerr<<"fade "<<alpha<<" got "<<int(pixel[0])<<','<<int(pixel[1])<<','<<int(pixel[2])<<','<<int(pixel[3])
                <<" expected premultiplied "<<expected<<'\n';
            immediate->Unmap(fade_staging.Get(),0);require(good,"premultiplied fade multiplies rgb and a");
        }
    }
    fallback.clear();std::cout<<"PASS: immediate/deferred same-queue alpha fallback, scaled sub-rect alpha, typeless art with typed SRV, PS constant state restored, self feedback rejected, premultiplied fade at alpha 0/0.5/1 (WARP readback)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}




