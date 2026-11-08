#pragma once
// v1106: the lens-hidden area (XR_KHR_visibility_mask) and the part of our render frustum outside the runtime FOV are stamped into
// the scene depth at the reverse-Z near value (1.0) before the first draw of each FORWARD_WORLD pass job, so the forward passes
// (FORWARD_WORLD, FORWARD_DYNAMIC: the depth-tested shading after the prepass) reject those pixels. v1105 headset: 28.2 % of each
// canted eye image (10.9 % lens mask + about 17 % outside the FOV) is never seen.
//
// Placement: after DEPTHPASS, DEPTHMINMAX, SSAO and SCREENSHADOWS have read the clean prepass depth, so the light-cluster depth
// ranges, SSAO and any culling input built from this frame's prepass never see the stamp (Metro VR had to make occlusion per eye
// because one eye's depth culled for the other). Later depth readers (fog, water, alpha, DOF, occlusion queries) do see it; the
// stamped pixels are never displayed. Kill switch build/no-visibility-stamp; live A/B: selftest command "vstamp on|off".
//
// Thread model: geometry is built on the Present thread (GameXr) and published under a mutex; before_draw() runs on the engine's
// job threads while they record deferred contexts. The stamp saves and restores every piece of deferred-context state it changes.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "visibility_mask.hpp"

namespace d2vr {

// Pure: NDC triangles (x, y pairs, three per triangle) covering, in a render frustum, the runtime's hidden mesh and the area outside
// the runtime FOV quad. margin widens the FOV quad by that fraction of its tangent extent on every side (keeps the stamp off visible
// pixels when the engine's projection differs by a texel).
inline std::vector<float> visibility_stamp_triangles(const float* xy,std::size_t vertex_count,const std::uint32_t* indices,std::size_t index_count,
    float left,float right,float up,float down,const RenderFrustum& render,double margin) {
    using visibility_mask_detail::project;
    std::vector<float> out;
    if(!(render.tan_x>0) || !(render.tan_y>0)) return out;
    auto ndc=[&](std::array<double,2> t) {return std::array<double,2>{t[0]/render.tan_x,t[1]/render.tan_y};};
    auto push=[&](std::array<double,2> a,std::array<double,2> b,std::array<double,2> c) {
        for(const auto& p:{a,b,c}) {out.push_back(static_cast<float>(p[0]));out.push_back(static_cast<float>(p[1]));}
    };
    if(xy && indices && index_count%3==0) for(std::size_t t=0;t<index_count;t+=3) {
        std::array<std::array<double,2>,3> q{};bool ok=true;
        for(int k=0;k<3;++k) {
            const auto v=indices[t+k];if(v>=vertex_count) {ok=false;break;}
            q[k]=project(render,xy[2*v],xy[2*v+1]);if(std::isnan(q[k][0])) {ok=false;break;}
            q[k]=ndc(q[k]);
        }
        if(ok) push(q[0],q[1],q[2]);
    }
    const double l=std::tan(left),r=std::tan(right),u=std::tan(up),d=std::tan(down);
    const double mx=(r-l)*margin,my=(u-d)*margin;
    const std::array<std::array<double,2>,4> fov{project(render,l-mx,d-my),project(render,r+mx,d-my),project(render,r+mx,u+my),project(render,l-mx,u+my)};
    for(const auto& p:fov) if(std::isnan(p[0])) return out;
    std::array<std::array<double,2>,4> q{};for(int i=0;i<4;++i) q[i]=ndc(fov[i]);
    // rect minus the convex FOV quad = the union, over the quad's edges, of the rect clipped to that edge's outer half-plane. The
    // pieces overlap at the corners (harmless for a depth stamp) and none reaches inside the quad. Corner order may be either winding.
    double winding=0;for(int i=0;i<4;++i) winding+=q[i][0]*q[(i+1)%4][1]-q[(i+1)%4][0]*q[i][1];
    for(int i=0;i<4;++i) {
        const auto a=q[i],b=q[(i+1)%4];
        // outside(p) > 0: p is on the outer side of edge a->b
        auto outside=[&](const std::array<double,2>& p) {return -((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]))*(winding>0?1:-1);};
        std::vector<std::array<double,2>> poly{{-1,-1},{1,-1},{1,1},{-1,1}},clipped;
        for(std::size_t k=0;k<poly.size();++k) {
            const auto& p=poly[k];const auto& n=poly[(k+1)%poly.size()];
            const double sp=outside(p),sn=outside(n);
            if(sp>=0) clipped.push_back(p);
            if((sp>=0)!=(sn>=0)) {const double t=sp/(sp-sn);clipped.push_back({p[0]+(n[0]-p[0])*t,p[1]+(n[1]-p[1])*t});}
        }
        for(std::size_t k=1;k+1<clipped.size();++k) push(clipped[0],clipped[k],clipped[k+1]);
    }
    return out;
}

