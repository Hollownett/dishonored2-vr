// Lab for src/spyglass_scope.hpp: the right-eye scope. Zoom factor -> narrowed projection, scope size from the runtime FOV, the rim,
// the scope centre of a canted view, the entry / exit / reset state machine, and the real mask pass on a WARP device (typed sRGB eye
// as the sim's chain, typeless as the headset's): white stays white in the middle, black outside, a dark rim between.
#define NOMINMAX
#include "../src/spyglass_scope.hpp"
#include "../src/spyglass_lens.hpp"
#include "../src/canted_fov.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace d2vr;
static bool close_to(double a,double b,double e) {return std::abs(a-b)<=e;}
static double rad(double deg) {return deg*spyglass_scope::pi/180;}
static void must(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);std::exit(1);}}

static void gpu_lab() {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    must(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
    const int w=400,h=400;const XrFovf fov{-static_cast<float>(rad(50)),static_cast<float>(rad(50)),static_cast<float>(rad(50)),-static_cast<float>(rad(50))};
    const double R=spyglass_scope::radius_tan(fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown),T=std::tan(rad(50));
    SpyglassScopeMask mask(device.Get());
    for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_TYPELESS}) {
        std::vector<std::uint8_t> white(static_cast<std::size_t>(w)*h*4,255);
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.Format=format;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA init{white.data(),static_cast<UINT>(w*4),0};ComPtr<ID3D11Texture2D> eye,staging;
        must(SUCCEEDED(device->CreateTexture2D(&d,&init,&eye)),"eye");
        mask.draw(eye.Get(),fov,{0.f,0.f});
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;must(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"staging");
        context->CopyResource(staging.Get(),eye.Get());D3D11_MAPPED_SUBRESOURCE map{};must(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"map");
        const auto px=[&](int x,int y) {return static_cast<const std::uint8_t*>(map.pData)+y*map.RowPitch+x*4;};
        for(int c=0;c<3;++c) {
            must(px(w/2,h/2)[c]==255,"centre clear");
            must(px(3,3)[c]==0 && px(w-4,h-4)[c]==0 && px(3,h-4)[c]==0,"outside black");
        }
        const int edge=w/2+static_cast<int>(R*.98/T*w/2);   // just inside the radius: dark (.9 at the edge)
        must(px(edge,h/2)[0]<130 && px(edge,h/2)[0]>0,"rim dark");
        const int mid=w/2+static_cast<int>(R*.9/T*w/2);      // inside the rim ramp: partly dark
        must(px(mid,h/2)[0]>px(edge,h/2)[0] && px(mid,h/2)[0]<255,"rim ramp");
        must(px(w/2,h/2-static_cast<int>(R*.98/T*h/2))[0]<130,"circle vertical");   // a circle in the tangent plane (square image here)
        std::printf("gpu mask %s: centre %d edge %d mid %d corner %d\n",format==DXGI_FORMAT_R8G8B8A8_TYPELESS?"typeless":"srgb",px(w/2,h/2)[0],px(edge,h/2)[0],px(mid,h/2)[0],px(3,3)[0]);
        context->Unmap(staging.Get(),0);
    }
    // The centre moves with the axis: a scope centred at +R (right) is clear there and black at -1.5 R.
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    std::vector<std::uint8_t> white(static_cast<std::size_t>(w)*h*4,255);D3D11_SUBRESOURCE_DATA init{white.data(),static_cast<UINT>(w*4),0};
    ComPtr<ID3D11Texture2D> eye,staging;must(SUCCEEDED(device->CreateTexture2D(&d,&init,&eye)),"eye2");
    mask.draw(eye.Get(),fov,{static_cast<float>(R),0.f});
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;must(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"staging2");
    context->CopyResource(staging.Get(),eye.Get());D3D11_MAPPED_SUBRESOURCE map{};must(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"map2");
    const auto px=[&](int x,int y) {return static_cast<const std::uint8_t*>(map.pData)+y*map.RowPitch+x*4;};
    must(px(w/2+static_cast<int>(R/T*w/2),h/2)[0]==255 && px(w/2-static_cast<int>(R*.5/T*w/2),h/2)[0]==0,"off-centre scope");
    context->Unmap(staging.Get(),0);
}

