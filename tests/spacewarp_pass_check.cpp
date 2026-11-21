// Lab check of src/spacewarp_pass.hpp on WARP: synthetic scenes with known answers (64 x 64, FOV tangents +-1, so one texel is 1/32 of
// a tangent; reverse-Z with m10 0, m11 1: distance = 1 / depth). Colour R = 4 x (a horizontal position marker), B = 255 on the object
// under test, so where things landed can be read back exactly.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/spacewarp_pass.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
static void require(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
constexpr unsigned N=64;

struct Scene {
    std::function<float(unsigned,unsigned)> distance=[](unsigned,unsigned) {return 2.f;};  // 0: sky
    std::function<bool(unsigned,unsigned)> object=[](unsigned,unsigned) {return false;};
    std::function<std::array<float,4>(unsigned,unsigned)> motion;                        // xyz metres, w px
    std::function<unsigned char(unsigned,unsigned)> rig;
    std::function<float(unsigned,unsigned)> prev_distance;                              // v1418: the previous real frame's depth (0: sky)
    std::function<unsigned char(unsigned,unsigned)> blue;                               // v1424: B instead of the object's 255 / 0 (soft edges)
};
struct Image {std::vector<unsigned char> rgba;unsigned char r(unsigned x,unsigned y) const {return rgba[(y*N+x)*4];} unsigned char b(unsigned x,unsigned y) const {return rgba[(y*N+x)*4+2];}};

static Image run(ID3D11Device* device,ID3D11DeviceContext* context,d2vr::SpacewarpPass& pass,const Scene& scene,d2vr::SpacewarpParams p) {
    const auto texture=[&](DXGI_FORMAT format,const void* data,unsigned bpp,UINT bind) {
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=N;d.MipLevels=d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA s{data,N*bpp,0};ComPtr<ID3D11Texture2D> t;require(SUCCEEDED(device->CreateTexture2D(&d,data?&s:nullptr,&t)),"texture");return t;
    };
    const auto srv=[&](ID3D11Texture2D* t) {ComPtr<ID3D11ShaderResourceView> v;require(SUCCEEDED(device->CreateShaderResourceView(t,nullptr,&v)),"SRV");return v;};
    std::vector<unsigned char> colour(N*N*4);std::vector<float> depth(N*N);std::vector<std::uint16_t> motion(N*N*4,0);std::vector<unsigned char> rig(N*N,0);
    const auto half=[](float f) {  // float -> half (normal range only, enough here)
        if(f==0) return std::uint16_t{0};
        std::uint32_t x;std::memcpy(&x,&f,4);const std::uint32_t sign=(x>>16)&0x8000u;const int e=static_cast<int>((x>>23)&0xff)-127+15;
        return static_cast<std::uint16_t>(sign|(static_cast<std::uint32_t>(e)<<10)|((x&0x7fffffu)>>13));
    };
    for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {
        const unsigned i=y*N+x;const float d=scene.distance(x,y);depth[i]=d>0?1/d:0;
        colour[i*4]=static_cast<unsigned char>(x*4);colour[i*4+1]=static_cast<unsigned char>(y*4);colour[i*4+2]=scene.blue?scene.blue(x,y):scene.object(x,y)?255:0;colour[i*4+3]=255;
        if(scene.motion) {const auto m=scene.motion(x,y);for(int c=0;c<4;++c) motion[i*4+c]=half(m[static_cast<std::size_t>(c)]);}
        if(scene.rig) rig[i]=scene.rig(x,y);
    }
    const auto ct=texture(DXGI_FORMAT_R8G8B8A8_UNORM,colour.data(),4,D3D11_BIND_SHADER_RESOURCE),dt=texture(DXGI_FORMAT_R32_FLOAT,depth.data(),4,D3D11_BIND_SHADER_RESOURCE);
    const auto mt=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,motion.data(),8,D3D11_BIND_SHADER_RESOURCE),rt=texture(DXGI_FORMAT_R8_UINT,rig.data(),1,D3D11_BIND_SHADER_RESOURCE);
    const auto out=texture(DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,4,D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(device->CreateRenderTargetView(out.Get(),nullptr,&rtv)),"RTV");
    const auto cs=srv(ct.Get()),ds=srv(dt.Get()),ms=srv(mt.Get()),rs=srv(rt.Get());
    d2vr::SpacewarpInputs in{cs.Get(),ds.Get(),scene.motion?ms.Get():nullptr,scene.rig?rs.Get():nullptr,N,N};
    std::vector<float> previous(N*N);ComPtr<ID3D11ShaderResourceView> ps;
    if(scene.prev_distance) {
        for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {const float d=scene.prev_distance(x,y);previous[y*N+x]=d>0?1/d:0;}
        const auto pt=texture(DXGI_FORMAT_R32_FLOAT,previous.data(),4,D3D11_BIND_SHADER_RESOURCE);ps=srv(pt.Get());
        in.prev_depth=ps.Get();in.prev_width=in.prev_height=N;
    }
    p.source_tan=p.target_tan={-1,1,1,-1};p.m10=0;p.m11=1;
    require(pass.run(context,in,p,rtv.Get(),N,N),"pass.run");
    D3D11_TEXTURE2D_DESC d{};out->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> rb;require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&rb)),"readback");context->CopyResource(rb.Get(),out.Get());
    D3D11_MAPPED_SUBRESOURCE m{};require(SUCCEEDED(context->Map(rb.Get(),0,D3D11_MAP_READ,0,&m)),"map");
    Image img;img.rgba.resize(N*N*4);for(unsigned y=0;y<N;++y) std::memcpy(&img.rgba[y*N*4],static_cast<const unsigned char*>(m.pData)+y*m.RowPitch,N*4);
    context->Unmap(rb.Get(),0);return img;
}
static int near_level(int a,int b,int tolerance) {return std::abs(a-b)<=tolerance;}

