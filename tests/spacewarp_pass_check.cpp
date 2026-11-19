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
        colour[i*4]=static_cast<unsigned char>(x*4);colour[i*4+1]=static_cast<unsigned char>(y*4);colour[i*4+2]=scene.object(x,y)?255:0;colour[i*4+3]=255;
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
            d2vr::SpacewarpParams p{};p.motion_share=1;const auto img=run(device.Get(),context.Get(),pass,s,p);
            // (the strip it vacates, 20..23, is stretched from both sides: same depth; the cell next to the wall takes the wall)
            check(img.b(42,30)>128 && img.b(25,30)>128 && img.b(21,30)<64,"mover moved 4 texels right (B at 42 and 25, not at 21)");
            d2vr::SpacewarpParams still{};const auto img0=run(device.Get(),context.Get(),pass,s,still);
            check(img0.b(21,30)>128 && img0.b(41,30)<64,"motion_share 0 leaves the mover in place");
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