// The game's lens art: a synthetic 64x64 mask (top 8 rows and left 8 columns opaque black, the rest clear, or mid grey for the multiply
// case) sampled over the scope's square. Checks orientation, the draw's own blend, black outside the square, and the dump.
static void art_lab() {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    must(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
    const int w=400,h=400;const XrFovf fov{-static_cast<float>(rad(50)),static_cast<float>(rad(50)),static_cast<float>(rad(50)),-static_cast<float>(rad(50))};
    const double R=spyglass_scope::radius_tan(fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown),T=std::tan(rad(50));
    const int half=static_cast<int>(R/T*w/2),cx=w/2,cy=h/2;   // the square's half side in pixels
    SpyglassScopeMask mask(device.Get());
    for(const bool multiply:{false,true}) {
        std::vector<std::uint8_t> art_px(64*64*4);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            auto* p=&art_px[(y*64+x)*4];const bool strip=y<8 || x<8;
            if(multiply) {p[0]=p[1]=p[2]=strip?0:128;p[3]=255;} else {p[0]=p[1]=p[2]=0;p[3]=strip?255:0;}
        }
        auto art=std::make_shared<LensArt>();
        D3D11_TEXTURE2D_DESC ad{};ad.Width=ad.Height=64;ad.MipLevels=ad.ArraySize=1;ad.SampleDesc.Count=1;ad.Usage=D3D11_USAGE_DEFAULT;ad.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ad.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA ai{art_px.data(),64*4,0};must(SUCCEEDED(device->CreateTexture2D(&ad,&ai,&art->texture)),"art texture");
        art->desc=ad;art->view.Format=ad.Format;art->view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;art->view.Texture2D.MipLevels=1;
        art->blend_enabled=!multiply;art->src=D3D11_BLEND_SRC_ALPHA;art->dst=D3D11_BLEND_INV_SRC_ALPHA;art->op=D3D11_BLEND_OP_ADD;
        must(spyglass_art::is_static_texture(ad),"art is static");
        if(!multiply) {art->dump_stem=std::filesystem::temp_directory_path()/L"spyglass-art-check";art->dump_pending=true;}
        for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_TYPELESS}) {
            std::vector<std::uint8_t> white(static_cast<std::size_t>(w)*h*4,255);
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA init{white.data(),static_cast<UINT>(w*4),0};ComPtr<ID3D11Texture2D> eye,staging;
            must(SUCCEEDED(device->CreateTexture2D(&d,&init,&eye)),"eye");
            mask.draw(eye.Get(),fov,{0.f,0.f},art);
            d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;must(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"staging");
            context->CopyResource(staging.Get(),eye.Get());D3D11_MAPPED_SUBRESOURCE map{};must(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"map");
            const auto px=[&](int x,int y) {return static_cast<int>(static_cast<const std::uint8_t*>(map.pData)[y*map.RowPitch+x*4]);};
            must(px(3,3)==0 && px(w-4,h-4)==0 && px(cx,cy-half-6)==0 && px(cx+half+6,cy)==0,"outside the art's square is black");
            must(px(cx,cy-half+8)==0,"top strip (texture rows 0..7) is at the top of the square");
            must(px(cx-half+8,cy)==0,"left strip is at the left of the square");
            const int centre=px(cx,cy),lower=px(cx,cy+half-12),right=px(cx+half-12,cy);
            if(multiply) {   // byte for byte on the headset's typeless chain (half of white); a typed sRGB chain (the sim) multiplies in linear light
                const int lo=format==DXGI_FORMAT_R8G8B8A8_TYPELESS?110:170,hi=format==DXGI_FORMAT_R8G8B8A8_TYPELESS?145:205;
                must(centre>lo && centre<hi && lower>lo && lower<hi && right>lo && right<hi,"multiply: the mask's grey over white");
            }
            else must(centre==255 && lower==255 && right==255,"alpha blend: clear parts keep the scene");
            std::printf("art %s %s: centre %d top-strip %d left-strip %d lower %d outside %d\n",multiply?"multiply":"blend",format==DXGI_FORMAT_R8G8B8A8_TYPELESS?"typeless":"srgb",centre,px(cx,cy-half+8),px(cx-half+8,cy),lower,px(3,3));
            context->Unmap(staging.Get(),0);
        }
        if(!multiply) {must(!art->dump_pending && std::filesystem::exists(art->dump_stem.wstring()+L".rgba") && std::filesystem::file_size(art->dump_stem.wstring()+L".rgba")==64*64*4,"art dump written");
            std::filesystem::remove(art->dump_stem.wstring()+L".rgba");std::filesystem::remove(art->dump_stem.wstring()+L".json");}
    }
    {   // v1043: the game's own lens is one-channel (BC4, sim v1042): its value is the coverage of black. Sampled as is it drew a red frame
        // and a black opening (the scene hidden); an R8 mask with the same strips must give black strips and keep the scene in the opening.
        must(spyglass_art::one_channel(DXGI_FORMAT_BC4_UNORM) && spyglass_art::one_channel(DXGI_FORMAT_R8_UNORM) &&
             !spyglass_art::one_channel(DXGI_FORMAT_R8G8B8A8_UNORM) && !spyglass_art::one_channel(DXGI_FORMAT_A8_UNORM),"one-channel formats");
        std::vector<std::uint8_t> r8(64*64);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) r8[static_cast<std::size_t>(y*64+x)]=(y<8 || x<8)?255:0;
        auto art=std::make_shared<LensArt>();
        D3D11_TEXTURE2D_DESC ad{};ad.Width=ad.Height=64;ad.MipLevels=ad.ArraySize=1;ad.SampleDesc.Count=1;ad.Usage=D3D11_USAGE_DEFAULT;ad.Format=DXGI_FORMAT_R8_UNORM;ad.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA ai{r8.data(),64,0};must(SUCCEEDED(device->CreateTexture2D(&ad,&ai,&art->texture)),"one-channel art texture");
        art->desc=ad;art->view.Format=ad.Format;art->view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;art->view.Texture2D.MipLevels=1;
        art->blend_enabled=true;art->src=D3D11_BLEND_SRC_ALPHA;art->dst=D3D11_BLEND_INV_SRC_ALPHA;art->op=D3D11_BLEND_OP_ADD;
        std::vector<std::uint8_t> white(static_cast<std::size_t>(w)*h*4,255);
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA init{white.data(),static_cast<UINT>(w*4),0};ComPtr<ID3D11Texture2D> eye,staging;
        must(SUCCEEDED(device->CreateTexture2D(&d,&init,&eye)),"eye");
        mask.draw(eye.Get(),fov,{0.f,0.f},art);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;must(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"staging");
        context->CopyResource(staging.Get(),eye.Get());D3D11_MAPPED_SUBRESOURCE map{};must(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"map");
        const auto ch=[&](int x,int y,int c) {return static_cast<int>(static_cast<const std::uint8_t*>(map.pData)[y*map.RowPitch+x*4+c]);};
        must(ch(cx,cy-half+8,0)==0 && ch(cx-half+8,cy,0)==0,"one-channel mask: the masked strips are black, not red");
        must(ch(cx,cy,0)==255 && ch(cx,cy,1)==255 && ch(cx,cy,2)==255,"one-channel mask: the opening keeps the scene");
        std::printf("art one-channel typeless: centre %d,%d,%d top-strip %d\n",ch(cx,cy,0),ch(cx,cy,1),ch(cx,cy,2),ch(cx,cy-half+8,0));
        context->Unmap(staging.Get(),0);
    }
}

