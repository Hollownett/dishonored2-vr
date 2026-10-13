#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/render_size.hpp"
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>
#include <cstring>

using Microsoft::WRL::ComPtr;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct HiddenWindow {
    HWND handle{};HDC context{};
    HiddenWindow() {
        handle=CreateWindowExW(0,L"STATIC",L"D2VR resize check",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(handle!=nullptr,"Cannot create hidden test window");
        context=GetDC(handle);require(context!=nullptr,"Cannot get test HDC");
    }
    ~HiddenWindow() {if(context) ReleaseDC(handle,context);if(handle) DestroyWindow(handle);}
};
int main() {
    try {
        HiddenWindow game,other;
        const std::array<d2vr::EyeSize,2> recommendations{{{2496,2688},{2448,2640}}},limits{{{4096,4096},{4096,4096}}};
        const auto recommended=d2vr::recommended_output(recommendations,limits,{8192,8192});
        require(recommended.width==2496 && recommended.height==2688,"Both eyes' recommendation was not enclosed");
        const std::array<d2vr::EyeFov,2> optical{{{-.942477763f,.69813168f,.767944872f,-.959931076f},
                                               {-.69813168f,.942477763f,.767944872f,-.959931076f}}};
        const auto density=d2vr::symmetric_output(recommendations,limits,{8192,8192},optical);
        require(density.width==3102 && density.height==3208,"Symmetric optical-density output differs from the physical frusta");
        const auto larger=d2vr::scaled_eye_size(density,1.2,{4096,4096});
        require(larger.size.width==3722 && larger.size.height==3850 && !larger.limited,"120 percent must scale both dimensions");
        const auto smaller=d2vr::scaled_eye_size(density,.75,{4096,4096});
        require(smaller.size.width==2327 && smaller.size.height==2406,"75 percent must decrease both dimensions");
        const auto limited=d2vr::scaled_eye_size(density,2,{4096,4096});
        require(limited.limited && limited.size.height==4096 && limited.size.width<=4096 && limited.effective<2,"Reported scale must reflect the allocation limit");
        // The runtime maximum, not the old 4096 diagnostic bound, caps the scale.
        const auto full=d2vr::scaled_eye_size(density,2,{8192,8192});
        require(!full.limited && full.size.width==6204 && full.size.height==6416 && full.effective==2,
            "200 percent at the runtime maximum must exceed the old diagnostic bound");
        const auto half=d2vr::scaled_eye_size(density,1.5,{8192,8192});
        require(!half.limited && half.size.width==4653 && half.size.height==4812,
            "150 percent at the runtime maximum must exceed the old diagnostic bound");
        const auto double_recommended=d2vr::scaled_eye_size(recommended,2,{8192,8192});
        require(!double_recommended.limited && double_recommended.size.width==4992 && double_recommended.size.height==5376,
            "200 percent of 2496x2688 must exceed the old diagnostic bound");
        const auto runtime_limited=d2vr::scaled_eye_size(density,2,{5000,5000});
        require(runtime_limited.limited && runtime_limited.effective<1.6 && runtime_limited.size.height==5000 &&
            runtime_limited.size.width<5000,"Reported scale must reflect the runtime maximum");
        bool multiplier_rejected=false;
        try {d2vr::scaled_eye_size(density,std::numeric_limits<double>::quiet_NaN(),{4096,4096});} catch(const std::invalid_argument&) {multiplier_rejected=true;}
        require(multiplier_rejected,"Invalid resolution multiplier was accepted");
        bool density_rejected=false;
        try {d2vr::symmetric_output(recommendations,limits,{3000,3000},optical);} catch(const std::runtime_error&) {density_rejected=true;}
        require(density_rejected,"Optical-density output exceeded the runtime limit");
        bool rejected=false;
        try {d2vr::recommended_output(recommendations,limits,{2048,2048});} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Unsupported recommendation was silently accepted");
        alignas(8) LONG64 cached=(static_cast<LONG64>(360)<<32)|640;
        d2vr::CachedOutputSize cache;cache.apply(&cached,recommended);
        require(static_cast<UINT>(cached)==2496 && static_cast<UINT64>(cached)>>32==2688,"Cached native viewport stayed at desktop size");
        cache.update(density);
        require(static_cast<UINT>(cached)==3102 && static_cast<UINT64>(cached)>>32==3208,"Located optical request did not update cached dimensions");
        require(cache.restore() && cached==((static_cast<LONG64>(360)<<32)|640),"Cached native viewport was not restored");
        cache.apply(&cached,recommended);cached=(static_cast<LONG64>(360)<<32)|640;
        cache.update(density);
        require(static_cast<UINT>(cached)==3102 && cache.restore(),"A restored native cache pair prevented VR re-entry");
        cache.apply(&cached,recommended);cached=(static_cast<LONG64>(3208)<<32)|3102;
        cache.update(density);
        require(cache.restore() && cached==((static_cast<LONG64>(360)<<32)|640),"Native requested-size acknowledgement was not adopted/restored");
        cache.apply(&cached,recommended);cached=(static_cast<LONG64>(720)<<32)|1280;
        bool update_rejected=false;
        try {cache.update(density);} catch(const std::runtime_error&) {update_rejected=true;}
        require(update_rejected && static_cast<UINT>(cached)==1280,"Concurrent output update was overwritten");
        require(!cache.restore() && static_cast<UINT>(cached)==1280,"Concurrent native resize was overwritten");
        cached=(static_cast<LONG64>(2688)<<32)|2496;require(cache.restore(),"Cannot restore owned cache value");
        using Refresh=d2vr::CachedOutputSize::RefreshStatus;
        require(cache.refresh()==Refresh::deferred,"Unowned cache refresh must defer");
        const auto desktop_pair=(static_cast<LONG64>(485)<<32)|841;
        const auto vr_pair=(static_cast<LONG64>(2688)<<32)|2496;
        cached=desktop_pair;cache.apply(&cached,recommended);
        require(cache.refresh()==Refresh::unchanged && cached==vr_pair,"Owned cache refresh changed the current VR pair");
        InterlockedExchange64(&cached,desktop_pair); // Native focus return restores its original output cache.
        require(cache.refresh()==Refresh::reapplied && cached==vr_pair,"Focus-reset viewport was not atomically reapplied");
        for(const auto mixed:{(static_cast<LONG64>(2688)<<32)|841,(static_cast<LONG64>(485)<<32)|2496,
                              (static_cast<LONG64>(720)<<32)|1280}) {
            InterlockedExchange64(&cached,mixed);
            require(cache.refresh()==Refresh::deferred && cached==mixed,"Mixed/foreign native pair was overwritten by refresh");
        }
        require(!cache.restore(),"Refresh lost foreign-pair restoration protection");
        InterlockedExchange64(&cached,desktop_pair);
        require(cache.refresh()==Refresh::reapplied && cache.restore() && cached==desktop_pair,
                "Refresh changed the original restoration value");
        cache.apply(&cached,recommended);cache.update(density);InterlockedExchange64(&cached,desktop_pair);
        require(cache.refresh()==Refresh::reapplied && static_cast<UINT>(cached)==3102 &&
                static_cast<UINT64>(cached)>>32==3208 && cache.restore() && cached==desktop_pair,
                "Refresh reapplied an obsolete requested size after settings update");
        cache.apply(&cached,recommended);
        const auto resized_pair=(static_cast<LONG64>(756)<<32)|1273;
        InterlockedExchange64(&cached,resized_pair);
        require(cache.refresh({1273,717})==Refresh::deferred && cached==resized_pair,
                "Unmatched client height was accepted as a native resize");
        require(cache.refresh({1273,756})==Refresh::reapplied && cached==vr_pair,
                "Legitimate resized desktop window did not recover VR output");
        require(cache.restore() && cached==resized_pair,"Recovery did not restore the latest real client pair");
        cache.apply(&cached,recommended);
        const auto mixed_resize=(static_cast<LONG64>(2688)<<32)|1273;
        InterlockedExchange64(&cached,mixed_resize);
        require(cache.refresh({1273,756})==Refresh::deferred && cached==mixed_resize,
                "Mixed resized native pair was overwritten");
        InterlockedExchange64(&cached,vr_pair);require(cache.restore(),"Cannot restore resized cache fixture");
        d2vr::NativeOutputSize output;output.request(game.handle,recommended);
        UINT width=640,height=360;
        require(!output.apply(other.context,&width,&height) && width==640 && height==360,"Foreign window's size was overridden");
        require(output.apply(game.context,&width,&height) && width==2496 && height==2688,"Native renderer size was not overridden");
        RECT client{};require(GetClientRect(game.handle,&client) && client.right==640 && client.bottom==360,"Real input window was resized");
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=640;desc.BufferDesc.Height=360;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=1;desc.OutputWindow=game.handle;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> chain;
        require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &desc,&chain,&device,nullptr,&context)),"Cannot create software DXGI test swapchain");
        // Exercise the same typed-source -> typeless-XR copy using actual GPU
        // resources and read back the pixels, rather than only testing enums.
        std::array<unsigned char,8*4*4> pixels{};
        for(std::size_t i=0;i<pixels.size();++i) pixels[i]=static_cast<unsigned char>((i*37+11)&255);
        D3D11_TEXTURE2D_DESC copy_desc{};copy_desc.Width=8;copy_desc.Height=4;
        copy_desc.MipLevels=1;copy_desc.ArraySize=1;copy_desc.SampleDesc.Count=1;
        copy_desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;copy_desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> destination,staging;
        require(SUCCEEDED(device->CreateTexture2D(&copy_desc,nullptr,&destination)),"Cannot create typeless eye storage");
        require(d2vr::eye_texture_matches(copy_desc,8,4,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),"Valid typeless eye was rejected");
        auto invalid=copy_desc;invalid.ArraySize=2;
        require(!d2vr::eye_texture_matches(invalid,8,4,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),"Array eye mismatch was accepted");
        invalid=copy_desc;invalid.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        require(!d2vr::eye_texture_matches(invalid,8,4,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),"Foreign colour group was accepted");
        copy_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;copy_desc.Usage=D3D11_USAGE_STAGING;copy_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        require(SUCCEEDED(device->CreateTexture2D(&copy_desc,nullptr,&staging)),"Cannot create eye readback");
        for(const auto source_format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}) {
            auto source_desc=copy_desc;source_desc.Format=source_format;source_desc.Usage=D3D11_USAGE_IMMUTABLE;source_desc.CPUAccessFlags=0;
            source_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=pixels.data();initial.SysMemPitch=8*4;
            ComPtr<ID3D11Texture2D> source;
            require(SUCCEEDED(device->CreateTexture2D(&source_desc,&initial,&source)),"Cannot create typed game source");
            context->CopyResource(destination.Get(),source.Get());context->CopyResource(staging.Get(),destination.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Cannot map eye pixels");
            bool equal=true;
            for(UINT row=0;row<4;++row) equal=equal && std::memcmp(static_cast<unsigned char*>(mapped.pData)+row*mapped.RowPitch,
                pixels.data()+row*8*4,8*4)==0;
            context->Unmap(staging.Get(),0);require(equal,"Typed/typeless eye copy altered pixels");
            const auto read_source=d2vr::readback_rgba(source.Get()),read_destination=d2vr::readback_rgba(destination.Get());
            require(read_source.width==8 && read_source.height==4 && read_destination.format==DXGI_FORMAT_R8G8B8A8_TYPELESS &&
                read_source.bytes==read_destination.bytes && read_source.bytes.size()==pixels.size() &&
                std::memcmp(read_source.bytes.data(),pixels.data(),pixels.size())==0,"Diagnostic readback differs from real copied pixels");
            std::array<unsigned char,8*4*4> dark{};
            context->UpdateSubresource(destination.Get(),0,nullptr,dark.data(),8*4,0);
            const auto black=d2vr::readback_rgba(destination.Get());
            require(black.bytes!=read_source.bytes && std::memcmp(black.bytes.data(),dark.data(),dark.size())==0,
                "Black destination was mistaken for the game source");
        }
        // The readback follows the render size instead of capping it, and an
        // unreadable resource yields nothing rather than failing the frame.
        D3D11_TEXTURE2D_DESC wide_desc{};wide_desc.Width=4200;wide_desc.Height=4;
        wide_desc.MipLevels=1;wide_desc.ArraySize=1;wide_desc.SampleDesc.Count=1;
        wide_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;wide_desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> wide;
        require(SUCCEEDED(device->CreateTexture2D(&wide_desc,nullptr,&wide)),"Cannot create above-bound eye texture");
        const auto wide_read=d2vr::readback_rgba(wide.Get());
        require(wide_read.width==4200 && wide_read.height==4 && wide_read.bytes.size()==4200u*4*4,
            "A width above the old readback bound did not read back");
        auto foreign=wide_desc;foreign.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        ComPtr<ID3D11Texture2D> foreign_texture;
        require(SUCCEEDED(device->CreateTexture2D(&foreign,nullptr,&foreign_texture)),"Cannot create foreign-format eye texture");
        auto unreadable=d2vr::readback_rgba(foreign_texture.Get());
        require(!unreadable.width && !unreadable.height && unreadable.bytes.empty(),
            "An unreadable resource did not yield an empty readback");
        auto arrayed=wide_desc;arrayed.ArraySize=2;
        ComPtr<ID3D11Texture2D> array_texture;
        require(SUCCEEDED(device->CreateTexture2D(&arrayed,nullptr,&array_texture)),"Cannot create arrayed eye texture");
        unreadable=d2vr::readback_rgba(array_texture.Get());
        require(unreadable.bytes.empty(),"A non-2D eye resource did not yield an empty readback");
        ComPtr<ID3D11Texture2D> held;
        require(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&held))),"Cannot retain test backbuffer");
        require(chain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0)==DXGI_ERROR_INVALID_CALL,"Retained backbuffer did not block resize");
        held.Reset();
        require(SUCCEEDED(chain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0)),"Resize failed after releasing capture reference");
        require(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&held))),"Cannot read resized backbuffer");
        D3D11_TEXTURE2D_DESC actual{};held->GetDesc(&actual);
        require(actual.Width==2496 && actual.Height==2688,"GPU output differs from the recommendation");
        held.Reset();output.clear();width=640;height=360;
        require(!output.apply(game.context,&width,&height) && width==640 && height==360,"Native size request was not cleared");
        require(SUCCEEDED(chain->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0)),"Cannot restore DXGI output from real client size");
        require(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&held))),"Cannot read restored backbuffer");held->GetDesc(&actual);
        require(actual.Width==640 && actual.Height==360,"DXGI output was not restored to the client window");
        std::cout << "PASS: typed/typeless RGBA8 eye copies preserve pixels; runtime-bounded 1.5x/2.0x scaling above 4096, empty readback for unreadable resources, recommended 2496x2688 GPU output, unchanged input window and restored output, WARP\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