class VisibilityStamp {
public:
    static constexpr int forward_world_pass=27; // ARK_RENDERPASS_FORWARD_WORLD (render_pass_table.hpp)
    inline static std::atomic<bool> enabled{false};
    inline static std::array<std::atomic<std::uint64_t>,2> stamps{};
    inline static std::atomic<std::uint64_t> no_eye{},no_depth{},no_geometry{},no_viewport{},failures{};
    inline static std::array<std::atomic<std::uint32_t>,2> triangles{};
    inline static std::array<std::atomic<float>,2> stamped_share{}; // NDC area share of the stamp (the wasted share, minus the margin)
    inline static std::array<std::atomic<std::uint32_t>,2> last_viewport_w{},last_viewport_h{};

    // Present thread, once the device exists.
    static bool create(ID3D11Device* device) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if(vs_) return true;
        static const char source[]="float4 main(float2 p:POSITION):SV_Position{return float4(p,1,1);}";
        Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
        if(FAILED(D3DCompile(source,sizeof source-1,"visibility_stamp",nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors))) return false;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
        const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        if(FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)) ||
           FAILED(device->CreateInputLayout(&element,1,code->GetBufferPointer(),code->GetBufferSize(),&layout))) return false;
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_ALWAYS;ds.StencilEnable=FALSE;
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=FALSE;
        D3D11_BLEND_DESC bs{};bs.RenderTarget[0].RenderTargetWriteMask=0; // depth only even if the game's targets stay bound
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dss;Microsoft::WRL::ComPtr<ID3D11RasterizerState> rss;Microsoft::WRL::ComPtr<ID3D11BlendState> bss;
        if(FAILED(device->CreateDepthStencilState(&ds,&dss)) || FAILED(device->CreateRasterizerState(&rs,&rss)) || FAILED(device->CreateBlendState(&bs,&bss))) return false;
        device_=device;vs_=vs;layout_=layout;dss_=dss;rss_=rss;bss_=bss;
        return true;
    }
    // Present thread: one eye's NDC triangles (from visibility_stamp_triangles). Empty clears that eye.
    static void publish(unsigned eye,const std::vector<float>& ndc) noexcept {
        if(eye>1) return;
        std::shared_ptr<Geometry> g;
        try {
            if(!ndc.empty()) {
                Microsoft::WRL::ComPtr<ID3D11Device> device;{std::lock_guard<std::mutex> lock(mutex_);device=device_;}
                if(!device) return;
                D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(ndc.size()*sizeof(float));desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA data{ndc.data(),0,0};
                g=std::make_shared<Geometry>();
                if(FAILED(device->CreateBuffer(&desc,&data,&g->vb))) {++failures;return;}
                g->vertices=static_cast<UINT>(ndc.size()/2);
            }
        } catch(...) {++failures;return;}
        double area=0;
        for(std::size_t t=0;t+5<ndc.size();t+=6) area+=std::abs((ndc[t+2]-ndc[t])*(ndc[t+5]-ndc[t+1])-(ndc[t+4]-ndc[t])*(ndc[t+3]-ndc[t+1]))*.5;
        triangles[eye]=static_cast<std::uint32_t>(ndc.size()/6);stamped_share[eye]=static_cast<float>(area/4);
        std::lock_guard<std::mutex> lock(mutex_);geometry_[eye]=std::move(g);
    }
    // Job thread, before a draw is recorded. pass/eye from RenderPassMap + ObjectMotion. One stamp per pass job (next_job()).
    static void before_draw(ID3D11DeviceContext* c,int pass,int eye) noexcept {
        if(pass!=forward_world_pass || !enabled.load(std::memory_order_relaxed) || !c) return;
        if(stamped_job_==job_serial_) return;
        stamped_job_=job_serial_; // also guards the stamp's own Draw, which re-enters the draw hook
        if(eye<0 || eye>1) {++no_eye;return;}
        std::shared_ptr<Geometry> g;Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dss;Microsoft::WRL::ComPtr<ID3D11RasterizerState> rss;Microsoft::WRL::ComPtr<ID3D11BlendState> bss;
        {std::lock_guard<std::mutex> lock(mutex_);g=geometry_[static_cast<unsigned>(eye)];vs=vs_;layout=layout_;dss=dss_;rss=rss_;bss=bss_;}
        if(!g || !g->vb || !vs) {++no_geometry;return;}
        // the forward pass's own targets and viewport
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        c->OMGetRenderTargets(0,nullptr,&dsv);
        if(!dsv) {++no_depth;return;}
        UINT viewports=1;D3D11_VIEWPORT viewport{};c->RSGetViewports(&viewports,&viewport);
        if(!viewports || viewport.Width<64 || viewport.Height<64) {++no_viewport;return;}
        last_viewport_w[static_cast<unsigned>(eye)]=static_cast<std::uint32_t>(viewport.Width);last_viewport_h[static_cast<unsigned>(eye)]=static_cast<std::uint32_t>(viewport.Height);
        Saved s;s.save(c);
        const UINT stride=8,offset=0;ID3D11Buffer* vb=g->vb.Get();
        c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        c->VSSetShader(vs.Get(),nullptr,0);c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);c->GSSetShader(nullptr,nullptr,0);
        c->PSSetShader(nullptr,nullptr,0);
        c->RSSetState(rss.Get());c->OMSetDepthStencilState(dss.Get(),0);const float factor[4]{};c->OMSetBlendState(bss.Get(),factor,0xffffffff);
        c->Draw(g->vertices,0);
        s.restore(c);
        ++stamps[static_cast<unsigned>(eye)];
    }
    // RenderPassMap calls this as each pass job starts on a thread.
    static void next_job() noexcept {++job_serial_;}
    static std::string json() {
        std::ostringstream o;
        o<<"{\"enabled\":"<<(enabled.load()?"true":"false")<<",\"stamps\":["<<stamps[0].load()<<','<<stamps[1].load()<<"],\"triangles\":["<<triangles[0].load()<<','<<triangles[1].load()
         <<"],\"stamped_share\":["<<stamped_share[0].load()<<','<<stamped_share[1].load()<<"],\"viewport\":[["<<last_viewport_w[0].load()<<','<<last_viewport_h[0].load()<<"],["
         <<last_viewport_w[1].load()<<','<<last_viewport_h[1].load()<<"]],\"skipped\":{\"no_eye\":"<<no_eye.load()<<",\"no_depth\":"<<no_depth.load()<<",\"no_geometry\":"<<no_geometry.load()
         <<",\"no_viewport\":"<<no_viewport.load()<<"},\"failures\":"<<failures.load()<<'}';
        return o.str();
    }
    static void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        geometry_={};vs_.Reset();layout_.Reset();dss_.Reset();rss_.Reset();bss_.Reset();device_.Reset();
    }
