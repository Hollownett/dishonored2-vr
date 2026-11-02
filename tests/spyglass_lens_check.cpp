// Lab for src/spyglass_lens.hpp: lens size from the runtime FOV, the game's zoom as a magnification, the zoom tracker through
// the engine's fades, the lens mapping, and the renderView suppression on a fake view (apply, then restore byte for byte).
// With an output path it also renders the lens over a test pattern (PPM) to look at. With an eye capture (RGBA8 file, width,
// height) it runs the real lens pass on a WARP device into a typed sRGB eye (the sim's chain) and a typeless one (the headset's),
// checks both against the source, and writes both images (PPM).
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "../src/spyglass_lens.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace d2vr;
static bool close_to(double a,double b,double e) {return std::abs(a-b)<=e;}
static double rad(double deg) {return deg*spyglass::pi/180;}

static void must(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);std::exit(1);}}
static void gpu_lab(const char* rgba,int w,int h,const std::string& out) {
    using Microsoft::WRL::ComPtr;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(w)*h*4);
    FILE* f=std::fopen(rgba,"rb");must(f && std::fread(image.data(),1,image.size(),f)==image.size(),"eye capture");std::fclose(f);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    must(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
    const double tx=std::tan(rad(50)),ty=tx*h/w;const XrFovf fov{-static_cast<float>(std::atan(tx)),static_cast<float>(std::atan(tx)),static_cast<float>(std::atan(ty)),-static_cast<float>(std::atan(ty))};
    SpyglassLens lens(device.Get());
    for(const auto eye_format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_TYPELESS}) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
        d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA init{image.data(),static_cast<UINT>(w*4),0};ComPtr<ID3D11Texture2D> source,eye,staging;
        must(SUCCEEDED(device->CreateTexture2D(&d,&init,&source)),"source");
        d.Format=eye_format;must(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&eye)),"eye");
        context->CopyResource(eye.Get(),source.Get());
        lens.draw(eye.Get(),source.Get(),fov,2.8f);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;must(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"staging");
        context->CopyResource(staging.Get(),eye.Get());D3D11_MAPPED_SUBRESOURCE map{};must(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"map");
        const auto px=[&](int x,int y) {return static_cast<const std::uint8_t*>(map.pData)+y*map.RowPitch+x*4;};
        const auto src=[&](int x,int y) {return image.data()+(static_cast<std::size_t>(y)*w+x)*4;};
        // Outside the shadow ring the eye is the copy; at the centre the lens shows the source's own centre (magnified about it).
        for(int c=0;c<3;++c) must(px(5,5)[c]==src(5,5)[c] && px(w-6,h-6)[c]==src(w-6,h-6)[c],"outside the lens");
        int centre_diff{};for(int c=0;c<3;++c) centre_diff=std::max(centre_diff,std::abs(px(w/2,h/2)[c]-src(w/2,h/2)[c]));
        must(centre_diff<=12,"centre");
        // Just inside the lens edge the rim darkens what the lens shows there (the source at r*.97/2.8): .31 of it in linear light.
        const double r=spyglass::lens_tan(fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown);
        const int edge=w/2+static_cast<int>(r*.97/tx*w/2),shown=w/2+static_cast<int>(r*.97/2.8/tx*w/2);
        int rim{},under{};for(int c=0;c<3;++c) {rim+=px(edge,h/2)[c];under+=src(shown,h/2)[c];}
        must(rim<=under*.7+18,"rim");
        std::vector<std::uint8_t> ppm;char header[32];const int len=std::snprintf(header,sizeof header,"P6 %d %d 255\n",w,h);ppm.assign(header,header+len);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) ppm.insert(ppm.end(),px(x,y),px(x,y)+3);
        context->Unmap(staging.Get(),0);
        const auto path=out+(eye_format==DXGI_FORMAT_R8G8B8A8_TYPELESS?"-typeless.ppm":"-srgb.ppm");
        if(FILE* o=std::fopen(path.c_str(),"wb")) {std::fwrite(ppm.data(),1,ppm.size(),o);std::fclose(o);}
        std::printf("gpu lab %s: centre diff %d, rim %d vs %d\n",eye_format==DXGI_FORMAT_R8G8B8A8_TYPELESS?"typeless":"srgb",centre_diff,rim,under);
    }
}

