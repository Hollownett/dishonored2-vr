#pragma once
// The game's own spyglass lens mask for the right-eye scope (v1041; user: "use the actual in-game lens asset, not your own").
// The zoom's post effect ply_spyglass_pp_01 puts a lens-mask quad in renderView fullScreenQuadsByStage (stage 4). Its entry holds
// only the material and a white tint, no rectangle, so the engine can only draw it over the whole view. We find the draw instead:
// during the first zoom the RIGHT view keeps the quad for two blocks and loses it for one in between (the left view never has it),
// and the small draws of every context are counted per block by (pixel shader, the four bound textures). The draw that exists only
// while the quad does is the lens mask; its static texture, view format and blend state are kept (references to the game's own
// objects, no copy) and SpyglassScopeMask samples that texture over the scope square. No draw found = no art = the procedural rim.
// Pure parts (block schedule, choosing the draw, texture mapping) are checked by tests/spyglass_scope_check.cpp.
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
namespace spyglass_art {
constexpr unsigned block_frames=10,skip_frames=3,blocks=3,start_after=20,total_frames=start_after+block_frames*blocks,max_attempts=3;
constexpr unsigned max_draw_elements=6,slots=4,max_seen=128;
// Frame f (1 = first frame of the zoom): the probe records during 3 blocks after `start_after` frames; blocks 0 and 2 keep the right
// view's quad, block 1 removes it. The first frames of a block are skipped (the engine draws a frame or two after its Setup).
inline int block_of(unsigned f) noexcept {return f>start_after && f<=total_frames?static_cast<int>((f-start_after-1)/block_frames):-1;}
inline bool keeps_quad(unsigned f) noexcept {const int b=block_of(f);return b==0 || b==2;}
inline int record_block(unsigned f) noexcept {const int b=block_of(f);return b>=0 && (f-start_after-1)%block_frames>=skip_frames?b:-1;}
// The lens draw: seen with the quad (blocks 0 and 2) and not without (block 1). Best = most draws with it, five times the draws without.
inline std::optional<std::size_t> choose(const std::vector<std::array<unsigned,3>>& counts) noexcept {
    std::optional<std::size_t> best;long best_score=0;
    for(std::size_t i=0;i<counts.size();++i) {
        const auto& c=counts[i];if(c[0]<4 || c[2]<4 || c[1]>1) continue;
        const long score=static_cast<long>(c[0])+c[2]-5L*c[1];
        if(score>best_score) {best=i;best_score=score;}
    }
    return best;
}
// The scope square in texture space. `s` = the point in scope radii from the centre (x right, y up; the circle is |s|=1). The texture's
// height spans the scope's diameter, its width keeps the texture's aspect; outside the texture is outside the art (black).
struct Uv {double u{},v{};bool inside{};};
inline Uv texture_uv(double sx,double sy,double aspect) noexcept {
    const double a=aspect>.05?aspect:1.;const double u=.5+sx/(2*a),v=.5-sy/2;
    return {u,v,u>=0 && u<=1 && v>=0 && v<=1};
}
// A one-channel format samples as (value, 0, 0, 1): the game's lens is BC4 (sim v1042: 1920x1080, 0 inside the opening, 255 masked,
// drawn with SRC_ALPHA/INV_SRC_ALPHA), so its value is the coverage of black, not a red colour.
inline bool one_channel(DXGI_FORMAT f) noexcept {
    switch(f) {
        case DXGI_FORMAT_BC4_TYPELESS:case DXGI_FORMAT_BC4_UNORM:case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_R8_TYPELESS:case DXGI_FORMAT_R8_UNORM:case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R32_FLOAT:return true;
        default:return false;
    }
}
inline bool is_static_texture(const D3D11_TEXTURE2D_DESC& d) noexcept {
    return d.SampleDesc.Count==1 && d.ArraySize==1 && d.Width>=16 && d.Height>=16 && (d.Usage==D3D11_USAGE_DEFAULT || d.Usage==D3D11_USAGE_IMMUTABLE) &&
        !(d.BindFlags&(D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_DEPTH_STENCIL)) && (d.BindFlags&D3D11_BIND_SHADER_RESOURCE) && !(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE);
}
}