int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        d2vr::SpacewarpPass pass(device.Get());
        int failures=0;const auto check=[&](bool ok,const std::string& what) {std::printf("%s %s\n",ok?"ok  ":"FAIL",what.c_str());failures+=!ok;};
        {   // 1. identity: the image comes back unchanged
            const auto img=run(device.Get(),context.Get(),pass,Scene{},{});
            int worst=0;for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) worst=std::max(worst,std::abs(img.r(x,y)-static_cast<int>(x*4)));
            check(worst<=1,"identity: R off by "+std::to_string(worst)+" at most");
        }
        {   // 2. the camera 0.5 m right of a plane 2 m ahead: the image moves 0.25 tangent = 8 texels left
            d2vr::SpacewarpParams p{};p.c={.5,0,0};const auto img=run(device.Get(),context.Get(),pass,Scene{},p);
            int worst=0;for(unsigned y=4;y<N-4;++y) for(unsigned x=0;x+8<N;++x) worst=std::max(worst,std::abs(img.r(x,y)-static_cast<int>((x+8)*4)));
            check(worst<=2,"translation parallax: R off by "+std::to_string(worst)+" at most (want the source 8 texels right)");
        }
        {   // 3. a near band (1 m, columns 24..39) over a far wall (4 m), camera 0.25 m right: band 8 texels left, wall 2; the strip the band
            // uncovers shows the wall, not the band
            Scene s;s.distance=[](unsigned x,unsigned) {return x>=24 && x<40?1.f:4.f;};s.object=[](unsigned x,unsigned) {return x>=24 && x<40;};
            d2vr::SpacewarpParams p{};p.c={.25,0,0};const auto img=run(device.Get(),context.Get(),pass,s,p);
            unsigned band=0;bool uncovered_ok=true;
            for(unsigned x=0;x<N;++x) band+=img.b(x,32)>128;
            for(unsigned x=33;x<38;++x) uncovered_ok=uncovered_ok && img.b(x,32)<64;
            check(img.b(17,32)>128 && img.b(30,32)>128 && img.b(14,32)<64,"near band moved 8 texels left (B at 17 and 30, not at 14)");
            check(uncovered_ok,"uncovered strip 33..37 shows the wall, not the band");
            check(band>=15 && band<=17,"band keeps its width ("+std::to_string(band)+" texels)");
            check(near_level(img.r(50,32),(52)*4,6),"wall moved 2 texels left (R at 50: "+std::to_string(img.r(50,32))+", want ~208)");
        }
        {   // 4. a mover (rows/cols 20..39, w 10 px, 0.25 m right in the world) on a still plane 2 m ahead, camera still: 4 texels right
            Scene s;const auto in=[](unsigned x,unsigned y) {return x>=20 && x<40 && y>=20 && y<40;};s.object=in;
            s.motion=[in](unsigned x,unsigned y) {return in(x,y)?std::array<float,4>{.25f,0,0,10}:std::array<float,4>{0,0,0,0};};
            d2vr::SpacewarpParams p{};p.motion_share=1;p.mover_cap=0;const auto img=run(device.Get(),context.Get(),pass,s,p);  // v1426: uncapped
            // (the strip it vacates, 20..23, is stretched from both sides: same depth; the cell next to the wall takes the wall)
            check(img.b(42,30)>128 && img.b(25,30)>128 && img.b(21,30)<64,"mover moved 4 texels right (B at 42 and 25, not at 21)");
            d2vr::SpacewarpParams still{};const auto img0=run(device.Get(),context.Get(),pass,s,still);
            check(img0.b(21,30)>128 && img0.b(41,30)<64,"motion_share 0 leaves the mover in place");
            d2vr::SpacewarpParams capped{};capped.motion_share=1;const auto img1=run(device.Get(),context.Get(),pass,s,capped);  // v1426: the default 3 cm cap
            check(img1.b(21,30)>128 && img1.b(41,30)<64,"v1426 mover cap: a 0.25 m step is cut to 3 cm (less than a texel here): no overshoot");
        }
        {   // 5. a drawn hand (rig 255, 1 m ahead, centred at texel 30) moved 0.125 m right by its rigid motion: 4 texels right; the rest of
            // the rig (the same mask beyond the palm's reach) follows the rest motion (here: none)
            Scene s;const auto hand=[](unsigned x,unsigned y) {return x>=26 && x<34 && y>=26 && y<34;};
            s.distance=[hand](unsigned x,unsigned y) {return hand(x,y)?1.f:3.f;};s.object=hand;s.rig=[hand](unsigned x,unsigned y) {return static_cast<unsigned char>(hand(x,y)?255:0);};
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.3};  // texel 30 at 1 m: tangent (30/64)*2-1 in x, its mirror in y
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(36,30)>128 && img.b(30,30)>128 && img.b(27,30)<64,"hand moved 4 texels right (B at 36 and 30, not at 27)");
            p.palm[0][3]=0;const auto img2=run(device.Get(),context.Get(),pass,s,p);  // no palm owns it: the rest rule (off) keeps it
            check(img2.b(27,30)>128 && img2.b(35,30)<64,"rig outside every palm's reach stays (rest motion off)");
        }
        {   // 7. true stereo has no rig mask: the same hand found by its palm's reach (rig_near on); the wall (3 m) is beyond rig_near
            Scene s;const auto hand=[](unsigned x,unsigned y) {return x>=26 && x<34 && y>=26 && y<34;};
            s.distance=[hand](unsigned x,unsigned y) {return hand(x,y)?1.f:3.f;};s.object=hand;
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;p.rest_on=true;p.rest.t={-.3,0,0};
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.3};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(36,30)>128 && img.b(27,30)<64,"no mask: the hand within its palm's reach moved 4 texels right");
            check(near_level(img.r(10,30),10*4,2),"no mask: the wall beyond rig_near stayed (the rest motion did not move it)");
        }
        {   // 8. a canted render (the headset default, game_xr.hpp v808): depth from a camera turned 10 degrees, colour from the straight
            // camera (the submitted image), target = the straight camera, nothing moves: the output must be the straight image itself.
            // Without the colour camera the generated frame is turned and stretched by the cant (headset 2026-11-19: doubled images).
            const double a=10*3.14159265358979/180,ca=std::cos(a),sa=std::sin(a);
            Scene s;  // a wall 2 m ahead of the straight camera, seen from the canted one: distance along each canted ray
            s.distance=[=](unsigned x,unsigned) {
                const double tx=-1+2*(x+.5)/N;  // canted ray (tx, ty, -1); the canted axes in straight coordinates: yaw a
                const double z=-sa*tx-ca;       // straight z of R * ray (a yaw leaves ty out of it)
                return static_cast<float>(2/-z);
            };
            d2vr::SpacewarpParams p{};
            p.m={ca,0,-sa,0,1,0,sa,0,ca};  // the straight camera's axes in canted coordinates: R^T
            p.colour_camera=true;p.colour_m=p.m;p.colour_tan={-1,1,1,-1};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            // (the straight view right of tangent 0.70, columns 54..63, lies outside the canted frustum: filled, not checked)
            int worst=0;for(unsigned y=8;y<N-8;++y) for(unsigned x=8;x<52;++x) worst=std::max(worst,std::abs(img.r(x,y)-static_cast<int>(x*4)));
            check(worst<=3,"canted depth + straight colour camera, no motion: the straight image comes back (R off by "+std::to_string(worst)+" at most)");
            d2vr::SpacewarpParams wrong=p;wrong.colour_camera=false;  // the v1416 mistake: colour read as if it were the canted camera's
            const auto bad=run(device.Get(),context.Get(),pass,s,wrong);
            int off=0;for(unsigned y=8;y<N-8;++y) for(unsigned x=8;x<N-8;++x) off=std::max(off,std::abs(bad.r(x,y)-static_cast<int>(x*4)));
            check(off>=12,"without the colour camera the same frame is visibly shifted (R off by "+std::to_string(off)+", the bug this guards)");
        }
        {   // 9. v1418, true stereo: a held thing's far part. Palm at texel 30 (1 m), its reach 0.22 m; the hand moved 0.125 m right since the
            // previous real frame and moves on 0.125 m (4 texels) to the target. A blade (columns 38..41, 0.27-0.36 m from the palm) was 4
            // texels left in the previous frame: it moved with the hand and follows it. A shelf (14..19, 0.33-0.48 m from the palm) is where it
            // was: it stays. Something behind the grip (the forearm side: 0.8 m away, 0.2 m nearer than the grip) moved with the hand too, but
            // only what is ahead of the grip can be held: it stays. The wall is 3 m away.
            const auto box=[](unsigned x,unsigned y,unsigned x0,unsigned x1,unsigned y0,unsigned y1) {return x>=x0 && x<=x1 && y>=y0 && y<=y1;};
            const auto now=[box](unsigned x,unsigned y,int shift) {  // shift: texels the moving things were left of their place now
                const int sx=static_cast<int>(x)+shift;const unsigned ux=sx<0?999u:static_cast<unsigned>(sx);
                if(box(ux,y,26,33,26,33) || box(ux,y,38,41,28,35) || box(x,y,14,19,28,35)) return 1.f;
                if(box(static_cast<unsigned>(std::max(0,static_cast<int>(x)+(shift?5:0))),y,34,37,40,43)) return .8f;
                return 3.f;
            };
            Scene s;s.distance=[now](unsigned x,unsigned y) {return now(x,y,0);};
            s.object=[box](unsigned x,unsigned y) {return box(x,y,26,33,26,33) || box(x,y,38,41,28,35) || box(x,y,14,19,28,35) || box(x,y,34,37,40,43);};
            s.prev_distance=[now](unsigned x,unsigned y) {return now(x,y,4);};
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.22};
            p.prev_on=true;p.prev_tan={-1,1,1,-1};p.prev_m10=0;p.prev_m11=1;p.prev_hand_on={true,false};p.prev_hand[0].t={-.125,0,0};
            p.grip[0]={0,0,-1,1};  // the grip's forward axis straight ahead, its origin 1 m away: forward = -z - 1
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(43,31)>128 && img.b(45,31)>128 && img.b(39,31)<64,"held far part: the blade past the palm's reach moved 4 texels right with the hand");
            check(img.b(16,31)>128 && img.b(14,31)>128 && img.b(21,31)<64,"held far part: the shelf beside the hand, confirmed still, stayed");
            check(img.b(35,41)>128 && img.b(40,41)<64,"held far part: what lies behind the grip (the forearm side) stayed");
            check(img.b(34,30)>128 && img.b(27,30)<64,"held far part: the hand within its palm's reach moved as before");
            d2vr::SpacewarpParams off=p;off.prev_on=false;const auto img0=run(device.Get(),context.Get(),pass,s,off);
            check(img0.b(39,31)>128 && img0.b(43,31)<64,"without the previous frame the blade stays (the v1417 rule)");
            Scene still=s;still.prev_distance=[now](unsigned x,unsigned y) {return now(x,y,0);};  // the hand held still last step: nothing to tell
            const auto img1=run(device.Get(),context.Get(),pass,still,p);
            check(img1.b(39,31)>128,"a blade the previous frame shows still is not taken (still and moved look alike)");
        }
        {   // 10. v1419: a weapon body. A blade (columns 50..53, 0.65-0.75 m from the palm: past every reach) lies on the hand's weapon
            // segment; with no previous frame at all it still follows the hand like the hand itself. A thing beside it (rows 40..43, off the
            // segment) stays.
            const auto box=[](unsigned x,unsigned y,unsigned x0,unsigned x1,unsigned y0,unsigned y1) {return x>=x0 && x<=x1 && y>=y0 && y<=y1;};
            Scene s;
            s.distance=[box](unsigned x,unsigned y) {return box(x,y,26,33,26,33) || box(x,y,50,53,30,33) || box(x,y,50,53,40,43)?1.f:3.f;};
            s.object=[box](unsigned x,unsigned y) {return box(x,y,26,33,26,33) || box(x,y,50,53,30,33) || box(x,y,50,53,40,43);};
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.22};
            const double tx=(51.5/N)*2-1,ty=1-(31.5/N)*2;   // the blade's centre ray at 1 m
            p.weapon[0]={centre,-centre,-1,tx+.06,ty,-1,.05};  // from the palm out to just past the blade, 5 cm around
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(55,31)>128 && img.b(51,31)<64,"weapon body: the blade 0.7 m out moved 4 texels right with the hand, no previous frame needed");
            check(img.b(51,41)>128 && img.b(56,41)<64,"weapon body: the thing beside the blade, off the segment, stayed");
            d2vr::SpacewarpParams none=p;none.weapon={};const auto img0=run(device.Get(),context.Get(),pass,s,none);
            check(img0.b(51,31)>128 && img0.b(55,31)<64,"without the weapon body the same blade stays (the v1418 rule has no previous frame here)");
        }
        {   // 11. v1423: what the player carries. Since the previous real frame the play space moved: the world shifted 0.125 m (4 texels at
            // 1 m) in this camera, a carried box (columns 40..43, 1 m: past rig_near, no palm) did not. The rest of the rig moves on 0.125 m.
            // The box follows the rest motion; a shelf at the same distance (14..19), confirmed still in the world, stays.
            const auto box=[](unsigned x,unsigned y,unsigned x0,unsigned x1,unsigned y0,unsigned y1) {return x>=x0 && x<=x1 && y>=y0 && y<=y1;};
            Scene s;
            s.distance=[box](unsigned x,unsigned y) {return box(x,y,40,43,28,35) || box(x,y,14,19,28,35)?1.f:3.f;};
            s.object=[box](unsigned x,unsigned y) {return box(x,y,40,43,28,35) || box(x,y,14,19,28,35);};
            s.prev_distance=[box](unsigned x,unsigned y) {return box(x,y,40,43,28,35) || box(x,y,18,23,28,35)?1.f:3.f;};
            d2vr::SpacewarpParams p{};p.rig_near=.5;p.rest_on=true;p.rest.t={.125,0,0};
            p.prev_on=true;p.prev_tan={-1,1,1,-1};p.prev_m10=0;p.prev_m11=1;p.prev_world.t={.125,0,0};p.carry_on=true;  // prev_rest: identity
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(45,31)>128 && img.b(41,31)<64,"carried: the box moved with the player since the last frame follows the rest motion (4 texels right)");
            check(img.b(16,31)>128 && img.b(21,31)<64,"carried: the shelf beside it, still in the world, stayed");
            d2vr::SpacewarpParams off=p;off.carry_on=false;const auto img0=run(device.Get(),context.Get(),pass,s,off);
            check(img0.b(41,31)>128 && img0.b(45,31)<64,"without the carried rule the box stays (v1422)");
        }
        {   // 12. v1424 (user, headset v1423: "around the hands, weapons, carried bodies and the player's own body a silhouette frame, a lot of
            // ghosting"): soft edges. The colour image's edges are soft (anti-aliasing, the upscaler, the depth's own jitter) while the depth's are
            // hard: the wall texels next to the hand carry hand colour (B 128, columns 24..25 and 34..35) and the hand's own edge texels carry
            // wall colour (B 128, columns 26 and 33). The hand (palm reach, true stereo) moves 4 texels right.
            const auto hand=[](unsigned x,unsigned y) {return x>=26 && x<34 && y>=26 && y<34;};
            const auto ring=[](unsigned x,unsigned y,unsigned x0,unsigned x1) {return x>=x0 && x<=x1 && y>=x0 && y<=x1;};
            Scene s;s.distance=[hand](unsigned x,unsigned y) {return hand(x,y)?1.f:3.f;};s.object=hand;
            s.blue=[hand,ring](unsigned x,unsigned y)->unsigned char {
                if(hand(x,y)) return ring(x,y,27,32)?255:128;   // the hand's edge texels: half wall
                return ring(x,y,24,35)?128:0;                    // the wall's rim: half hand
            };
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.3};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            int ghost=0;for(unsigned x=18;x<30;++x) ghost=std::max(ghost,static_cast<int>(img.b(x,30)));
            check(ghost<40,"soft edges: where the hand was (and its rim) shows the wall, no silhouette (B up to "+std::to_string(ghost)+")");
            check(img.b(30,30)>=200 && img.b(37,30)>=200,"soft edges: the moved hand's edges show the hand, not the wall it left (B "+
                std::to_string(img.b(30,30))+", "+std::to_string(img.b(37,30))+")");
            check(img.b(34,30)>=200 && img.b(40,30)<140,"soft edges: the hand moved 4 texels right");
            d2vr::SpacewarpParams still{};still.rig_near=.5;const auto img0=run(device.Get(),context.Get(),pass,s,still);
            int off=0;for(unsigned x=0;x<N;++x) off=std::max(off,std::abs(static_cast<int>(img0.b(x,30))-static_cast<int>(s.blue(x,30))));
            check(off<=2,"soft edges: nothing moving, the soft edges come back untouched (B off by "+std::to_string(off)+")");
            d2vr::SpacewarpParams legacy=p;legacy.soft_edges=false;const auto old=run(device.Get(),context.Get(),pass,s,legacy);
            int was=0;for(unsigned x=18;x<30;++x) was=std::max(was,static_cast<int>(old.b(x,30)));
            check(was>=100 && old.b(30,30)<200,"soft edges off (build/spacewarp-legacy-edges): the v1423 silhouette and halo come back (B "+std::to_string(was)+")");
        }
        {   // 14. v1424: the shards. The wall behind the hand is striped (B 0 / 80 in 2-texel columns); the hand (B 255) moves 4 texels right.
            // The strip it uncovers was one seed's colour copied along each fill direction (a streak of 0 or of 80: the shards); now the
            // background around it blended (between the stripes).
            const auto hand=[](unsigned x,unsigned y) {return x>=26 && x<34 && y>=26 && y<34;};
            Scene s;s.distance=[hand](unsigned x,unsigned y) {return hand(x,y)?1.f:3.f;};s.object=hand;
            s.blue=[hand](unsigned x,unsigned y)->unsigned char {return hand(x,y)?255:(x/2)%2?80:0;};
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.3};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            d2vr::SpacewarpParams legacy=p;legacy.soft_edges=false;const auto old=run(device.Get(),context.Get(),pass,s,legacy);
            // against the wall the hand hid (the stripes run on vertically behind it)
            double now_err=0,old_err=0;int hand_left=0;
            for(unsigned y=27;y<33;++y) for(unsigned x=26;x<30;++x) {
                const int truth=(x/2)%2?80:0;now_err+=std::abs(img.b(x,y)-truth);old_err+=std::abs(old.b(x,y)-truth);hand_left+=img.b(x,y)>128;
            }
            now_err/=24;old_err/=24;
            check(hand_left==0 && now_err<old_err*.95 && now_err<38,"uncovered strip (v1425: blended, then softened): the hidden striped wall, not the hand nor one seed's streak (off the truth by "+
                std::to_string(static_cast<int>(now_err))+" levels; one seed's streak, soft edges off: "+std::to_string(static_cast<int>(old_err))+")");
        }
        {   // 15. v1425: cracks on a close, steep surface that comes nearer. A hand-held surface (columns 16..47, rows 16..47) slopes from 0.30 m
            // (left) to 0.45 m (right) in front of a wall at 3 m and moves 4 cm toward the camera: it magnifies by 9-15 %, so whole columns
            // of cells get no texel. Neighbours two cells apart differ by up to 7 % in depth: past the crack rule's 10 % only at the steep
            // right... the surface must stay closed (no wall seen through it) except at its outline.
            const auto in_hand=[](unsigned x,unsigned y) {return x>=16 && x<48 && y>=16 && y<48;};
            Scene s;s.distance=[in_hand](unsigned x,unsigned y) {return in_hand(x,y)?.25f+.35f*(static_cast<float>(x)-16)/31.f:3.f;};
            s.object=in_hand;
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={0,0,.04};p.rig_near=.5;
            p.palm[0]={0,0,-.37,.4};  // the palm in the middle, its reach covering the whole surface
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            int holes=0;for(unsigned y=20;y<44;++y) for(unsigned x=20;x<44;++x) holes+=img.b(x,y)<128;
            check(holes==0,"close steep surface coming nearer: no wall seen through it ("+std::to_string(holes)+" cells)");
        }
        {   // 16. v1425: one surface, two motions. An arm (rows 28..35, columns 8..55, 0.45 m: inside rig_near) over a wall at 3 m. Its hand
            // end (within 0.1 m of the palm at column 48) moves 4 texels right with the hand; the rest of it with the rest of the rig (still).
            // A per-texel rule tears the arm at the palm's reach: the wall shows through as a line. The arm must stay closed (it stretches).
            const auto arm=[](unsigned x,unsigned y) {return x>=8 && x<56 && y>=28 && y<36;};
            Scene s;s.distance=[arm](unsigned x,unsigned y) {return arm(x,y)?.45f:3.f;};s.object=arm;
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.rig_near=.5;p.rest_on=true;
            const double tx=(48.5/N)*2-1,ty=1-(31.5/N)*2;p.palm[0]={tx*.45,ty*.45,-.45,.1};
            p.hand[0].t={4*.45/32,0,0};  // 4 texels at 0.45 m
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            int torn=0;for(unsigned y=29;y<35;++y) for(unsigned x=10;x<56;++x) torn+=img.b(x,y)<128;
            check(torn==0,"one surface, two motions: the arm stays closed where its hand end moves ("+std::to_string(torn)+" cells of wall seen through it)");
            check(img.b(57,31)>128 && img.b(9,31)>128,"one surface, two motions: the hand end moved 4 texels right, the far end stayed");
        }
        {   // 17. v1429: sharpness. Fine detail (B a 4-texel sine, amplitude 100) on a wall, the camera turned half a texel: bilinear lowers the
            // detail's contrast (to half at this phase) in every generated frame, Catmull-Rom keeps ~25 % more within the 2x2 clamp (45 Hz shimmer)
            Scene s;s.blue=[](unsigned x,unsigned) {return static_cast<unsigned char>(128+100*std::sin(2*3.14159265358979*x/4));};  // crests on texels
            const double a=std::atan(1.0/N);d2vr::SpacewarpParams p{};p.m={std::cos(a),0,std::sin(a),0,1,0,-std::sin(a),0,std::cos(a)};
            const auto amp=[&](const Image& img) {int lo=255,hi=0;for(unsigned x=16;x<48;++x) {lo=std::min(lo,static_cast<int>(img.b(x,32)));hi=std::max(hi,static_cast<int>(img.b(x,32)));}return hi-lo;};
            const auto sharp=run(device.Get(),context.Get(),pass,s,p);
            d2vr::SpacewarpParams soft=p;soft.sharp_resample=false;const auto blur=run(device.Get(),context.Get(),pass,s,soft);
            check(amp(sharp)>=amp(blur)+15 && amp(sharp)<=200,"sharp resample: fine detail half a texel off keeps more contrast ("+std::to_string(amp(sharp))+" of 200; bilinear "+std::to_string(amp(blur))+"), never past the source");
        }
        {   // 18. v1432 (user, headset v1431: "player body (especially legs) still has a lot of ghosting"): the player's own body. Looking
            // straight down (up = +z in the camera), a leg (columns 28..35, rows 20..43, 1 m below the eye) over the floor (1.7 m) moved
            // 0.15 m right in the world since the last frame (the walk 0.10 m plus its swing); the play space's step to the slot is 0.10 m.
            // Body rule: 0.10 + (0.15 - 0.10) = 0.15 m = 4.8 texels right. Without it the leg is a mover capped at 3 cm: about 1 texel.
            const auto leg=[](unsigned x,unsigned y) {return x>=28 && x<=35 && y>=20 && y<=43;};
            Scene s;s.distance=[leg](unsigned x,unsigned y) {return leg(x,y)?1.f:1.7f;};s.object=leg;
            s.motion=[leg](unsigned x,unsigned y) {return leg(x,y)?std::array<float,4>{.15f,0,0,10}:std::array<float,4>{0,0,0,0};};
            d2vr::SpacewarpParams p{};p.motion_share=1;p.rest_on=true;p.rest.t={.10,0,0};p.body_on=true;p.body_up={0,0,1};
            p.body_axis={0,0,-1.7};p.body_radius=.5;p.body_top=1.4;  // the root on the floor straight below the eye
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(39,30)>128 && img.b(29,30)<64,"body: the walking leg moves with the walk and its swing (5 texels right)");
            d2vr::SpacewarpParams off=p;off.body_on=false;const auto img0=run(device.Get(),context.Get(),pass,s,off);
            check(img0.b(29,30)>128 && img0.b(39,30)<64,"without the body rule the leg is a capped mover and stays behind (v1431)");
            Scene wild=s;wild.motion=[leg](unsigned x,unsigned y) {return leg(x,y)?std::array<float,4>{2.f,0,0,10}:std::array<float,4>{0,0,0,0};};
            const auto img1=run(device.Get(),context.Get(),pass,wild,p);
            check(img1.b(29,30)>128 && img1.b(39,30)<64,"body: a 2 m step (a sword swing, a wild vector) is not the body's: a mover capped at 3 cm");
            // no motion vectors (the engine draws the player's body without them); the previous real frame shows the leg where the play
            // space's step put it (prev_rest identity) and the world 0.10 m further right (prev_world)
            Scene floor_only=s;floor_only.motion=[](unsigned,unsigned) {return std::array<float,4>{0,0,0,0};};
            floor_only.prev_distance=[leg](unsigned x,unsigned y) {return leg(x,y)?1.f:1.7f;};
            d2vr::SpacewarpParams pp=p;pp.prev_on=true;pp.prev_tan={-1,1,1,-1};pp.prev_m10=0;pp.prev_m11=1;pp.prev_world.t={.10,0,0};pp.carry_on=true;
            const auto img2=run(device.Get(),context.Get(),pass,floor_only,pp);
            check(img2.b(31,30)>128 && img2.b(29,30)<64,"body: a leg without a motion vector (the engine's player body) moves with the play space (3 texels)");
            check(img2.r(12,30)==12*4,"body: the floor (on the root's plane) stays still");
            // v1438 (user, headset v1437: "fix legs warping"): the mod's drawn leg (hip, knee, ankle down the leg's centre line, 1 m below the
            // eye) stepped 0.15 m right since the previous real frame (the walk 0.10 m plus its swing): the leg takes that step, not just the
            // play space's (5 texels, as with a motion vector); the other leg is far away
            // v1439: four points per leg (hip, knee, ankle, toe; w the capsule radius to the next point) and each point's past step (the
            // previous frame shows the leg where the play space's 0.10 m step put it)
            d2vr::SpacewarpParams pl=pp;pl.legs_on=true;
            pl.leg_point[0]={0,.4,-1,.13};pl.leg_point[1]={0,0,-1,.13};pl.leg_point[2]={0,-.4,-1,.13};pl.leg_point[3]={0,-.45,-1,0};
            for(unsigned j=0;j<4;++j) {pl.leg_step[j]={.15,0,0};pl.leg_past[j]={.10,0,0};pl.leg_point[4+j]={5,5,-1,.01};pl.leg_step[4+j]={0,0,0};pl.leg_past[4+j]={0,0,0};}
            const auto img4=run(device.Get(),context.Get(),pass,floor_only,pl);
            check(img4.b(39,30)>128 && img4.b(29,30)<64,"legs: a drawn leg without a motion vector moves with its own step (5 texels)");
            check(img4.r(12,30)==12*4,"legs: the floor stays still");
            {   // v1439: a running stride leaves the body cylinder: the leg rule still holds the leg (the cylinder shrunk to 5 cm here)
                d2vr::SpacewarpParams narrow=pl;narrow.body_radius=.05;const auto img6=run(device.Get(),context.Get(),pass,floor_only,narrow);
                check(img6.b(39,30)>128 && img6.b(29,30)<64,"legs: a leg outside the body cylinder still moves with its own step");
            }
            {   // v1439: the root 0.15 m below the floor (a slope, stairs): with the leg capsules the cylinder's floor is 0.25 m (game_xr), so
                // the floor beside the leg stays; with v1438's 0.06 m it slid with the play space
                d2vr::SpacewarpParams slope=pl;slope.body_axis={0,0,-1.85};slope.body_floor=.25;
                const auto img7=run(device.Get(),context.Get(),pass,floor_only,slope);
                check(img7.r(25,30)==25*4 && img7.b(39,30)>128,"legs: the floor beside the leg 0.15 m above the root stays (cylinder floor 0.25 m) and the leg still moves");
                d2vr::SpacewarpParams low=slope;low.body_floor=.06;low.legs_on=false;const auto img8=run(device.Get(),context.Get(),pass,floor_only,low);
                check(img8.r(25,30)!=25*4,"legs off, cylinder floor 0.06 m: that floor slides with the play space (v1438)");
            }
            for(unsigned j=0;j<4;++j) pl.leg_step[j]={0,0,0};  // a planted foot: no step in the world
            const auto img5=run(device.Get(),context.Get(),pass,floor_only,pl);
            check(img5.b(29,30)>128 && img5.b(37,30)<64,"legs: a leg that did not move in the world stays while the play space moves on (leg cap 0.15 m)");
            Scene no_prev=floor_only;no_prev.prev_distance=nullptr;const auto img2b=run(device.Get(),context.Get(),pass,no_prev,p);
            check(img2b.b(29,30)>128,"body: without a previous real frame a texel without a motion vector is left alone (scenery cannot be told apart)");
            d2vr::SpacewarpParams level=p;level.body_up={0,1,0};const auto img3=run(device.Get(),context.Get(),pass,s,level);
            check(img3.b(29,30)>128,"body: looking level the same texels are 1 m ahead, outside the cylinder: a capped mover again");
        }
        {   // 19. v1438 (user, headset v1437: "a warp area around the hands, like a bubble, when hands hover over some object"): a hand (columns
            // 26..33, rows 26..33, 1 m, palm at texel 30) moved 4 texels right since the previous real frame and moves on 4 to the target. A
            // table edge (columns 27..30, rows 34..35, 1 m: 0.13-0.18 m from the palm, inside its 0.22 m reach but past the inner 0.12 m) stood
            // still. v1437 moved it with the hand; now the previous frame confirms it still, so it stays, and the hand still moves.
            const auto box=[](unsigned x,unsigned y,unsigned x0,unsigned x1,unsigned y0,unsigned y1) {return x>=x0 && x<=x1 && y>=y0 && y<=y1;};
            const auto at=[box](unsigned x,unsigned y,int shift) {
                const int sx=static_cast<int>(x)+shift;const unsigned ux=sx<0?999u:static_cast<unsigned>(sx);
                return box(ux,y,26,33,26,33) || box(x,y,27,30,34,35)?1.f:3.f;
            };
            Scene s;s.distance=[at](unsigned x,unsigned y) {return at(x,y,0);};
            s.object=[box](unsigned x,unsigned y) {return box(x,y,26,33,26,33) || box(x,y,27,30,34,35);};
            s.prev_distance=[at](unsigned x,unsigned y) {return at(x,y,4);};
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.22};
            p.prev_on=true;p.prev_tan={-1,1,1,-1};p.prev_m10=0;p.prev_m11=1;p.prev_hand_on={true,false};p.prev_hand[0].t={-.125,0,0};
            p.grip[0]={0,0,-1,1};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(28,35)>128 && img.b(33,35)<64,"hand bubble: the table edge inside the palm's outer shell, confirmed still, stayed");
            check(img.b(35,30)>128 && img.b(27,30)<64,"hand bubble: the hand itself still moved 4 texels right");
            d2vr::SpacewarpParams old=p;old.hand_scenery=false;const auto img0=run(device.Get(),context.Get(),pass,s,old);
            check(img0.b(33,35)>128,"hand bubble off (v1437): the table edge moved with the hand");
        }
        {   // 20. v1460 (user: "fix the player's body and legs warp in spacewarp mode, do the same as for the hands"): legs_local. Looking straight
            // down as in test 18, the play space's step to the slot is 0.10 m; the previous real frame shows each thing where the play space's
            // step put it. A leg point's step is its own motion beyond that step: the pass draws it at rest(P + step).
            const auto leg=[](unsigned x,unsigned y) {return x>=28 && x<=35 && y>=20 && y<=43;};
            Scene s;s.distance=[leg](unsigned x,unsigned y) {return leg(x,y)?1.f:1.7f;};s.object=leg;s.prev_distance=s.distance;
            d2vr::SpacewarpParams p{};p.rest_on=true;p.rest.t={.10,0,0};p.body_on=true;p.body_up={0,0,1};p.body_axis={0,0,-1.7};p.body_radius=.5;p.body_top=1.4;
            p.prev_on=true;p.prev_tan={-1,1,1,-1};p.prev_m10=0;p.prev_m11=1;p.prev_world.t={.10,0,0};p.carry_on=true;
            p.legs_on=true;p.legs_local=true;p.body_floor=.25;
            p.leg_point[0]={0,.4,-1,.13};p.leg_point[1]={0,0,-1,.13};p.leg_point[2]={0,-.4,-1,.13};p.leg_point[3]={0,-.45,-1,0};
            for(unsigned j=0;j<4;++j) {p.leg_step[j]={.05,0,0};p.leg_past[j]={.10,0,0};p.leg_point[4+j]={5,5,-1,.01};}
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(39,30)>128 && img.b(29,30)<64,"legs_local: the leg's own 0.05 m on the play space's 0.10 m (5 texels right)");
            check(img.r(12,30)==12*4,"legs_local: the floor stays still");
            d2vr::SpacewarpParams planted=p;for(unsigned j=0;j<4;++j) planted.leg_step[j]={-.10,0,0};  // spacewarp_leg_own's planted foot: rest^-1(P) - P
            const auto img1=run(device.Get(),context.Get(),pass,s,planted);
            check(img1.b(29,30)>128 && img1.b(37,30)<64,"legs_local: a planted leg (own step = minus the play space's) stays in the world");
            // a foot 0.10 m above the root (1.6 m away; one 0.25 m capsule from (0, .4) to (0, -.4)): v1439's leg rule took texels only above the
            // cylinder's 0.25 m floor, so the foot stood still in every generated frame. legs_local: above legs_floor (1.5 cm)
            const auto foot=[](unsigned x,unsigned y) {return x>=30 && x<=33 && y>=20 && y<=43;};
            Scene f;f.distance=[foot](unsigned x,unsigned y) {return foot(x,y)?1.6f:1.7f;};f.object=foot;f.prev_distance=f.distance;
            d2vr::SpacewarpParams pf=p;for(unsigned j=0;j<4;++j) {pf.leg_point[j]={0,.4-.8*std::min(j,2u)/2.0,-1.6,j<3?.4:0};pf.leg_step[j]={.05,0,0};}
            const auto img2=run(device.Get(),context.Get(),pass,f,pf);
            check(img2.b(34,30)>128 && img2.b(31,30)<64,"legs_local: a foot 0.10 m above the root moves with its step (3 texels right)");
            // (column 38: 0.35 m beside the foot's line, 0.10 m under it, inside its 0.4 m capsule; 5 texels from the foot, no rim)
            check(img2.r(38,30)==38*4,"legs_local: the floor inside the foot's capsule (below legs_floor) stays");
            d2vr::SpacewarpParams nofloor=pf;nofloor.legs_floor=-1;const auto img3=run(device.Get(),context.Get(),pass,f,nofloor);
            check(img3.r(38,30)<38*4-4,"legs_local with no leg floor: the same floor moves with the foot (R "+std::to_string(img3.r(38,30))+"; the floor rule is what keeps it)");
            d2vr::SpacewarpParams old=pf;old.legs_local=false;for(unsigned j=0;j<4;++j) old.leg_step[j]={.15,0,0};  // v1439: the world step
            const auto img4=run(device.Get(),context.Get(),pass,f,old);
            check(img4.b(31,30)>128 && img4.b(34,30)<64,"v1439 rule: the same foot below the cylinder's 0.25 m floor stays behind (the bug)");
        }
        {   // 21. v1460: the torso. Looking straight down, a torso block (columns 20..35, rows 24..39, 0.6 m away: 1.1 m above the root) in the
            // body cylinder, no motion vectors, carried with the play space (0.10 m to the slot) in the previous frame. With the torso step
            // (0.06 m: the head's) it moves 0.16 m (8.5 texels), the play space's step alone 0.10 m (5.3 texels)
            const auto torso=[](unsigned x,unsigned y) {return x>=20 && x<=35 && y>=24 && y<=39;};
            Scene s;s.distance=[torso](unsigned x,unsigned y) {return torso(x,y)?.6f:1.7f;};s.object=torso;s.prev_distance=s.distance;
            d2vr::SpacewarpParams p{};p.rest_on=true;p.rest.t={.10,0,0};p.body_on=true;p.body_up={0,0,1};p.body_axis={0,0,-1.7};p.body_radius=.5;p.body_top=1.4;
            p.prev_on=true;p.prev_tan={-1,1,1,-1};p.prev_m10=0;p.prev_m11=1;p.prev_world.t={.10,0,0};p.carry_on=true;
            p.torso_on=true;p.torso_step={.06,0,0};p.torso_pelvis_h=.7;p.torso_neck_h=1.1;p.torso_rho=0;
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            check(img.b(30,30)>128 && img.b(27,30)<64 && img.b(43,30)>128,"torso: moves with the play space and the head's step (8.5 texels right)");
            d2vr::SpacewarpParams off=p;off.torso_on=false;const auto img0=run(device.Get(),context.Get(),pass,s,off);
            check(img0.b(27,30)>128 && img0.b(41,30)<64,"torso off (v1432): the play space's step only (5.3 texels right)");
            d2vr::SpacewarpParams lean=p;lean.torso_pelvis_h=1.1;lean.torso_neck_h=1.5;  // at the pelvis's height, standing (rho 0): no head step
            const auto img1=run(device.Get(),context.Get(),pass,s,lean);
            check(img1.b(27,30)>128 && img1.b(41,30)<64,"torso at the pelvis's height, standing (rho 0): the play space's step only");
            d2vr::SpacewarpParams walk=lean;walk.torso_rho=1;const auto img2=run(device.Get(),context.Get(),pass,s,walk);
            check(img2.b(30,30)>128 && img2.b(27,30)<64,"torso at the pelvis's height, moving (rho 1): the head's step too");
            // a crate in the cylinder (columns 26..29) that the previous frame shows still in the world (5 texels right there: the camera moved
            // 0.10 m since) and not where the play space would have carried it: scenery, it stays
            const auto crate=[](unsigned x,unsigned y) {return x>=26 && x<=29 && y>=24 && y<=39;};
            Scene wall;wall.distance=[crate](unsigned x,unsigned y) {return crate(x,y)?.6f:1.7f;};wall.object=crate;
            wall.prev_distance=[crate](unsigned x,unsigned y) {return x>=5 && crate(x-5,y)?.6f:1.7f;};
            const auto img3=run(device.Get(),context.Get(),pass,wall,p);
            check(img3.b(27,30)>128 && img3.b(33,30)<64,"torso: scenery in the cylinder (the previous frame shows it still in the world) stays");
        }
        {   // 22. v1460: the torso's own scenery test. Standing (no locomotion: the play space did not move, so "carried with the play space" and
            // "still in the world" are the same test and v1432's could never call anything in the cylinder scenery), the player leans: head,
            // camera and torso moved 0.05 m right since the previous real frame and the head moves on 0.03 m to the slot. A wall patch in the
            // cylinder (columns 26..28, 0.6 m) stood still: the previous frame shows it 2.7 texels further right. A torso block (40..47) moved
            // with the torso: the previous frame shows it where it is now. The wall must stay, the torso move with the head (1.6 texels).
            const auto wall=[](unsigned x,unsigned y) {return x>=26 && x<=28 && y>=24 && y<=39;};
            const auto torso=[](unsigned x,unsigned y) {return x>=40 && x<=47 && y>=24 && y<=39;};
            Scene s;s.distance=[=](unsigned x,unsigned y) {return wall(x,y) || torso(x,y)?.6f:1.7f;};s.object=[=](unsigned x,unsigned y) {return wall(x,y) || torso(x,y);};
            s.prev_distance=[=](unsigned x,unsigned y) {return (x>=3 && wall(x-3,y)) || torso(x,y)?.6f:1.7f;};
            d2vr::SpacewarpParams p{};p.rest_on=true;p.body_on=true;p.body_up={0,0,1};p.body_axis={0,0,-1.7};p.body_radius=.5;p.body_top=1.4;
            p.prev_on=true;p.prev_tan={-1,1,1,-1};p.prev_m10=0;p.prev_m11=1;p.prev_world.t={.05,0,0};p.prev_rest.t={.05,0,0};p.carry_on=true;
            p.torso_on=true;p.torso_step={.03,0,0};p.torso_pelvis_h=.7;p.torso_neck_h=1.1;p.torso_rho=1;
            p.torso_past_on=true;p.torso_past_pelvis={.05,0,0};p.torso_past_neck={.05,0,0};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            // (the wall's edge texel next to where it stood in the previous frame passes, as every previous-frame test does: PrevShows takes
            // the 2 x 2 texels around a point)
            check(img.b(26,30)>128 && img.b(27,30)>128,"torso scenery test: a wall in the cylinder that stood still while the player leaned stays");
            check(img.b(43,30)>128 && img.b(40,30)<64,"torso scenery test: the torso that moved with the head still follows it (1.6 texels right)");
            d2vr::SpacewarpParams old=p;old.torso_past_on=false;const auto img0=run(device.Get(),context.Get(),pass,s,old);
            check(img0.b(26,30)<64 && img0.b(29,30)>128,"without the torso's own step (v1432's test) the same wall moves with the head");
        }
        {   // 13. v1424: the same hand against the sky moves 4 texels right: the sky it uncovers is sky, not the hand looked up again
            const auto hand=[](unsigned x,unsigned y) {return x>=26 && x<34 && y>=26 && y<34;};
            Scene s;s.distance=[hand](unsigned x,unsigned y) {return hand(x,y)?1.f:0.f;};s.object=hand;
            d2vr::SpacewarpParams p{};p.hand_on={true,false};p.hand[0].t={.125,0,0};p.rig_near=.5;
            const double centre=(30.0/N)*2-1;p.palm[0]={centre,-centre,-1,.3};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            int ghost=0;for(unsigned x=20;x<30;++x) ghost=std::max(ghost,static_cast<int>(img.b(x,30)));
            check(ghost<40 && img.b(35,30)>128,"sky: the hand moved, the sky it left shows sky (B up to "+std::to_string(ghost)+")");
        }
        {   // 6. sky only, the camera turned: every pixel is a direction (rotation-only look-up)
            Scene s;s.distance=[](unsigned,unsigned) {return 0.f;};
            const double a=std::atan(.25);d2vr::SpacewarpParams p{};p.m={std::cos(a),0,std::sin(a),0,1,0,-std::sin(a),0,std::cos(a)};
            const auto img=run(device.Get(),context.Get(),pass,s,p);
            // the target's centre ray (0,0,-1) in source coordinates: m * ray = (-sin a, 0, -cos a): tangent -0.25 -> source u = 0.375
            check(near_level(img.r(32,32),static_cast<int>(.375*N*4),4),"sky turned: centre shows source u 0.375 (R "+std::to_string(img.r(32,32))+", want ~96)");
        }
        std::printf("%s: %llu passes\n",failures?"FAILED":"all passed",static_cast<unsigned long long>(pass.passes));
        return failures?1:0;
    } catch(const std::exception& e) {std::cerr<<"error: "<<e.what()<<"\n";return 2;}
}