int main(int argc,char** argv) {
    // Lens: 35-45 deg across, from the eye's own FOV (a Quest 3 left eye: out 54, in 40, up 44, down 55).
    const double q3=std::atan(spyglass::lens_tan(rad(-54),rad(40),rad(44),rad(-55)))*2*180/spyglass::pi;
    assert(q3>35 && q3<45 && close_to(q3,.21*94*2,.01));
    assert(close_to(std::atan(spyglass::lens_tan(rad(-35),rad(35),rad(35),rad(-35)))*2,rad(35),1e-9));   // narrow headset: 35
    assert(close_to(std::atan(spyglass::lens_tan(rad(-70),rad(70),rad(70),rad(-70)))*2,rad(45),1e-9));   // wide headset: 45
    // The game's levels on a 90 deg camera: m_fov 0.4375 and 0.1875 of the angle.
    assert(close_to(spyglass::magnification(rad(90),rad(90*.4375)),2.795,.002));
    assert(close_to(spyglass::magnification(rad(90),rad(90*.1875)),6.742,.002));
    assert(spyglass::magnification(rad(90),rad(90))==1 && spyglass::magnification(0,rad(40))==1 && spyglass::magnification(rad(90),rad(120))==1);
    // Tracker: base while idle, the engine's fade in, the fade out after the state ended, base again 0.5 s later.
    SpyglassZoom zoom;
    assert(zoom.update(false,rad(90),0)==1 && close_to(zoom.base(),rad(90),1e-12));
    assert(zoom.update(true,rad(90),100)==1);
    assert(close_to(zoom.update(true,rad(90*.4375),300),2.795,.002));
    const double fading=zoom.update(false,rad(60),600);   // state gone, FOV still on its way back
    assert(fading>1.5 && fading<2 && close_to(zoom.base(),rad(90),1e-12));
    assert(zoom.update(false,rad(90),900)==1 && close_to(zoom.base(),rad(90),1e-12));
    assert(zoom.update(false,rad(80),1000)==1 && close_to(zoom.base(),rad(80),1e-12)); // a new FOV setting becomes the base
    // Mapping: the centre stays, inside the lens t/m, a rim that darkens, a shadow ring that fades out by 1.12 R.
    const double R=spyglass::lens_tan(rad(-54),rad(40),rad(44),rad(-55)),m=2.795;
    auto s=spyglass::sample(0,0,R,m);assert(s.x==0 && s.y==0 && s.alpha==1 && s.shade==1);
    s=spyglass::sample(R*.5,-R*.2,R,m);assert(close_to(s.x,R*.5/m,1e-12) && close_to(s.y,-R*.2/m,1e-12) && s.shade==1);
    s=spyglass::sample(R*.999,0,R,m);assert(s.alpha==1 && s.shade<.25f);
    s=spyglass::sample(R*1.05,0,R,m);assert(s.alpha>.2f && s.alpha<.85f && s.shade==0);
    assert(spyglass::sample(R*1.2,0,R,m).alpha==0);
    // Suppression on a fake view: learned without a zoom, then the zoom adds two fsdisto parms and a lens-mask quad.
    std::vector<std::uint8_t> view(0xc00);const auto base=reinterpret_cast<std::uintptr_t>(view.data());
    auto put=[&](std::size_t at,const auto& v) {std::memcpy(view.data()+at,&v,sizeof v);};
    const std::uintptr_t block=base+0x3c0;put(0x3c0,block+0x20);put(0x3d0,block+0x420);
    auto parms=[&](std::initializer_list<std::uint16_t> ids) {std::uint32_t i=0;for(const auto id:ids) {put(0x3c0+0x420+i*2,id);const float v=1.f+id;put(0x3c0+0x20+i*16,v);++i;}put(0x3d8,i|0x5000000u);};
    put(0x8b0,base+0x8c0);put(0x8b8,std::int32_t{5});
    for(std::size_t k=0;k<5;++k) put(0x8c0+k*0x90,base+0x8c0+k*0x90+0x10);
    auto quads=[&](std::size_t k,std::initializer_list<std::uint64_t> materials) {std::uint32_t n=0;for(const auto m:materials) {put(0x8c0+k*0x90+0x10+n*0x20,m);put(0x8c0+k*0x90+0x18+n*0x20,std::uint64_t{0x77+m});++n;}put(0x8c0+k*0x90+8,n|0x5000000u);};
    parms({7,9});quads(3,{0xa000});
    auto inside=[](std::uintptr_t a,std::size_t size,std::uintptr_t lo,std::size_t span) {return a>=lo && a+size<=lo+span;};
    auto read=[&](std::uintptr_t a,void* out,std::size_t size) {if(!inside(a,size,base,view.size())) return false;std::memcpy(out,reinterpret_cast<const void*>(a),size);return true;};
    auto write=[&](std::uintptr_t a,const void* in,std::size_t size) {if(!inside(a,size,base,view.size())) return false;std::memcpy(reinterpret_cast<void*>(a),in,size);return true;};
    SpyglassViewSuppress suppress;suppress.learn(read,base);
    parms({7,31,9,32});quads(3,{0xa000,0xb000});quads(4,{0xc000});view[0xb91]=1;   // the zoom's parms, its mask quad and its blur flag
    const auto zoomed=view;
    suppress.apply(read,write,base);
    assert(suppress.parms==2 && suppress.quads==2 && suppress.blur==1 && suppress.holding());
    std::uint32_t count{};std::memcpy(&count,view.data()+0x3d8,4);std::uint16_t id0{},id1{};std::memcpy(&id0,view.data()+0x7e0,2);std::memcpy(&id1,view.data()+0x7e2,2);
    float v1{};std::memcpy(&v1,view.data()+0x3f0,4);
    assert(count==(2|0x5000000u) && id0==7 && id1==9 && v1==10.f && view[0xb91]==0);   // kept in order, values with them
    std::int32_t n3{},n4{};std::uint64_t m3{};std::memcpy(&n3,view.data()+0x8c0+3*0x90+8,4);std::memcpy(&n4,view.data()+0x8c0+4*0x90+8,4);std::memcpy(&m3,view.data()+0x8c0+3*0x90+0x10,8);
    assert(n3==(1|0x5000000) && m3==0xa000 && n4==0x5000000);   // counts keep their flag byte
    suppress.restore(write);assert(view==zoomed && !suppress.holding());
    // A block that does not keep its own storage is left alone (and so is everything without a baseline).
    put(0x3c0,base+0x100);suppress.apply(read,write,base);assert(suppress.parms==0 && suppress.quads==2);suppress.restore(write);put(0x3c0,block+0x20);assert(view==zoomed);
    SpyglassViewSuppress fresh;fresh.apply(read,write,base);assert(fresh.parms==0 && fresh.quads==0 && fresh.blur==1);fresh.restore(write);assert(view==zoomed);
    if(argc>1) { // render: an eye of +-50 x +-50 deg, a checker pattern of 1 deg squares with a ring every 5 deg, the lens at 2.8x
        const int w=600,h=600;std::vector<std::uint8_t> ppm;const double t=std::tan(rad(50)),r=spyglass::lens_tan(rad(-50),rad(50),rad(50),rad(-50));
        auto pattern=[&](double x,double y) {const double ax=std::atan(x)*180/spyglass::pi,ay=std::atan(y)*180/spyglass::pi;
            const double ring=std::fmod(std::hypot(ax,ay),5.);const bool c=(static_cast<int>(std::floor(ax))+static_cast<int>(std::floor(ay)))&1;
            return ring<.25?40.:(c?200.:120.);};
        char header[32];const int len=std::snprintf(header,sizeof header,"P5 %d %d 255\n",w,h);ppm.assign(header,header+len);
        for(int j=0;j<h;++j) for(int i=0;i<w;++i) {
            const double x=-t+(i+.5)/w*2*t,y=t-(j+.5)/h*2*t;const auto l=spyglass::sample(x,y,r,2.8);
            const double under=pattern(x,y),lens=pattern(l.x,l.y)*l.shade;
            ppm.push_back(static_cast<std::uint8_t>(l.alpha>=1?lens:under*(1-l.alpha)));
        }
        if(FILE* f=std::fopen(argv[1],"wb")) {std::fwrite(ppm.data(),1,ppm.size(),f);std::fclose(f);}
    }
    if(argc>5) gpu_lab(argv[2],std::atoi(argv[3]),std::atoi(argv[4]),argv[5]);
    std::puts("spyglass_lens_check OK");
}
