// v1107 GPU lab: FSR 4 on a centre box vs the whole eye, at the headset's real sizes (render 1922x2070 -> 2496x2688, LDR RGBA8, as the
// upscale mode feeds it). Uses the real AMD provider on this GPU. Reports FFX GPU ms for both, checks the box result matches the full
// result inside the box (away from its edge) and that nothing outside the box is written. Usage: fsr4_fovea_lab.exe [provider] [half_deg]
#include "../src/fsr4_eye_upscaler.hpp"
#include "../src/foveated_upscale.hpp"
#include <cstdio>
#include <vector>
using Ptr11=Microsoft::WRL::ComPtr<ID3D11Texture2D>;
static void check(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
static Ptr11 texture(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format,const void* data,UINT pitch,bool rtv=false) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.ArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|(rtv?D3D11_BIND_RENDER_TARGET:0);
    D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data;initial.SysMemPitch=pitch;
    Ptr11 t;check(SUCCEEDED(device->CreateTexture2D(&desc,data?&initial:nullptr,&t)),"create texture");return t;
}
static std::vector<unsigned char> read(ID3D11Device* device,ID3D11DeviceContext* c,ID3D11Texture2D* t,UINT& pitch) {
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);d.BindFlags=d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr11 s;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&s)),"staging");c->CopyResource(s.Get(),t);
    D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m)),"map");
    std::vector<unsigned char> out(static_cast<std::size_t>(m.RowPitch)*d.Height);std::memcpy(out.data(),m.pData,out.size());pitch=m.RowPitch;c->Unmap(s.Get(),0);return out;
}
static double halton(unsigned i,unsigned b) {double f=1,r=0;while(i) {f/=b;r+=f*(i%b);i/=b;}return r;}
static void wait(d2vr::Fsr4EyeUpscaler& e) {const auto t=GetTickCount64()+10000;while(e.completed_gpu_value()<e.submitted_gpu_value() && GetTickCount64()<t) Sleep(1);}
int wmain(int argc,wchar_t** argv) {
    try {
        const auto provider=std::filesystem::absolute(argc>1?std::filesystem::path(argv[1]):std::filesystem::path(L"third_party/fsr4/bin/amd_fidelityfx_upscaler_dx12.dll"));
        const double half_deg=argc>2?_wtof(argv[2]):30.0;
        const UINT W=2496,H=2688,RW=1922,RH=2070;
        Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> c;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"device");
        // Scene: fine detail (an 11 px stripe pattern, a diagonal ramp, a ring) so the upscaler has real work; static camera.
        std::vector<unsigned char> colour(static_cast<std::size_t>(RW)*RH*4);
        for(UINT y=0;y<RH;++y) for(UINT x=0;x<RW;++x) {
            auto* p=&colour[(static_cast<std::size_t>(y)*RW+x)*4];
            const double r=std::hypot(x-RW*.5,y-RH*.5);
            p[0]=static_cast<unsigned char>(128+100*std::sin(x*0.57));p[1]=static_cast<unsigned char>((x+y)*255/(RW+RH));
            p[2]=static_cast<unsigned char>(128+120*std::sin(r*0.21));p[3]=255;
        }
        std::vector<float> depth(static_cast<std::size_t>(RW)*RH,.01f);std::vector<unsigned short> mv(static_cast<std::size_t>(RW)*RH*2,0);
        auto col=texture(device.Get(),RW,RH,DXGI_FORMAT_R8G8B8A8_UNORM,colour.data(),RW*4);
        auto dep=texture(device.Get(),RW,RH,DXGI_FORMAT_R32_FLOAT,depth.data(),RW*4);
        auto mot=texture(device.Get(),RW,RH,DXGI_FORMAT_R16G16_FLOAT,mv.data(),RW*4);
        // The box: a symmetric 110 deg eye (tan 1.43) for the NDC half extent of half_deg, centred.
        const double half_ndc=std::tan(half_deg*3.14159265358979/180)/1.43;
        const auto box=d2vr::fovea::box(W,H,RW,RH,0,0,half_ndc,half_ndc);
        check(box.valid(),"no valid fovea box");
        std::printf("box: render %ux%u at (%u,%u) -> output %ux%u at (%u,%u), alignment error %.3f/%.3f px, %.1f%% of the output\n",
            box.x.render,box.y.render,box.x.src,box.y.src,box.x.display,box.y.display,box.x.dst,box.y.dst,box.x.error_px,box.y.error_px,
            100.0*box.x.display*box.y.display/(W*H));
        auto run=[&](bool crop,ID3D11Texture2D* out,unsigned frames,double& gpu_ms) {
            d2vr::Fsr4EyeUpscaler e;d2vr::Fsr4EyeUpscaler::Desc desc{};desc.hdr=false;desc.auto_exposure=false;desc.mv_jittered=true;
            desc.render_w=crop?box.x.render:RW;desc.render_h=crop?box.y.render:RH;desc.display_w=crop?box.x.display:W;desc.display_h=crop?box.y.display:H;
            check(e.create(device.Get(),desc,provider.c_str(),DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM),e.last_error());
            d2vr::Fsr4EyeUpscaler::Inputs in{};in.colour=col.Get();in.depth=dep.Get();in.motion=mot.Get();in.output=out;
            in.render_w=desc.render_w;in.render_h=desc.render_h;in.mv_scale_x=static_cast<float>(RW);in.mv_scale_y=static_cast<float>(RH);
            in.vfov_rad=1.6f;in.cam_near=.01f;in.cam_far=1e6f;in.sharpness=.5f;
            if(crop) {in.src_x=box.x.src;in.src_y=box.y.src;in.dst_x=box.x.dst;in.dst_y=box.y.dst;in.output_region=true;}
            double total=0;unsigned samples=0;
            for(unsigned f=0;f<frames;++f) {
                in.jitter_px_x=static_cast<float>(halton(f%8+1,2)-.5);in.jitter_px_y=static_cast<float>(halton(f%8+1,3)-.5);in.reset=f==0;
                check(e.dispatch(c.Get(),in,true),e.last_error());c->Flush();wait(e);e.poll_gpu_timing();
                if(f>=8 && e.compute_gpu_samples()) {total+=e.compute_gpu_ms();++samples;}
            }
            gpu_ms=samples?total/samples:0;e.destroy();
        };
        auto full_out=texture(device.Get(),W,H,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,0,true);
        auto crop_out=texture(device.Get(),W,H,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,0,true);
        {   // marker outside the box: a crop dispatch must leave it
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> v;check(SUCCEEDED(device->CreateRenderTargetView(crop_out.Get(),nullptr,&v)),"rtv");
            const float marker[4]{7/255.f,9/255.f,11/255.f,1};c->ClearRenderTargetView(v.Get(),marker);
        }
        double full_ms=0,crop_ms=0;
        run(false,full_out.Get(),40,full_ms);run(true,crop_out.Get(),40,crop_ms);
        UINT fp=0,cp=0;const auto a=read(device.Get(),c.Get(),full_out.Get(),fp),b=read(device.Get(),c.Get(),crop_out.Get(),cp);
        // inside the box, 24 px from its edge: the crop matches the full result
        double sum=0;unsigned long long n=0;int worst=0;
        for(UINT y=box.y.dst+24;y<box.y.dst+box.y.display-24;++y) for(UINT x=box.x.dst+24;x<box.x.dst+box.x.display-24;++x)
            for(int k=0;k<3;++k) {const int d=std::abs(int(a[y*fp+x*4+k])-int(b[y*cp+x*4+k]));sum+=d;++n;worst=(std::max)(worst,d);}
        const double mean=sum/n;
        // outside the box: untouched
        unsigned long long touched=0;
        for(UINT y=0;y<H;y+=7) for(UINT x=0;x<W;x+=7) {
            if(x>=box.x.dst && x<box.x.dst+box.x.display && y>=box.y.dst && y<box.y.dst+box.y.display) continue;
            const auto* p=&b[y*cp+x*4];if(p[0]!=7 || p[1]!=9 || p[2]!=11) ++touched;
        }
        std::printf("FFX GPU ms: full %.3f, box %.3f (%.0f%%); box vs full inside: mean |diff| %.3f, worst %d (8-bit); outside written: %llu\n",
            full_ms,crop_ms,full_ms>0?100*crop_ms/full_ms:0,mean,worst,touched);
        check(touched==0,"crop wrote outside its box");
        check(mean<2.0,"crop differs from the full result inside the box");
        check(full_ms>0 && crop_ms>0 && crop_ms<full_ms,"crop is not cheaper");
        std::printf("PASS fsr4 fovea lab\n");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FSR4 FOVEA LAB FAILED: %s\n",e.what());return 1;}
}
