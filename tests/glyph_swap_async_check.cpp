#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/glyph_swap.hpp"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
// CPU COM fixtures: no D3D device, GPU, hook installation or game process.
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
namespace {
struct Mock {
    void** table{};std::array<void*,113> methods{};unsigned refs=1;
    Mock* device{};Mock* context{};Mock* resource{};
    D3D11_TEXTURE2D_DESC desc{};D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    std::vector<std::uint8_t> blocks,mapped;
    D3D11_DEVICE_CONTEXT_TYPE type=D3D11_DEVICE_CONTEXT_IMMEDIATE;
    explicit Mock(unsigned kind);
};
std::vector<std::unique_ptr<Mock>> objects;
unsigned copies{},maps{},unmaps{},busy{},creates{};HRESULT map_error=S_OK;bool reenter{};
bool talk_scope{};
Mock* reenter_view{};
bool talk_context() noexcept {return talk_scope;}
Mock* make(unsigned kind) {objects.push_back(std::make_unique<Mock>(kind));return objects.back().get();}
ULONG STDMETHODCALLTYPE add(Mock* p) {return ++p->refs;}
ULONG STDMETHODCALLTYPE release(Mock* p) {assert(p->refs);const auto refs=--p->refs;if(!refs && p->resource)release(p->resource);return refs;}
HRESULT STDMETHODCALLTYPE query(Mock* p,REFIID,void** out) {*out=p;add(p);return S_OK;}
void STDMETHODCALLTYPE get_device(Mock* p,ID3D11Device** out) {*out=reinterpret_cast<ID3D11Device*>(p->device);add(p->device);}
void STDMETHODCALLTYPE immediate(Mock* p,ID3D11DeviceContext** out) {*out=reinterpret_cast<ID3D11DeviceContext*>(p->context);add(p->context);}
void STDMETHODCALLTYPE get_resource(Mock* p,ID3D11Resource** out) {*out=reinterpret_cast<ID3D11Resource*>(p->resource);add(p->resource);}
void STDMETHODCALLTYPE texture_desc(Mock* p,D3D11_TEXTURE2D_DESC* out) {*out=p->desc;}
void STDMETHODCALLTYPE view_desc(Mock* p,D3D11_SHADER_RESOURCE_VIEW_DESC* out) {*out=p->srv;}
D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE context_type(Mock* p) {return p->type;}
HRESULT STDMETHODCALLTYPE create_texture(Mock* p,const D3D11_TEXTURE2D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture2D** out);
HRESULT STDMETHODCALLTYPE create_view(Mock* p,ID3D11Resource* resource,const D3D11_SHADER_RESOURCE_VIEW_DESC* d,ID3D11ShaderResourceView** out) {
    auto* view=make(3);view->device=p;view->resource=reinterpret_cast<Mock*>(resource);add(view->resource);view->srv=*d;
    *out=reinterpret_cast<ID3D11ShaderResourceView*>(view);return S_OK;
}
void STDMETHODCALLTYPE copy(Mock*,ID3D11Resource* dst,ID3D11Resource* src) {
    ++copies;auto* d=reinterpret_cast<Mock*>(dst);auto* s=reinterpret_cast<Mock*>(src);d->blocks=s->blocks;
}
HRESULT STDMETHODCALLTYPE map(Mock*,ID3D11Resource* resource,UINT sub,D3D11_MAP mode,UINT flags,D3D11_MAPPED_SUBRESOURCE* out) {
    ++maps;assert(sub==0 && mode==D3D11_MAP_READ && flags==D3D11_MAP_FLAG_DO_NOT_WAIT);
    if(busy) {--busy;return DXGI_ERROR_WAS_STILL_DRAWING;}if(FAILED(map_error))return map_error;
    auto* t=reinterpret_cast<Mock*>(resource);const UINT row=((t->desc.Width+3)/4)*16,bh=(t->desc.Height+3)/4;
    const UINT pitch=row+16;t->mapped.assign(static_cast<std::size_t>(pitch)*bh,0x5a);
    for(UINT y=0;y<bh;++y)std::memcpy(t->mapped.data()+static_cast<std::size_t>(y)*pitch,t->blocks.data()+static_cast<std::size_t>(y)*row,row);
    *out={t->mapped.data(),pitch,0};return S_OK;
}
void STDMETHODCALLTYPE unmap(Mock*,ID3D11Resource*,UINT) {++unmaps;}
Mock::Mock(unsigned kind) {
    table=methods.data();methods[0]=reinterpret_cast<void*>(&query);methods[1]=reinterpret_cast<void*>(&add);methods[2]=reinterpret_cast<void*>(&release);
    if(kind==0) {methods[5]=reinterpret_cast<void*>(&create_texture);methods[7]=reinterpret_cast<void*>(&create_view);methods[40]=reinterpret_cast<void*>(&immediate);}
    else {methods[3]=reinterpret_cast<void*>(&get_device);
        if(kind==1) {methods[14]=reinterpret_cast<void*>(&map);methods[15]=reinterpret_cast<void*>(&unmap);methods[47]=reinterpret_cast<void*>(&copy);methods[112]=reinterpret_cast<void*>(&context_type);}
        if(kind==2)methods[10]=reinterpret_cast<void*>(&texture_desc);
        if(kind==3) {methods[7]=reinterpret_cast<void*>(&get_resource);methods[8]=reinterpret_cast<void*>(&view_desc);}
    }
}
}
namespace d2vr {
struct GlyphSwapAsyncCheck {
    static void begin(Mock* device) {std::lock_guard<std::mutex> lock(GlyphSwap::process_mutex_);assert(GlyphSwap::adopt_owner(reinterpret_cast<ID3D11Device*>(device)));GlyphSwap::enabled=true;}
    static auto lookup(Mock* view) {return GlyphSwap::lookup(reinterpret_cast<ID3D11ShaderResourceView*>(view));}
    static void process(Mock* context) {GlyphSwap::process(reinterpret_cast<ID3D11DeviceContext*>(context));}
    static void fill_negatives() {
        std::unique_lock<std::shared_mutex> lock(GlyphSwap::map_mutex_);
        for(std::uintptr_t i=0;i<16385;++i) GlyphSwap::map_.emplace(reinterpret_cast<ID3D11ShaderResourceView*>(0x10000+i*16),GlyphSwap::Entry{});
    }
    static std::size_t cache_size() {return GlyphSwap::map_.size();}
    static bool pending(Mock* view) {const auto it=GlyphSwap::map_.find(reinterpret_cast<ID3D11ShaderResourceView*>(view));return it!=GlyphSwap::map_.end() && it->second.pending;}
};
}
namespace {
HRESULT STDMETHODCALLTYPE create_texture(Mock* p,const D3D11_TEXTURE2D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture2D** out) {
    ++creates;if(reenter) {reenter=false;assert(!d2vr::GlyphSwapAsyncCheck::lookup(reenter_view));}
    auto* t=make(2);t->device=p;t->desc=*d;
    if(init) {const auto bytes=static_cast<std::size_t>(init->SysMemPitch)*((d->Height+3)/4);t->blocks.assign(static_cast<const std::uint8_t*>(init->pSysMem),static_cast<const std::uint8_t*>(init->pSysMem)+bytes);}
    *out=reinterpret_cast<ID3D11Texture2D*>(t);return S_OK;
}
Mock* view(Mock* device,const d2vr::GlyphTexture& glyph,const std::vector<std::uint8_t>& blocks) {
    auto* t=make(2);t->device=device;t->desc.Width=glyph.width;t->desc.Height=glyph.height;t->desc.MipLevels=t->desc.ArraySize=t->desc.SampleDesc.Count=1;
    t->desc.Format=DXGI_FORMAT_BC7_UNORM;t->desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;t->blocks=blocks;
    auto* v=make(3);v->device=device;v->resource=t;add(t);v->srv.Format=t->desc.Format;v->srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;v->srv.Texture2D.MipLevels=1;return v;
}
}
int main() {
    using namespace d2vr;
    GlyphSwap::talk_prompt_context=talk_context;
    const auto& glyph=kXboxGlyphs[0];std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path()/
        "artifacts/research/glyphs/extracted/ui_lib_controller_buttons.raw.iggytex",std::ios::binary);
    assert(file);file.seekg(glyph.offset);std::vector<std::uint8_t> native(glyph.bytes);file.read(reinterpret_cast<char*>(native.data()),native.size());assert(file && find_xbox_glyph(glyph.width,glyph.height,native.data(),native.size())==&glyph);
    auto* device=make(0);auto* context=make(1);context->device=device;device->context=context;
    GlyphSwapAsyncCheck::begin(device);auto* known=view(device,glyph,native);
    assert(!GlyphSwapAsyncCheck::lookup(known) && known->refs==2 && GlyphSwap::pending_readbacks==1);
    reenter=true;reenter_view=known;GlyphSwapAsyncCheck::process(context);assert(copies==1 && maps==0);
    auto* staging=objects.back().get();assert(staging->desc.Usage==D3D11_USAGE_STAGING && staging->refs==1);
    busy=2;for(unsigned i=0;i<2;++i) {GlyphSwapAsyncCheck::process(context);assert(copies==1 && !GlyphSwapAsyncCheck::lookup(known) && GlyphSwapAsyncCheck::pending(known));}
    assert(GlyphSwap::deferred==2 && GlyphSwap::checked==0 && GlyphSwap::pending_readbacks==1);
    GlyphSwapAsyncCheck::process(context);auto replacement=GlyphSwapAsyncCheck::lookup(known);
    assert(replacement && copies==1 && unmaps==1 && staging->refs==0 && known->refs==2);
    assert(GlyphSwap::checked==1 && GlyphSwap::identified==1 && GlyphSwap::completed==1 && GlyphSwap::pending_readbacks==0);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(glyph.width)*glyph.height*4);paint_quest_glyphs(rgba.data(),glyph.width,glyph.height,glyph.label);
    auto* painted=reinterpret_cast<Mock*>(replacement.Get())->resource;assert(painted->blocks==encode_bc7_mode6(rgba.data(),glyph.width,glyph.height));
    const auto& x_glyph=kXboxGlyphs[1];file.clear();file.seekg(x_glyph.offset);std::vector<std::uint8_t> x_native(x_glyph.bytes);
    file.read(reinterpret_cast<char*>(x_native.data()),x_native.size());assert(file && find_xbox_glyph(x_glyph.width,x_glyph.height,x_native.data(),x_native.size())==&x_glyph);
    auto* x_view=view(device,x_glyph,x_native);assert(!GlyphSwapAsyncCheck::lookup(x_view));GlyphSwapAsyncCheck::process(context);GlyphSwapAsyncCheck::process(context);
    const auto grip=GlyphSwapAsyncCheck::lookup(x_view);assert(grip);
    rgba.assign(static_cast<std::size_t>(x_glyph.width)*x_glyph.height*4,0);paint_quest_glyphs(rgba.data(),x_glyph.width,x_glyph.height,"Grip");
    assert(reinterpret_cast<Mock*>(grip.Get())->resource->blocks==encode_bc7_mode6(rgba.data(),x_glyph.width,x_glyph.height));
    talk_scope=true;const auto talk=GlyphSwapAsyncCheck::lookup(x_view);assert(talk && talk.Get()!=grip.Get());
    rgba.assign(static_cast<std::size_t>(x_glyph.width)*x_glyph.height*4,0);paint_quest_glyphs(rgba.data(),x_glyph.width,x_glyph.height,"Trig");
    assert(reinterpret_cast<Mock*>(talk.Get())->resource->blocks==encode_bc7_mode6(rgba.data(),x_glyph.width,x_glyph.height));
    // v970: the short NPC latch keeps the X glyph at Trig even when the per-child context callback is false (the button
    // icon is often a different child than the prompt, and the game redraws it).
    GlyphSwap::talk_prompt_context=nullptr;GlyphSwap::npc_prompt_until=GetTickCount64()+600;
    assert(GlyphSwapAsyncCheck::lookup(x_view).Get()==talk.Get());
    GlyphSwap::npc_prompt_until=0;GlyphSwap::talk_prompt_context=talk_context;
    GlyphSwap::wheel_open=true;const auto wheel=GlyphSwapAsyncCheck::lookup(x_view);assert(wheel && wheel.Get()!=talk.Get());
    rgba.assign(static_cast<std::size_t>(x_glyph.width)*x_glyph.height*4,0);paint_quest_glyphs(rgba.data(),x_glyph.width,x_glyph.height,"A");
    assert(reinterpret_cast<Mock*>(wheel.Get())->resource->blocks==encode_bc7_mode6(rgba.data(),x_glyph.width,x_glyph.height));
    GlyphSwap::native_labels=true;assert(!GlyphSwapAsyncCheck::lookup(x_view));GlyphSwap::native_labels=false;
    GlyphSwap::wheel_open=false;talk_scope=false;assert(GlyphSwapAsyncCheck::lookup(x_view).Get()==grip.Get());
    GlyphSwap::talk_prompt_context=nullptr;
    auto* unknown=view(device,glyph,std::vector<std::uint8_t>(glyph.bytes));assert(!GlyphSwapAsyncCheck::lookup(unknown));GlyphSwapAsyncCheck::process(context);GlyphSwapAsyncCheck::process(context);
    const auto prior_copies=copies;assert(!GlyphSwapAsyncCheck::lookup(unknown));GlyphSwapAsyncCheck::process(context);assert(copies==prior_copies && unknown->refs==1);
    auto* queued=view(device,glyph,native);GlyphSwapAsyncCheck::lookup(queued);
    GlyphSwapAsyncCheck::fill_negatives();assert(GlyphSwapAsyncCheck::cache_size()>16384);
    GlyphSwapAsyncCheck::process(context);
    assert(GlyphSwapAsyncCheck::cache_size()==3 && GlyphSwapAsyncCheck::pending(queued));
    assert(GlyphSwapAsyncCheck::lookup(known).Get()==replacement.Get()); // Positive and pending entries survive pruning.
    GlyphSwapAsyncCheck::process(context);assert(!GlyphSwapAsyncCheck::pending(queued));
    auto* error=view(device,glyph,native);GlyphSwapAsyncCheck::lookup(error);GlyphSwapAsyncCheck::process(context);map_error=E_FAIL;GlyphSwapAsyncCheck::process(context);assert(error->refs==1 && GlyphSwap::pending_readbacks==0);map_error=S_OK;
    assert(!GlyphSwapAsyncCheck::lookup(error) && error->refs==2); // Error is retryable.
    auto* deferred_context=make(1);deferred_context->device=device;deferred_context->type=D3D11_DEVICE_CONTEXT_DEFERRED;
    const auto before=maps;GlyphSwapAsyncCheck::process(deferred_context);assert(maps==before && GlyphSwap::pending_readbacks==1);
    GlyphSwapAsyncCheck::process(context); // Queue a pending copy before teardown.
    GlyphSwap::disable();assert(known->refs==1 && error->refs==1 && GlyphSwap::pending_readbacks==0);
    assert(device->refs==1 && context->refs==1);replacement.Reset();
    GlyphSwapAsyncCheck::begin(device);GlyphSwapAsyncCheck::lookup(known);GlyphSwapAsyncCheck::process(context);
    auto* other_device=make(0);auto* other_context=make(1);other_context->device=other_device;other_device->context=other_context;
    GlyphSwapAsyncCheck::process(other_context);assert(!GlyphSwap::enabled && known->refs==1 && GlyphSwap::pending_readbacks==0);
    assert(device->refs==1 && context->refs==1);GlyphSwapAsyncCheck::begin(other_device);
    auto* foreign=view(device,glyph,native);GlyphSwapAsyncCheck::lookup(foreign);GlyphSwapAsyncCheck::process(other_context);
    assert(foreign->refs==1 && GlyphSwap::pending_readbacks==0);GlyphSwap::disable();
    GlyphSwapAsyncCheck::begin(device);std::array<Mock*,17> bounded{};
    for(unsigned i=0;i<8;++i) {bounded[i]=view(device,glyph,native);GlyphSwapAsyncCheck::lookup(bounded[i]);}
    GlyphSwapAsyncCheck::process(context);assert(GlyphSwap::pending_readbacks==8);
    busy=8;GlyphSwapAsyncCheck::process(context); // Eight existing copies remain busy.
    for(unsigned i=8;i<17;++i) {bounded[i]=view(device,glyph,native);GlyphSwapAsyncCheck::lookup(bounded[i]);}
    assert(GlyphSwap::pending_readbacks==16 && bounded[16]->refs==1); // Full queue remains retryable, not negative cached.
    GlyphSwap::disable();for(auto* v:bounded)assert(v->refs==1);
    assert(GlyphSwap::pending_readbacks==0 && device->refs==1 && context->refs==1);
    std::cout<<"PASS: CPU COM fixtures: busy nonblocking readback, one copy, native BC7 exact label replacement, negative cache, retryable error, callback-safe locks, owner/deferred-context refusal and COM cleanup\n";
}