private:
    struct Geometry {Microsoft::WRL::ComPtr<ID3D11Buffer> vb;UINT vertices{};};
    struct Saved {
        Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;std::array<ID3D11ClassInstance*,256> vs_inst{};UINT vs_count=256;
        Microsoft::WRL::ComPtr<ID3D11HullShader> hs;Microsoft::WRL::ComPtr<ID3D11DomainShader> ds;Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;std::array<ID3D11ClassInstance*,256> ps_inst{};UINT ps_count=256;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rs;Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dss;UINT stencil_ref{};
        Microsoft::WRL::ComPtr<ID3D11BlendState> blend;float factor[4]{};UINT sample_mask{};
        void save(ID3D11DeviceContext* c) {
            c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
            c->VSGetShader(&vs,vs_inst.data(),&vs_count);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);
            c->PSGetShader(&ps,ps_inst.data(),&ps_count);
            c->RSGetState(&rs);c->OMGetDepthStencilState(&dss,&stencil_ref);c->OMGetBlendState(&blend,factor,&sample_mask);
        }
        void restore(ID3D11DeviceContext* c) {
            ID3D11Buffer* b=vb.Get();
            c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);c->IASetVertexBuffers(0,1,&b,&stride,&offset);
            c->VSSetShader(vs.Get(),vs_count?vs_inst.data():nullptr,vs_count);c->HSSetShader(hs.Get(),nullptr,0);c->DSSetShader(ds.Get(),nullptr,0);c->GSSetShader(gs.Get(),nullptr,0);
            c->PSSetShader(ps.Get(),ps_count?ps_inst.data():nullptr,ps_count);
            c->RSSetState(rs.Get());c->OMSetDepthStencilState(dss.Get(),stencil_ref);c->OMSetBlendState(blend.Get(),factor,sample_mask);
            for(UINT i=0;i<vs_count;++i) if(vs_inst[i]) vs_inst[i]->Release();
            for(UINT i=0;i<ps_count;++i) if(ps_inst[i]) ps_inst[i]->Release();
        }
    };
    inline static std::mutex mutex_;
    inline static Microsoft::WRL::ComPtr<ID3D11Device> device_;
    inline static Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;inline static Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    inline static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dss_;inline static Microsoft::WRL::ComPtr<ID3D11RasterizerState> rss_;
    inline static Microsoft::WRL::ComPtr<ID3D11BlendState> bss_;
    inline static std::array<std::shared_ptr<Geometry>,2> geometry_{};
    inline static thread_local std::uint64_t job_serial_=0,stamped_job_=~std::uint64_t{0};
};

} // namespace d2vr