// What the winning draw bound: the mask texture of the game, the view it was read through, and the draw's blend state.
struct LensArt {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    bool blend_enabled{};D3D11_BLEND src{D3D11_BLEND_ONE},dst{D3D11_BLEND_ZERO};D3D11_BLEND_OP op{D3D11_BLEND_OP_ADD};
    unsigned slot{};std::filesystem::path dump_stem;std::atomic<bool> dump_pending{};   // dump_stem: set by the owner of the artifacts folder
    // The draw's own blend; a draw without blending (the lens multiplying the scene itself) is a multiply of what is there.
    D3D11_BLEND eff_src() const noexcept {return blend_enabled?src:D3D11_BLEND_ZERO;}
    D3D11_BLEND eff_dst() const noexcept {return blend_enabled?dst:D3D11_BLEND_SRC_COLOR;}
    D3D11_BLEND_OP eff_op() const noexcept {return blend_enabled?op:D3D11_BLEND_OP_ADD;}
};

class SpyglassArtProbe {
    struct Slot {Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};D3D11_SHADER_RESOURCE_VIEW_DESC view{};bool have{};};
    struct Seen {
        void* ps{};std::array<void*,spyglass_art::slots> res{};std::array<unsigned,3> n{};UINT elements{};bool indexed{};
        std::array<Slot,spyglass_art::slots> slot{};bool blend_enabled{};D3D11_RENDER_TARGET_BLEND_DESC blend{};
    };
    std::atomic<int> phase_{-1};   // the block being recorded, -1 = not recording (read by every draw hook)
    std::mutex mutex_;std::vector<Seen> seen_;
    unsigned frame_{},attempts_{};bool done_{};std::shared_ptr<LensArt> art_;std::mutex art_mutex_;
    void clear_() {std::lock_guard<std::mutex> lock(mutex_);seen_.clear();phase_.store(-1,std::memory_order_relaxed);}
    void observe_slow_(ID3D11DeviceContext* context,UINT elements,bool indexed,int block) {
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;context->PSGetShader(&ps,nullptr,nullptr);
        if(!ps) return;
        std::array<ID3D11ShaderResourceView*,spyglass_art::slots> views{};context->PSGetShaderResources(0,spyglass_art::slots,views.data());
        std::array<Microsoft::WRL::ComPtr<ID3D11Resource>,spyglass_art::slots> res{};
        for(unsigned i=0;i<spyglass_art::slots;++i) if(views[i]) {views[i]->GetResource(&res[i]);}
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for(auto& s:seen_) if(s.ps==ps.Get() && s.res[0]==res[0].Get() && s.res[1]==res[1].Get() && s.res[2]==res[2].Get() && s.res[3]==res[3].Get()) {
                ++s.n[static_cast<unsigned>(block)];for(auto* v:views) if(v) v->Release();return;
            }
            if(seen_.size()>=spyglass_art::max_seen) {for(auto* v:views) if(v) v->Release();return;}
        }
        Seen s;s.ps=ps.Get();s.elements=elements;s.indexed=indexed;s.n[static_cast<unsigned>(block)]=1;
        for(unsigned i=0;i<spyglass_art::slots;++i) {
            s.res[i]=res[i].Get();
            if(views[i]) {
                D3D11_SHADER_RESOURCE_VIEW_DESC vd{};views[i]->GetDesc(&vd);
                Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
                if(res[i] && vd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D && SUCCEEDED(res[i].As(&t))) {s.slot[i].texture=t;t->GetDesc(&s.slot[i].desc);s.slot[i].view=vd;s.slot[i].have=true;}
                views[i]->Release();
            }
        }
        Microsoft::WRL::ComPtr<ID3D11BlendState> bs;FLOAT factor[4]{};UINT mask{};context->OMGetBlendState(&bs,factor,&mask);
        if(bs) {D3D11_BLEND_DESC bd{};bs->GetDesc(&bd);s.blend=bd.RenderTarget[0];s.blend_enabled=bd.RenderTarget[0].BlendEnable!=FALSE;}
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& o:seen_) if(o.ps==s.ps && o.res==s.res) {++o.n[static_cast<unsigned>(block)];return;}
        if(seen_.size()<spyglass_art::max_seen) seen_.push_back(std::move(s));
    }
    static const char* yes(bool v) {return v?"true":"false";}
    // Evaluate after the last block: publishes the art when a draw was found and has a usable static texture; returns the JSON report.
    std::string finish_() {
        std::vector<std::array<unsigned,3>> counts;std::vector<Seen> seen;
        {std::lock_guard<std::mutex> lock(mutex_);seen=std::move(seen_);seen_.clear();phase_.store(-1,std::memory_order_relaxed);}
        for(const auto& s:seen) counts.push_back(s.n);
        const auto pick=spyglass_art::choose(counts);
        std::ostringstream o;o<<"{\"attempt\":"<<attempts_<<",\"seen\":"<<seen.size()<<",\"chosen\":"<<(pick?static_cast<long>(*pick):-1L)<<",\"draws\":[";
        // the 12 draws that appear in blocks 0/2 most (the chosen one is among them), for the artifacts reader
        std::vector<std::size_t> order(seen.size());for(std::size_t i=0;i<order.size();++i) order[i]=i;
        std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {return seen[a].n[0]+seen[a].n[2]+5*(seen[a].n[0]+seen[a].n[2]>0 && seen[a].n[1]==0)>seen[b].n[0]+seen[b].n[2]+5*(seen[b].n[0]+seen[b].n[2]>0 && seen[b].n[1]==0);});
        for(std::size_t k=0;k<order.size() && k<12;++k) {
            const auto& s=seen[order[k]];
            o<<(k?",":"")<<"{\"index\":"<<order[k]<<",\"ps\":\""<<s.ps<<"\",\"counts\":["<<s.n[0]<<','<<s.n[1]<<','<<s.n[2]<<"],\"elements\":"<<s.elements<<",\"indexed\":"<<yes(s.indexed)
              <<",\"blend\":{\"enabled\":"<<yes(s.blend_enabled)<<",\"src\":"<<s.blend.SrcBlend<<",\"dst\":"<<s.blend.DestBlend<<",\"op\":"<<s.blend.BlendOp<<"},\"srv\":[";
            for(unsigned i=0;i<spyglass_art::slots;++i) {
                o<<(i?",":"");
                if(!s.slot[i].have) {o<<(s.res[i]?"\"other\"":"null");continue;}
                const auto& d=s.slot[i].desc;
                o<<"{\"w\":"<<d.Width<<",\"h\":"<<d.Height<<",\"mips\":"<<d.MipLevels<<",\"format\":"<<d.Format<<",\"view_format\":"<<s.slot[i].view.Format<<",\"usage\":"<<d.Usage
                 <<",\"bind\":"<<d.BindFlags<<",\"static\":"<<yes(spyglass_art::is_static_texture(d))<<'}';
            }
            o<<"]}";
        }
        o<<"],\"art\":";
        std::shared_ptr<LensArt> art;
        if(pick) {
            const auto& s=seen[*pick];
            for(unsigned i=0;i<spyglass_art::slots && !art;++i) if(s.slot[i].have && spyglass_art::is_static_texture(s.slot[i].desc)) {
                art=std::make_shared<LensArt>();art->texture=s.slot[i].texture;art->desc=s.slot[i].desc;art->view=s.slot[i].view;art->slot=i;
                art->blend_enabled=s.blend_enabled;art->src=s.blend.SrcBlend;art->dst=s.blend.DestBlend;art->op=s.blend.BlendOp;
            }
        }
        if(art) {o<<"{\"slot\":"<<art->slot<<",\"w\":"<<art->desc.Width<<",\"h\":"<<art->desc.Height<<",\"format\":"<<art->desc.Format<<'}';}
        else o<<"null";
        o<<'}';
        if(art) {std::lock_guard<std::mutex> lock(art_mutex_);art_=art;done_=true;}
        return o.str();
    }
