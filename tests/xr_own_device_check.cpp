// v1453: XrOwnDevice (src/xr_own_device.hpp), the XR session's own D3D11 device: mirrors on the game's device, copies into the
// runtime's images across two shared fences, OpenXR's acquire/wait/release order. Build and run:
// build\night\lab.cmd xr_own_device_check "d3d11.lib dxgi.lib"
#include "xr_own_device.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;
static int failures=0;
static void check(bool ok,const char* what) {if(!ok) {++failures;std::printf("FAIL %s\n",what);} else std::printf("ok   %s\n",what);}

int main() {
    ComPtr<ID3D11Device> game;ComPtr<ID3D11DeviceContext> game_ctx;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_1;
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,&level,1,D3D11_SDK_VERSION,&game,nullptr,&game_ctx);
    const bool hardware=SUCCEEDED(hr);
    if(!hardware) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,&level,1,D3D11_SDK_VERSION,&game,nullptr,&game_ctx);
    if(FAILED(hr)) {std::printf("no D3D11 device\n");return 1;}
    std::printf("     device: %s\n",hardware?"hardware":"WARP");
    d2vr::XrOwnDevice own;
    check(!own.create(nullptr) && !own && own.why()=="no game device","no game device: refused, nothing kept");
    check(own.create(game.Get()) && own && own.device() && own.device()!=game.Get(),"own device on the game's adapter, fences shared, round trip passed");
    std::printf("     why: \"%s\"\n",own.why().c_str());
    if(!own) {std::printf("FAILED %d\n",failures+1);return 1;}
    ComPtr<ID3D11Multithread> mt;ComPtr<ID3D11DeviceContext> own_ctx;own.device()->GetImmediateContext(&own_ctx);own_ctx.As(&mt);
    check(mt && mt->GetMultithreadProtected(),"the own device is multithread-protected");

    // A swapchain of 3 runtime images on the own device, as a runtime would create it (typeless, as runtimes often do)
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> runtime[3];
    for(auto& t:runtime) own.device()->CreateTexture2D(&desc,nullptr,&t);
    ID3D11Texture2D* images[3]={runtime[0].Get(),runtime[1].Get(),runtime[2].Get()};
    const std::uintptr_t chain=0x1234;
    check(own.mirror(chain,images,3),"mirrors made for a typeless 3-image swapchain");
    ComPtr<ID3D11Device> owner;images[0]->GetDevice(&owner);
    check(images[0]!=runtime[0].Get() && owner.Get()==game.Get(),"... handed back as textures on the game's device");
    D3D11_TEXTURE2D_DESC got{};images[1]->GetDesc(&got);
    check(got.Width==64 && got.Height==48 && got.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS && (got.BindFlags&D3D11_BIND_RENDER_TARGET),"... with the runtime image's description");
    ID3D11Texture2D* again[3]={runtime[0].Get(),runtime[1].Get(),runtime[2].Get()};
    check(own.mirror(chain,again,3) && again[0]==images[0] && again[2]==images[2],"enumerating again hands out the same mirrors");

    // Typeless images need a typed view to draw: make RTVs as GameXr does (UNORM)
    const auto draw=[&](ID3D11Texture2D* target,const float rgba[4]) {
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> rtv;game->CreateRenderTargetView(target,&view,&rtv);game_ctx->ClearRenderTargetView(rtv.Get(),rgba);
    };
    const auto runtime_pixel=[&](int i) {
        D3D11_TEXTURE2D_DESC d{};runtime[i]->GetDesc(&d);d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;own.device()->CreateTexture2D(&d,nullptr,&staging);
        own_ctx->CopyResource(staging.Get(),runtime[i].Get());  // typeless -> UNORM: same format group
        D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(own_ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&m))) return 0xdeadbeefu;
        std::uint32_t v{};std::memcpy(&v,static_cast<const std::uint8_t*>(m.pData)+m.RowPitch+4,4);own_ctx->Unmap(staging.Get(),0);return v;
    };
    // frame 1: image 0 red
    {   const float red[4]={1,0,0,1};
        own.acquired(chain,0);own.waited(chain);draw(images[0],red);own.released(chain);
        check(runtime_pixel(0)==0xff0000ffu,"frame 1: the game device's red reaches runtime image 0 (after the fences)");
    }
    // frames 2-3: images 1 and 2, then image 0 again (after its last copy)
    {   const float green[4]={0,1,0,1},blue[4]={0,0,1,1},white[4]={1,1,1,1};
        own.acquired(chain,1);own.waited(chain);draw(images[1],green);own.released(chain);
        own.acquired(chain,2);own.waited(chain);draw(images[2],blue);own.released(chain);
        own.acquired(chain,0);own.waited(chain);draw(images[0],white);own.released(chain);
        check(runtime_pixel(1)==0xff00ff00u && runtime_pixel(2)==0xffff0000u,"frames 2-3: green and blue in images 1 and 2");
        check(runtime_pixel(0)==0xffffffffu,"frame 4: image 0 drawn again after its last copy: white");
    }
    // two images held at once (acquire, acquire, wait, wait, release, release): releases follow the acquire order
    {   const float a[4]={1,1,0,1},b[4]={0,1,1,1};
        own.acquired(chain,1);own.acquired(chain,2);own.waited(chain);own.waited(chain);
        draw(images[1],a);draw(images[2],b);own.released(chain);own.released(chain);
        check(runtime_pixel(1)==0xff00ffffu && runtime_pixel(2)==0xffffff00u,"two images held: each copied to its own runtime image");
    }
    // a release with nothing waited does nothing (an abandoned acquire)
    {   const auto before=own.copies();own.acquired(chain,0);own.released(chain);
        check(own.copies()==before,"a release before the wait copies nothing");
        own.waited(chain);own.released(chain);check(own.copies()==before+1,"... and the image's own release after its wait copies it");
    }
    // a chain that is not mirrored is left alone
    {   const auto before=own.copies();own.acquired(0x999,0);own.waited(0x999);own.released(0x999);
        check(own.copies()==before,"an unknown swapchain is ignored");
    }
    // forget, and a mirror the game device cannot share (mipmapped) is refused
    {   own.forget(chain);check(own.chains()==0,"forget drops the chain");
        D3D11_TEXTURE2D_DESC mip=desc;mip.MipLevels=2;mip.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ComPtr<ID3D11Texture2D> t;own.device()->CreateTexture2D(&mip,nullptr,&t);
        ID3D11Texture2D* one[1]={t.Get()};
        check(!own.mirror(0x55,one,1) && one[0]==t.Get() && own.chains()==0,"a mipmapped image is refused, nothing replaced");
        check(own.mirror_failure().find("mipmapped")!=std::string::npos && own.mirror_failure().find("mips 2")!=std::string::npos,"... and the refusal names the image");
        std::printf("     refusal: \"%s\"\n",own.mirror_failure().c_str());
        D3D11_TEXTURE2D_DESC array=desc;array.ArraySize=2;array.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ComPtr<ID3D11Texture2D> a;own.device()->CreateTexture2D(&array,nullptr,&a);
        ID3D11Texture2D* two[1]={a.Get()};
        check(!own.mirror(0x56,two,1) && two[0]==a.Get() && own.chains()==0,"an array image is refused too");
    }
    // the own device works from another thread while the render thread queues copies (multithread protection)
    {   std::atomic<bool> stop{false};std::atomic<int> runs{0};
        std::thread other([&] {while(!stop.load()) {own_ctx->Flush();++runs;}});
        ID3D11Texture2D* imgs[3]={runtime[0].Get(),runtime[1].Get(),runtime[2].Get()};own.mirror(0x77,imgs,3);
        const float c[4]={.5f,.5f,.5f,1};
        for(int f=0;f<200;++f) {own.acquired(0x77,f%3);own.waited(0x77);draw(imgs[f%3],c);own.released(0x77);}
        stop=true;other.join();
        check(runs.load()>0 && runtime_pixel(2)!=0xdeadbeefu,"200 frames of copies beside another thread on the own device");
    }
    own.reset();check(!own && own.chains()==0,"reset drops everything");
    // v1457: status texts in JSON strings
    check(d2vr::json_text("plain text (ok)")=="plain text (ok)","json_text: plain text unchanged");
    check(d2vr::json_text("a \"quoted\" word")=="a \\\"quoted\\\" word" && d2vr::json_text("C:\\path")=="C:\\\\path","json_text: quotes and backslashes escaped");
    check(d2vr::json_text(std::string("line\nbreak"))=="line\\u000abreak","json_text: control characters as \\u escapes");
    std::printf(failures?"FAILED %d\n":"ALL PASSED\n",failures);
    return failures?1:0;
}