int main() {
    using namespace spyglass_scope;
    // Zoom factor -> projection: the game's levels (0.4375 and 0.1875 of the angle on a 90 deg camera) give tangent / m on every edge.
    const double m1=spyglass::magnification(rad(90),rad(90*.4375)),m2=spyglass::magnification(rad(90),rad(90*.1875));
    auto n=narrowed(-static_cast<float>(rad(45)),static_cast<float>(rad(45)),static_cast<float>(rad(45)),-static_cast<float>(rad(45)),m1);
    must(close_to(std::tan(n[1]),1/m1,1e-6) && close_to(n[0],-n[1],1e-7) && close_to(n[2],-n[3],1e-7) && close_to(std::tan(n[2]),1/m1,1e-6),"narrowed symmetric");
    // The engine's own narrowed camera is the same frustum: the 90 deg camera at 0.4375 of the angle.
    must(close_to(n[1]*2,rad(90*.4375),1e-6),"narrowed equals the game's fov");
    n=narrowed(-static_cast<float>(rad(54)),static_cast<float>(rad(40)),static_cast<float>(rad(44)),-static_cast<float>(rad(55)),m2);   // an asymmetric eye keeps its shape: tangents all / m
    must(close_to(std::tan(n[0]),-std::tan(rad(54))/m2,1e-6) && close_to(std::tan(n[3]),-std::tan(rad(55))/m2,1e-6),"narrowed asymmetric");
    n=narrowed(-static_cast<float>(rad(45)),static_cast<float>(rad(45)),static_cast<float>(rad(45)),-static_cast<float>(rad(45)),1.0);must(n[1]==static_cast<float>(rad(45)),"m=1 is the unzoomed frustum");
    n=narrowed(-static_cast<float>(rad(45)),static_cast<float>(rad(45)),static_cast<float>(rad(45)),-static_cast<float>(rad(45)),100.);must(close_to(std::tan(n[1]),1/16.,1e-6),"zoom capped at 16");
    // Scope: half the smaller span across, 40-56 deg, from the eye's own FOV.
    must(close_to(std::atan(radius_tan(rad(-54),rad(40),rad(44),rad(-55)))*2*180/pi,.5*94,.01),"scope diameter, Quest-like eye");
    must(close_to(std::atan(radius_tan(rad(-30),rad(30),rad(30),rad(-30)))*2,rad(40),1e-9),"narrow headset clamps to 40");
    must(close_to(std::atan(radius_tan(rad(-80),rad(80),rad(80),rad(-80)))*2,rad(56),1e-9),"wide headset clamps to 56");
    must(close_to(std::atan(radius_tan(0,0,0,0)),rad(24),1e-9),"bad FOV falls back");
    // Rim: clear inside .8, .9 at the edge, black by 1.06, monotonic.
    must(coverage(0)==0 && coverage(.8)==0 && close_to(coverage(1.),.9,1e-6) && close_to(coverage(1.06),1.,1e-6) && coverage(2.)==1.f,"rim ends");
    float last=0;for(int i=0;i<=120;++i) {const float c=coverage(i/100.);must(c>=last,"rim monotonic");last=c;}
    // Scope centre of a canted view: the optical middle of the eye (yaw toward +x, pitch toward +y); identity -> 0.
    must(axis_tangent({0,0,0,1})[0]==0 && axis_tangent({0,0,0,1})[1]==0,"axis identity");
    const auto c=canted_fov(-static_cast<float>(rad(54)),static_cast<float>(rad(40)),static_cast<float>(rad(44)),-static_cast<float>(rad(55)));must(c.has_value(),"canted fov");
    const auto a=axis_tangent(c->rotation);
    must(close_to(a[0],std::tan(c->yaw),1e-4) && a[0]<0 && a[1]<0,"axis of a canted view (a left eye looks left and down)");
    const auto b=axis_tangent({0,static_cast<float>(std::sin(rad(10)/2)),0,static_cast<float>(std::cos(rad(10)/2))});   // +y rotation turns left in OpenXR
    must(close_to(b[0],-std::tan(rad(10)),1e-6) && close_to(b[1],0,1e-6),"axis sign");
    // State: entry and exit reset once each, hysteresis, fades reset only when asked, factor and flag follow.
    State s;
    auto st=s.update(1.0,false);must(!st.active && !st.reset && st.m==1,"idle");
    st=s.update(1.015,false);must(!st.active && !st.reset,"below on threshold");
    st=s.update(1.4,false);must(st.active && st.reset && close_to(st.m,1.4,1e-12),"entry resets");
    st=s.update(2.0,false);must(st.active && !st.reset && st.m==2.0,"fade without reset");
    st=s.update(2.5,true);must(st.active && st.reset,"fade resets when asked");
    st=s.update(2.5,true);must(st.active && !st.reset,"settled");
    st=s.update(1.015,false);must(st.active && !st.reset,"hysteresis keeps it on above 1.01");
    st=s.update(1.005,false);must(!st.active && st.reset && st.m==1,"exit resets");
    st=s.update(1.0,false);must(!st.active && !st.reset,"idle again");
    st=s.update(40.,false);must(st.active && st.m==16.,"clamped");
    st=s.update(std::nan(""),false);must(!st.active && st.reset && st.m==1,"nan exits");
    // The lens-art probe: schedule, choice, texture mapping.
    must(spyglass_art::block_of(20)==-1 && spyglass_art::block_of(21)==0 && spyglass_art::block_of(30)==0 && spyglass_art::block_of(31)==1 && spyglass_art::block_of(40)==1 && spyglass_art::block_of(41)==2 && spyglass_art::block_of(50)==2 && spyglass_art::block_of(spyglass_art::total_frames)==2 && spyglass_art::block_of(spyglass_art::total_frames+1)==-1,"probe blocks");
    must(spyglass_art::keeps_quad(21) && !spyglass_art::keeps_quad(35) && spyglass_art::keeps_quad(45) && !spyglass_art::keeps_quad(10) && !spyglass_art::keeps_quad(spyglass_art::total_frames+1),"keep schedule");
    must(spyglass_art::record_block(21)==-1 && spyglass_art::record_block(24)==0 && spyglass_art::record_block(34)==1 && spyglass_art::record_block(44)==2 && spyglass_art::record_block(41)==-1,"first frames of a block are skipped");
    {
        using C=std::array<unsigned,3>;
        must(!spyglass_art::choose({}) ,"nothing seen");
        must(spyglass_art::choose({C{20,20,20},C{7,0,7},C{0,7,0},C{7,1,6}})==std::optional<std::size_t>(1),"lens draw = only with the quad");
        must(!spyglass_art::choose({C{20,20,20},C{3,0,7},C{7,5,7}}),"too few or also without");
        must(spyglass_art::choose({C{5,0,5},C{7,0,7}})==std::optional<std::size_t>(1),"most draws wins");
        const auto c0=spyglass_art::texture_uv(0,0,1.);must(c0.inside && close_to(c0.u,.5,1e-12) && close_to(c0.v,.5,1e-12),"uv centre");
        const auto t=spyglass_art::texture_uv(1,1,1.);must(close_to(t.u,1,1e-12) && close_to(t.v,0,1e-12) && t.inside,"uv top right of the square");
        must(!spyglass_art::texture_uv(1.01,0,1.).inside && !spyglass_art::texture_uv(0,-1.01,1.).inside,"outside the square");
        const auto wide=spyglass_art::texture_uv(1,0,16./9);must(close_to(wide.u,.5+.5*9/16,1e-12),"wide texture keeps its aspect, height spans the diameter");
    }
    gpu_lab();art_lab();
    std::puts("spyglass_scope_check OK");
}
