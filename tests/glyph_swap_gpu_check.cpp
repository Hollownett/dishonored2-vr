#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/glyph_swap.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
// Standalone software D3D11 test. No hooks, live game or headset.
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
namespace {
void check(bool ok,const char* what) {if(!ok)throw std::runtime_error(what);}
template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
ULONG refs(IUnknown* value) {const auto n=value->AddRef();value->Release();return n;}
}
namespace d2vr {
struct GlyphSwapAsyncCheck {
    static void begin(ID3D11Device* device) {
        std::lock_guard<std::mutex> lock(GlyphSwap::process_mutex_);
        check(GlyphSwap::adopt_owner(device),"canonical immediate owner");GlyphSwap::enabled=true;
    }
    static auto lookup(ID3D11ShaderResourceView* view) {return GlyphSwap::lookup(view);}
    static ID3D11Texture2D* staging() {for(const auto& job:GlyphSwap::jobs_)if(job.view)return job.staging.Get();return nullptr;}
    static bool empty() {
        for(const auto& job:GlyphSwap::jobs_)if(job.view || job.staging)return false;
        return GlyphSwap::pending_count_==0 && GlyphSwap::map_.empty() && !GlyphSwap::device_identity_ && !GlyphSwap::context_identity_;
    }
};
}
int main() {
    try {
        using namespace d2vr;
        const auto root=std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path();
        const auto& glyph=kXboxGlyphs[0];std::ifstream input(root/"artifacts/research/glyphs/extracted/ui_lib_controller_buttons.raw.iggytex",std::ios::binary);
        check(bool(input),"native extracted BC7 fixture");input.seekg(glyph.offset);
        std::vector<std::uint8_t> native(glyph.bytes);input.read(reinterpret_cast<char*>(native.data()),static_cast<std::streamsize>(native.size()));
        check(bool(input) && find_xbox_glyph(glyph.width,glyph.height,native.data(),native.size())==&glyph,"exact native BC7 signature");
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"software WARP device");
        check(level>=D3D_FEATURE_LEVEL_11_0,"BC7 feature level");
        const UINT row=((glyph.width+3)/4)*16,bh=(glyph.height+3)/4;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=glyph.width;desc.Height=glyph.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_BC7_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{native.data(),row,0};Ptr<ID3D11Texture2D> source;Ptr<ID3D11ShaderResourceView> view;
        check(SUCCEEDED(device->CreateTexture2D(&desc,&init,&source)) && SUCCEEDED(device->CreateShaderResourceView(source.Get(),nullptr,&view)),"native glyph texture/view");
        const auto baseline=refs(view.Get());GlyphSwapAsyncCheck::begin(device.Get());
        check(!GlyphSwapAsyncCheck::lookup(view.Get()) && GlyphSwap::pending_readbacks==1,"queue one owned source");
        GlyphSwap::process(context.Get());auto* staging=GlyphSwapAsyncCheck::staging();check(staging && GlyphSwap::checked==0,"copy first; no synchronous read");
        // Standalone submission substitutes for the game's next Present. No
        // Flush or wait is performed by GlyphSwap::process itself.
        context->Flush();Ptr<ID3D11ShaderResourceView> replacement;unsigned frames{};
        for(;frames<200;++frames) {
            GlyphSwap::process(context.Get());replacement=GlyphSwapAsyncCheck::lookup(view.Get());
            if(replacement)break;
            check(GlyphSwapAsyncCheck::staging()==staging,"busy read retained original staging copy");Sleep(1);
        }
        check(replacement && frames<200 && GlyphSwap::checked==1 && GlyphSwap::completed==1 && GlyphSwap::failed==0 && GlyphSwap::pending_readbacks==0,"later-frame glyph completion");
        Ptr<ID3D11Resource> painted_resource;replacement->GetResource(&painted_resource);
        Ptr<ID3D11Texture2D> painted;check(SUCCEEDED(painted_resource.As(&painted)),"replacement texture");
        auto read_desc=desc;read_desc.Usage=D3D11_USAGE_STAGING;read_desc.BindFlags=0;read_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Texture2D> readback;check(SUCCEEDED(device->CreateTexture2D(&read_desc,nullptr,&readback)),"verification staging");
        context->CopyResource(readback.Get(),painted.Get());context->Flush();D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT mapped_result=DXGI_ERROR_WAS_STILL_DRAWING;
        for(unsigned frame=0;frame<200 && mapped_result==DXGI_ERROR_WAS_STILL_DRAWING;++frame) {
            mapped_result=context->Map(readback.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(mapped_result==DXGI_ERROR_WAS_STILL_DRAWING)Sleep(1);
        }
        check(SUCCEEDED(mapped_result) && mapped.pData && mapped.RowPitch>=row,"nonblocking verification read");
        std::vector<std::uint8_t> actual(static_cast<std::size_t>(row)*bh);
        for(UINT y=0;y<bh;++y)std::memcpy(actual.data()+static_cast<std::size_t>(y)*row,static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,row);
        context->Unmap(readback.Get(),0);std::vector<std::uint8_t> rgba(static_cast<std::size_t>(glyph.width)*glyph.height*4);
        paint_quest_glyphs(rgba.data(),glyph.width,glyph.height,glyph.label);
        check(actual==encode_bc7_mode6(rgba.data(),glyph.width,glyph.height),"exact current Quest-label BC7 replacement");
        check(refs(view.Get())>baseline,"positive source retained against address reuse");
        GlyphSwap::disable();check(GlyphSwapAsyncCheck::empty() && GlyphSwap::pending_readbacks==0 && refs(view.Get())==baseline,"disable releases source/cache/staging/owner refs");
        check(replacement.Get()!=nullptr,"external replacement COM owner survives disable");
        std::cout<<"PASS: standalone WARP canonical ownership, copy-once staging, later-frame DO_NOT_WAIT readback, exact native glyph replacement and COM teardown; completion_frames="<<frames+1<<"\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';d2vr::GlyphSwap::disable();return 1;}
}
