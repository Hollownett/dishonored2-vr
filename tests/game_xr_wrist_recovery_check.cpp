#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace sim=d2vr::simxr;
using Microsoft::WRL::ComPtr;
static unsigned created{},timeouts{};
static bool broken{},observer_fault{};
static XrResult XRAPI_CALL create_chain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    const auto result=sim::create_chain(session,info,out);if(XR_FAILED(result))return result;
    ++created;
    // Real OpenXR DXGI images are typeless, unlike the old simulation.
    auto& chain=*sim::chains.at(*out);
    for(auto& texture:chain.textures) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        if(broken && (info->width==128 || info->width==192))desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        texture.Reset();assert(SUCCEEDED(sim::device->CreateTexture2D(&desc,nullptr,&texture)));
    }
    return result;
}
static XrResult XRAPI_CALL wait_image(XrSwapchain chain,const XrSwapchainImageWaitInfo* info) {
    if(sim::chains.at(chain)->info.width==128 && timeouts) {
        assert(info->timeout==0);--timeouts;return XR_TIMEOUT_EXPIRED;
    }
    return sim::wait_image(chain,info);
}
static XrResult XRAPI_CALL dispatch(XrInstance instance,const char* name,PFN_xrVoidFunction* out) {
    if(std::strcmp(name,"xrCreateSwapchain")==0){*out=reinterpret_cast<PFN_xrVoidFunction>(&create_chain);return XR_SUCCESS;}
    if(std::strcmp(name,"xrWaitSwapchainImage")==0){*out=reinterpret_cast<PFN_xrVoidFunction>(&wait_image);return XR_SUCCESS;}
    return sim::dispatch(instance,name,out);
}
static void observe(unsigned,ID3D11Texture2D*,std::uintptr_t) {
    if(observer_fault)throw std::runtime_error("injected optional layer draw failure");
}
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,UINT width,UINT height) {
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> out;assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&out)));return out;
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    const auto scene=texture(device.Get(),64,64),art=texture(device.Get(),90,24),grown=texture(device.Get(),160,24);
    for(unsigned scenario=0;scenario<3;++scenario) {
        sim::configure(device.Get());created=0;timeouts=scenario==2?2:0;broken=scenario==1;observer_fault=false;
        sim::control.grips[1]={{0,.70710678f,0,.70710678f},{.1f,1.5f,-.45f}}; // health watch: right inner wrist toward the eyes
        {
            d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&dispatch);
            xr.legacy_wrist_hud=true; // this check covers the pre-v1013 two-wrist layout and its retirement paths
            xr.observe_native_wrist_art=&observe;
            const auto frame=[&]{
                if(const auto views=xr.tracking_views())for(unsigned eye=0;eye<2;++eye)xr.record_rendered_eye(eye,views->at(eye).fov);
                xr.present(scene.Get(),scene.Get(),1.6f,1.6f,true,false,true);
            };
            frame();frame();assert(xr.last_tracked_pair && xr.last_layer_submitted);
            assert(xr.native_art_source(1,true,art.Get(),{0,0,0,90,24,1},123,xr.pose_epoch(),GetTickCount64()));
            frame();assert(xr.last_tracked_pair && xr.last_layer_submitted && !xr.native_art_presented(1,true)); // the canvas is created between frames: this one only records its size
            frame();assert(xr.last_tracked_pair && xr.last_layer_submitted);
            if(scenario==2)assert(!xr.native_art_presented(1,true));
            if(scenario==1) {
                assert(xr.wrist_failures==1 && FAILED(xr.wrist_failure_hresult));
                assert(sim::summary.overlays.empty() && xr.wrist_device_removed==S_OK);
                assert(!xr.native_art_presented(1,true));
            }else {
                frame();frame();assert(!sim::summary.overlays.empty());
                // The legacy path sizes the quad from the 64-pixel-rounded canvas (90 px of art in a 128 px canvas), so the
                // submitted width is .0996 m; it is still the smaller pre-v1013 watch, not the old .11 m face.
                assert(std::any_of(sim::summary.overlays.begin(),sim::summary.overlays.end(),[](const auto& o){return o.size.width>0 && o.size.width<.11f;}));
                assert(xr.native_art_presented(1,true));
                assert(xr.wrist_target_creations==3); // cached, typed views
                const unsigned base=created; // projection + wrist (+ the left-wrist controls card once that wrist faces the eyes)
                if(scenario==2)assert(xr.wrist_timeouts==2);
                assert(xr.native_art_source(1,true,art.Get(),{0,0,0,89,24,1},123,xr.pose_epoch(),GetTickCount64()));
                frame();assert(xr.wrist_target_creations==3 && created==base); // one-pixel animation reuses canvas
                assert(xr.native_art_source(1,true,grown.Get(),{0,0,0,150,24,1},123,xr.pose_epoch(),GetTickCount64()));
                frame(); // The outgrown canvas is only flagged inside the frame.
                frame(); // First retirement poll (between frames) fences the old GPU work and skips only this panel.
                assert(created==base && xr.wrist_retirement_waits>0 && xr.last_layer_submitted);
                assert(!xr.native_art_presented(1,true) && sim::chains.size()==10); // two eyes, menu band, pointer, settings panel, controls card, wrist, wheel dot, comfort vignette, black fallback quad (not run on Linux: wined3d)
                for(unsigned poll=0;poll<100 && created==base;++poll) {Sleep(1);frame();}
                assert(created==base+1 && xr.wrist_target_creations==6 && sim::chains.size()==base);
                assert(xr.native_art_presented(1,true));
                // Plan E task 4: unchanged art re-submits the last released image without drawing, so the observer (and its fault) needs a changed crop.
                assert(xr.native_art_source(1,true,grown.Get(),{0,0,0,149,24,1},123,xr.pose_epoch(),GetTickCount64()));
                observer_fault=true;frame();assert(xr.wrist_failures==1 && xr.last_layer_submitted);
                assert(sim::summary.overlays.empty());observer_fault=false;
                assert(!xr.native_art_presented(1,true));
            }
            assert(sim::summary.protocol_errors==0 && sim::summary.chain_ops_in_frame==0 && xr.swapchain_creates_in_frame==0);
        }
        assert(sim::chains.empty());
    }
    // v1015 default path: the single left back-of-hand watch shows one texture containing the game's own MasterHUD
    // cluster. The head is 0.46 m along the hand's dorsal normal and looks back at the wrist.
    {
        sim::configure(device.Get());created=0;timeouts=0;broken=false;observer_fault=false;
        sim::control.head={{0,std::sin(-.785398f),0,std::cos(-.785398f)},{-0.7f,1.3f,-0.4f}};
        sim::control.grips[0]={{0,0,0,1},{-0.2f,1.3f,-0.4f}};
        sim::control.aims[0]=sim::control.grips[0];
        {
            d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&dispatch);
            const auto frame=[&]{
                if(const auto views=xr.tracking_views())for(unsigned eye=0;eye<2;++eye)xr.record_rendered_eye(eye,views->at(eye).fov);
                xr.present(scene.Get(),scene.Get(),1.6f,1.6f,true,false,true);
            };
            const auto cluster_pixels=[](unsigned size) {
                std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size)*size*4,0);
                for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
                    const auto at=(static_cast<std::size_t>(y)*size+x)*4;
                    const bool right=x>=size/2,bottom=y>=size/2;
                    pixels[at+0]=static_cast<std::uint8_t>(right?0u:255u);
                    pixels[at+1]=static_cast<std::uint8_t>(bottom?255u:0u);
                    pixels[at+2]=static_cast<std::uint8_t>(right?255u:0u);
                    if(right && bottom) {pixels[at+0]=pixels[at+1]=pixels[at+2]=255;}
                    pixels[at+3]=255;
                }
                return pixels;
            };
            const auto cluster_device_texture=[&](unsigned size,const std::vector<std::uint8_t>& pixels) {
                D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.MipLevels=1;desc.ArraySize=1;
                desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
                desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA data{pixels.data(),size*4,0};
                ComPtr<ID3D11Texture2D> texture;assert(SUCCEEDED(device->CreateTexture2D(&desc,&data,&texture)));
                return texture;
            };
            const auto read_watch_pixel=[&](unsigned x,unsigned y) {
                ID3D11Texture2D* watch_texture=nullptr;
                for(auto& entry:sim::chains) if(entry.second->info.width==512 && entry.second->info.height==512) {
                    watch_texture=entry.second->textures[entry.second->current].Get();break;
                }
                assert(watch_texture);
                D3D11_TEXTURE2D_DESC wd{};watch_texture->GetDesc(&wd);
                wd.Usage=D3D11_USAGE_STAGING;wd.BindFlags=0;wd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;wd.MiscFlags=0;
                ComPtr<ID3D11Texture2D> staged;assert(SUCCEEDED(device->CreateTexture2D(&wd,nullptr,&staged)));
                context->CopyResource(staged.Get(),watch_texture);
                D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staged.Get(),0,D3D11_MAP_READ,0,&mapped)));
                const auto* pixel=static_cast<const std::uint8_t*>(mapped.pData)+mapped.RowPitch*y+x*4;
                std::array<std::uint8_t,4> out{pixel[0],pixel[1],pixel[2],pixel[3]};
                context->Unmap(staged.Get(),0);return out;
            };
            for(unsigned i=0;i<4;++i) {frame();Sleep(5);}
            assert(xr.wrist_watch_shown() && xr.wrist_watch_opacity()>0);
            const auto pixels=cluster_pixels(256);
            const auto cluster=cluster_device_texture(256,pixels);
            UINT64 generation=1;
            const auto publish=[&](ID3D11Texture2D* texture,UINT64 value) {
                return xr.wrist_watch_cluster_source(texture,256,256,value,GetTickCount64(),123,xr.pose_epoch());
            };
            assert(publish(cluster.Get(),generation));
            assert(xr.wrist_watch_cluster_ready());
            for(unsigned i=0;i<30;++i) {publish(cluster.Get(),generation);frame();Sleep(5);}
            assert(xr.last_layer_submitted && xr.wrist_watch_opacity()==1);
            assert(xr.wrist_watch_cluster_updates()==1 && xr.wrist_watch_composite_rejects()==0 && xr.wrist_watch_cluster_rejects()==0);
            assert(std::any_of(sim::summary.overlays.begin(),sim::summary.overlays.end(),[](const auto& o){return std::abs(o.size.width-.085f)<1e-5f;}));
            assert(std::any_of(sim::chains.begin(),sim::chains.end(),[](const auto& entry){return entry.second->info.width==512 && entry.second->info.height==512;}));
            // The game cluster reached the watch through the alpha blit unchanged at alpha 1: four distinct quadrants.
            {const auto red=read_watch_pixel(64,64),blue=read_watch_pixel(448,64);
             const auto yellow=read_watch_pixel(64,448),white=read_watch_pixel(448,448);
             const auto same=[](const std::array<std::uint8_t,4>& p,std::uint8_t r,std::uint8_t g,std::uint8_t b) {
                 return std::abs(static_cast<int>(p[0])-r)<=2 && std::abs(static_cast<int>(p[1])-g)<=2 && std::abs(static_cast<int>(p[2])-b)<=2 && p[3]>=253;};
             if(!same(red,255,0,0)) std::cerr<<"watch red "<<int(red[0])<<','<<int(red[1])<<','<<int(red[2])<<','<<int(red[3])<<'\n';
             if(!same(blue,0,0,255)) std::cerr<<"watch blue "<<int(blue[0])<<','<<int(blue[1])<<','<<int(blue[2])<<','<<int(blue[3])<<'\n';
             if(!same(yellow,255,255,0)) std::cerr<<"watch yellow "<<int(yellow[0])<<','<<int(yellow[1])<<','<<int(yellow[2])<<','<<int(yellow[3])<<'\n';
             if(!same(white,255,255,255)) std::cerr<<"watch white "<<int(white[0])<<','<<int(white[1])<<','<<int(white[2])<<','<<int(white[3])<<'\n';
             assert(same(red,255,0,0) && same(blue,0,0,255) && same(yellow,255,255,0) && same(white,255,255,255));}
            // v1015 no regeneration per frame: the same source and pose leave the watch image and the cluster update
            // counters alone; a new cluster generation forces exactly one new draw.
            {const auto draws=xr.wrist_watch_draws(),updates=xr.wrist_watch_cluster_updates();
             for(unsigned i=0;i<3;++i) {publish(cluster.Get(),generation);frame();}
             if(xr.wrist_watch_draws()>draws+1 || xr.wrist_watch_cluster_updates()!=updates)
                 std::cerr<<"no-regen draws "<<draws<<"->"<<xr.wrist_watch_draws()<<" updates "<<updates<<"->"<<xr.wrist_watch_cluster_updates()<<'\n';
             // No new cluster content: the update counter cannot move. A draw may happen at most once (the overlay's own
             // 500 ms refresh), never once per frame.
             assert(xr.wrist_watch_draws()<=draws+1 && xr.wrist_watch_cluster_updates()==updates);
             generation=2;
             assert(publish(cluster.Get(),generation));
             const auto before=xr.wrist_watch_draws();
             for(unsigned i=0;i<20 && xr.wrist_watch_draws()==before;++i) {publish(cluster.Get(),generation);frame();Sleep(2);}
             assert(xr.wrist_watch_draws()>=before+1 && xr.wrist_watch_cluster_updates()==updates+1);}
            // Invalid publication is rejected and clears the source; a valid one restores it. A stale tick is not drawn.
            {const auto rejects=xr.wrist_watch_cluster_rejects();
             assert(!xr.wrist_watch_cluster_source(cluster.Get(),256,256,generation,GetTickCount64(),0,xr.pose_epoch()));
             assert(xr.wrist_watch_cluster_rejects()==rejects+1 && !xr.wrist_watch_cluster_ready());
             assert(xr.wrist_watch_cluster_source(cluster.Get(),256,256,generation,GetTickCount64()-1000,123,xr.pose_epoch()));
             assert(!xr.wrist_watch_cluster_ready());
             assert(xr.wrist_watch_cluster_source(cluster.Get(),256,256,generation+1,GetTickCount64(),123,xr.pose_epoch()));
             assert(xr.wrist_watch_cluster_ready());}
            // The optional backdrop is off by default. A transparent cluster corner is fully transparent without it and
            // the premultiplied dark card with it; both are checked through the same 512-pixel watch image.
            {std::vector<std::uint8_t> transparent(256u*256u*4,0);
             for(unsigned y=64;y<192;++y) for(unsigned x=64;x<192;++x) {
                 const auto at=(static_cast<std::size_t>(y)*256+x)*4;
                 transparent[at+0]=255;transparent[at+3]=255;
             }
             const auto card=cluster_device_texture(256,transparent);
             xr.wrist_hud_backdrop=false;
             assert(publish(card.Get(),++generation));
             for(unsigned i=0;i<20;++i) {publish(card.Get(),generation);frame();Sleep(3);}
             const auto clear=read_watch_pixel(8,8);
             if(clear[3]!=0) std::cerr<<"watch transparent corner "<<int(clear[0])<<','<<int(clear[1])<<','<<int(clear[2])<<','<<int(clear[3])<<'\n';
             assert(clear[3]==0);
             xr.wrist_hud_backdrop=true;
             assert(publish(card.Get(),++generation));
             const auto backdrop_draws=xr.wrist_watch_draws();
             for(unsigned i=0;i<20;++i) {publish(card.Get(),generation);frame();Sleep(3);}
             const auto card_pixel=read_watch_pixel(8,8);
             if(card_pixel[3]<=100 || card_pixel[3]>=130 || card_pixel[0]>=card_pixel[3] || card_pixel[1]>=card_pixel[3] || card_pixel[2]>=card_pixel[3]) {
                 std::cerr<<"backdrop corner "<<int(card_pixel[0])<<','<<int(card_pixel[1])<<','<<int(card_pixel[2])<<','<<int(card_pixel[3])
                     <<" draws "<<backdrop_draws<<"->"<<xr.wrist_watch_draws()<<" opacity "<<xr.wrist_watch_opacity()<<" shown "<<xr.wrist_watch_shown()<<'\n';
                 for(auto& entry:sim::chains) if(entry.second->info.width==512 && entry.second->info.height==512) {
                     for(unsigned image=0;image<3;++image) {
                         D3D11_TEXTURE2D_DESC wd{};entry.second->textures[image]->GetDesc(&wd);
                         wd.Usage=D3D11_USAGE_STAGING;wd.BindFlags=0;wd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;wd.MiscFlags=0;
                         ComPtr<ID3D11Texture2D> staged;assert(SUCCEEDED(device->CreateTexture2D(&wd,nullptr,&staged)));
                         context->CopyResource(staged.Get(),entry.second->textures[image].Get());
                         D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staged.Get(),0,D3D11_MAP_READ,0,&mapped)));
                         const auto* pixel=static_cast<const std::uint8_t*>(mapped.pData)+mapped.RowPitch*8+8*4;
                         std::cerr<<"image "<<image<<" current "<<entry.second->current<<" pixel "<<int(pixel[0])<<','<<int(pixel[1])<<','<<int(pixel[2])<<','<<int(pixel[3])<<'\n';
                         context->Unmap(staged.Get(),0);
                     }
                 }
             }
             assert(card_pixel[3]>100 && card_pixel[3]<130 && card_pixel[0]<card_pixel[3] && card_pixel[1]<card_pixel[3] && card_pixel[2]<card_pixel[3]);
             xr.wrist_hud_backdrop=false;}
            // Lab timing of the Present-thread cluster publication check only; the game-cluster image itself is
            // generated entirely on the GPU by dxgi_probe (no CPU pixels, no upload).
            {
                const auto fixed_tick=GetTickCount64();
                const auto start=std::chrono::steady_clock::now();
                for(unsigned i=0;i<20000;++i) assert(xr.wrist_watch_cluster_source(cluster.Get(),256,256,generation,fixed_tick,123,xr.pose_epoch()));
                const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/20000.;
                std::cout<<"LAB watch_cluster_publish_us="<<us<<" (same source fast path)\n";
            }
            assert(sim::summary.protocol_errors==0 && sim::summary.chain_ops_in_frame==0 && xr.swapchain_creates_in_frame==0);
        }
        assert(sim::chains.empty());
    }
    std::cout << "PASS: typed wrist textures, cached views, allocation/draw faults, nonblocking wait recovery, game-cluster watch source, content-gated draws and the optional backdrop\n";
}