public:
    // Every draw hook: one relaxed load while not recording.
    void observe(ID3D11DeviceContext* context,UINT elements,bool indexed) noexcept {
        const int block=phase_.load(std::memory_order_relaxed);
        if(block<0 || elements<3 || elements>spyglass_art::max_draw_elements) return;
        try {observe_slow_(context,elements,indexed,block);} catch(...) {}
    }
    // Once per displayed frame (the Setup block). `active` = the scope is on. Returns a report when an attempt ended.
    std::optional<std::string> step(bool active) {
        if(done_ || attempts_>=spyglass_art::max_attempts) return std::nullopt;
        if(!active) {
            if(frame_) {frame_=0;++attempts_;clear_();}   // the zoom ended before the blocks did: try again at the next one
            return std::nullopt;
        }
        ++frame_;
        phase_.store(spyglass_art::record_block(frame_),std::memory_order_relaxed);
        if(frame_==spyglass_art::total_frames+1) {++attempts_;frame_=0;return finish_();}
        return std::nullopt;
    }
    // The right view keeps the engine's lens quad during the keep blocks of the probe.
    bool keep_quad() const noexcept {return !done_ && attempts_<spyglass_art::max_attempts && frame_ && spyglass_art::keeps_quad(frame_);}
    std::shared_ptr<LensArt> art() {std::lock_guard<std::mutex> lock(art_mutex_);return art_;}
    bool found() const noexcept {return done_;}
    unsigned attempts() const noexcept {return attempts_;}
};
// Never destroyed: the game's D3D objects it references outlive the DLL's statics.
inline SpyglassArtProbe& spyglass_art_probe() {static auto* probe=new SpyglassArtProbe;return *probe;}
}
